/*	$Id$ */
/*
 * Copyright (c) 2018 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#if HAVE_SYS_QUEUE
# include <sys/queue.h>
#endif
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tls.h>
#include <unistd.h>

#include "extern.h"
#include "slant.h"

/*
 * Close out a connection (its file descriptor).
 * This is sensitive to whether we're https (tls_close) or not.
 * Return zero if the closing needs reentrancy (we need to re-call the
 * function), non-zero if the connection has closed.
 */
static int
http_close_inner(WINDOW *errwin, struct node *n, time_t t)
{
	int	 c;

	if ( ! n->addrs.https) {
		close(n->xfer.pfd->fd);
		n->xfer.pfd->fd = -1;
		return 1;
	}

	/* TODO: close timeout handler? */

	c = tls_close(n->xfer.tls);
	if (TLS_WANT_POLLIN == c) {
		n->xfer.lastio = t;
		n->xfer.pfd->events = POLLIN;
		return 0;
	} else if (TLS_WANT_POLLOUT == c) {
		n->xfer.lastio = t;
		n->xfer.pfd->events = POLLOUT;
		return 0;
	} 
	
	close(n->xfer.pfd->fd);
	n->xfer.pfd->fd = -1;
	return 1;
}

int
http_close_err(struct out *out, struct node *n, time_t t)
{
	int	 c;

	if (0 != (c = http_close_inner(out->errwin, n, t))) {
		n->addrs.curaddr = 
			(n->addrs.curaddr + 1) % n->addrs.addrsz;
		n->state = STATE_CONNECT_WAITING;
		n->waitstart = t;
		return 1;
	}

	n->state = STATE_CLOSE_ERR;
	return 1;
}

/*
 * After closing out a connection, we're ready to parse the HTTP buffer
 * placed into n->xfer.
 * Returns zero on failure (fatal), non-zero on success (or non-fatal
 * errors in the data).
 */
static int
http_close_done_ok(struct out *out, struct node *n, time_t t)
{
	char	*end, *sv, *start = n->xfer.rbuf, *val;
	size_t	 len, sz = n->xfer.rbufsz;
	int	 rc, httpok = 0;

	n->state = STATE_CONNECT_WAITING;
	n->waitstart = t;

	/*
	 * Start with HTTP headers, reading up until the double CRLF.
	 * We care about a few headers, such as the version response and
	 * the date (maybe others in the future).
	 */

	while (NULL != (end = memmem(start, sz, "\r\n", 2))) {
		/* 
		 * NUL-terminate the current line.
		 * Position the next line ("start") after the CRLF. 
		 * Keep the current start of the line in "sv".
		 * Adjust buffer size less current line and CRLF.
		 */

		*end = '\0';
		sv = start;
		len = end - start;
		start = end + 2;
		sz -= len + 2;

		/* Not allowed, but harmless. */

		if (0 == len)
			break;

		/* 
		 * Do we have the status line?
		 * If so, all we want to see is whether we have an
		 * HTTP/200 or not.
		 * XXX: if we were to have a redirect, that should be
		 * handled here by re-initialising the URL and then
		 * re-acquiring the IPs.
		 * However, slant currently can't handle that because it
		 * needs to have a subprocess for DNS (async DNS is too
		 * damn complicated, and we'd rather not have a dns
		 * pledge), which it doesn't have yet.
		 */

		if (len >= 13 &&
		    (0 == memcmp(sv, "HTTP/1.0 200 ", 13) ||
		     0 == memcmp(sv, "HTTP/1.1 200 ", 13))) {
			httpok = 1;
			continue;
		}

		/* 
		 * Do we have an HTTP key/value pair? 
		 * If not, the line is bogus (harmless) or a non-200.
		 */

		if (NULL == (val = strchr(sv, ':')))
			continue;

		*val = '\0';
		val++;

		/* TODO. */
	}

	/*
	 * If we encountered an HTTP/200, then we're good to investigate
	 * the message itself.
	 * Otherwise, dump the entire HTTP response to our log file.
	 */

	if ( ! httpok) {
		xwarnx(out, "bad HTTP response (%lld seconds): "
			"%s", t - n->xfer.start, n->host);
		fprintf(out->errs, "------>------\n");
		fprintf(out->errs, "%.*s\n", (int)n->xfer.rbufsz, 
			n->xfer.rbuf);
		fprintf(out->errs, "------<------\n");
		fflush(out->errs);
		rc = 1;
	} else if ((rc = json_parse(out, n, start, sz)) > 0) {
		/*
		 * XXX: should a bad (rc == 0) JSON parse really trigger
		 * the fatal error condition?
		 * First let's see if we pick this up in reality.
		 */
		n->dirty = 1;
		n->lastseen = t;
	}

	free(n->xfer.rbuf);
	n->xfer.rbuf = NULL;
	n->xfer.rbufsz = 0;
	return rc >= 0;
}

int
http_close_done(struct out *out, struct node *n, time_t t)
{
	int	 c;

	if ((c = http_close_inner(out->errwin, n, t)) > 0)
		return http_close_done_ok(out, n, t);

	n->state = STATE_CLOSE_DONE;
	return 1;
}

/*
 * Prepare the write buffer. 
 * Return zero on failure, non-zero on success.
 */
static int
http_write_ready(struct out *out, struct node *n, time_t t)
{
	int	 c;

	if (n->addrs.https) {
		c = tls_connect_socket(n->xfer.tls, 
			n->xfer.pfd->fd, n->host);
		if (c < 0) {
			xwarnx(out, "tls_connect_socket: %s: %s", 
				n->host, tls_error(n->xfer.tls));
		}
	}

	n->xfer.wbufsz = n->xfer.wbufpos = 0;
	free(n->xfer.wbuf);
	n->xfer.wbuf = NULL;

	c = NULL != n->httpauth ?
		asprintf(&n->xfer.wbuf,
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"Authorization: Basic %s\r\n"
			"\r\n",
			n->path, n->host, n->httpauth) :
		asprintf(&n->xfer.wbuf,
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"\r\n",
			n->path, n->host);

	if (c < 0) {
		xwarn(out, NULL);
		return 0;
	}

	n->xfer.wbufsz = c;
	n->xfer.lastio = t;
	n->state = STATE_WRITE;

	if (n->addrs.https)
		n->xfer.pfd->events = POLLOUT|POLLIN;
	else
		n->xfer.pfd->events = POLLOUT;
	return 1;
}

/*
 * Initialise a socket descriptor to the current endpoint.
 * Moves into STATE_CONNECT for async connection, STATE_WRITE on
 * success, or calls http_close_err() on connection failure.
 * Returns zero on system failure, non-zero on success.
 */
int
http_init_connect(struct out *out, struct node *n, time_t t)
{
	int		   family, c, flags;
	socklen_t	   sslen;
	struct tls_config *cfg;

	memset(&n->xfer.ss, 0, sizeof(struct sockaddr_storage));

	if (NULL == n->xfer.tls) {
		if (NULL == (n->xfer.tls = tls_client())) {
			xwarn(out, "tls_client");
			return 0;
		}
	} else
		tls_reset(n->xfer.tls);

	if (NULL != n->xfer.tls) {
		if (NULL == (cfg = tls_config_new())) {
			xwarn(out, "tls_config_new");
			return 0;
		}
		tls_config_set_protocols(cfg, TLS_PROTOCOLS_ALL);
		if (-1 == tls_configure(n->xfer.tls, cfg)) {
			xwarnx(out, "tls_configure: %s: %s", n->host,
				tls_error(n->xfer.tls));
			return 0;
		}
		tls_config_free(cfg);
	}

	if (4 == n->addrs.addrs[n->addrs.curaddr].family) {
		family = PF_INET;
		((struct sockaddr_in *)&n->xfer.ss)->sin_family = AF_INET;
		((struct sockaddr_in *)&n->xfer.ss)->sin_port = 
			htons(n->addrs.port);
		c = inet_pton(AF_INET, 
			n->addrs.addrs[n->addrs.curaddr].ip,
			&((struct sockaddr_in *)&n->xfer.ss)->sin_addr);
		sslen = sizeof(struct sockaddr_in);
	} else {
		family = PF_INET6;
		((struct sockaddr_in6 *)&n->xfer.ss)->sin6_family = AF_INET6;
		((struct sockaddr_in6 *)&n->xfer.ss)->sin6_port =
			htons(n->addrs.port);
		c = inet_pton(AF_INET6, 
			n->addrs.addrs[n->addrs.curaddr].ip,
			&((struct sockaddr_in6 *)&n->xfer.ss)->sin6_addr);
		sslen = sizeof(struct sockaddr_in6);
	} 

	if (c < 0) {
		xwarn(out, "cannot convert: %s: %s", n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return 0;
	} else if (0 == c) {
		xwarnx(out, "cannot convert: %s: %s", n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return 0;
	}

	n->xfer.pfd->events = POLLOUT;
	n->xfer.pfd->fd = socket(family, SOCK_STREAM, 0);

	if (-1 == n->xfer.pfd->fd) {
		xwarn(out, "socket");
		return 0;
	}

	/* Set up non-blocking mode. */

	if (-1 == (flags = fcntl(n->xfer.pfd->fd, F_GETFL, 0))) {
		xwarn(out, "fcntl");
		return 0;
	}
	if (-1 == fcntl(n->xfer.pfd->fd, F_SETFL, flags|O_NONBLOCK)) {
		xwarn(out, "fcntl");
		return 0;
	}

	n->state = STATE_CONNECT;
	n->xfer.start = n->xfer.lastio = t;

	/* This is from connect(2): asynchronous connection. */

	c = connect(n->xfer.pfd->fd, 
		(struct sockaddr *)&n->xfer.ss, sslen);
	if (0 == c)
		return http_write_ready(out, n, t);

	/* Asynchronous connect... */

	if (EINTR == errno || 
	    EINPROGRESS == errno)
		return 1;

	if (ETIMEDOUT == errno ||
	    ECONNREFUSED == errno ||
	    EHOSTUNREACH == errno ||
	    ENETDOWN == errno ||
	    ENETUNREACH == errno) {
		xwarn(out, "connect (transient): %s: %s", 
			n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return http_close_err(out, n, t);
	}

	xwarn(out, "connect: %s: %s", n->host, 
		n->addrs.addrs[n->addrs.curaddr].ip);

	return 0;
}

/*
 * Check for asynchronous connect(2).
 * Returns zero on system failure, non-zero on success.
 * Calls http_close_err() if connection fails or transitions to
 * STATE_WRITE on success.
 */
int
http_connect(struct out *out, struct node *n, time_t t)
{
	int	  c;
	int 	  error = 0;
	socklen_t len = sizeof(error);

	assert(-1 != n->xfer.pfd->fd);

	if ((POLLNVAL & n->xfer.pfd->revents) ||
	    (POLLERR & n->xfer.pfd->revents)) {
		xwarn(out, "poll (connect): %lld seconds, %s: %s", 
			t - n->xfer.start, n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return 0;
	} else if (POLLHUP & n->xfer.pfd->revents) {
		xwarnx(out, "poll hup (connect): %lld seconds, %s: %s",
			t - n->xfer.start, n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return http_close_err(out, n, t);
	} 

	assert(t >= n->xfer.lastio);
	if (t - n->xfer.lastio > n->timeout) {
		xwarnx(out, "connect timeout: %lld seconds, %s: %s", 
			t - n->xfer.start, n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return http_close_err(out, n, t);
	}
	
	if ( ! (POLLOUT & n->xfer.pfd->revents))
		return 1;

	c = getsockopt(n->xfer.pfd->fd, 
		SOL_SOCKET, SO_ERROR, &error, &len);

	if (c < 0) {
		xwarn(out, "getsockopt: %s: %s",
			n->host, n->addrs.addrs[n->addrs.curaddr].ip);
		return 0;
	} else if (0 == error)
		return http_write_ready(out, n, t);

	errno = error;
	if (ETIMEDOUT == errno ||
	    ECONNREFUSED == errno ||
	    EHOSTUNREACH == errno ||
	    ENETDOWN == errno ||
	    ENETUNREACH == errno) {
		xwarn(out, "getsockopt (transient): %s: %s",
			n->host, n->addrs.addrs[n->addrs.curaddr].ip);
		return http_close_err(out, n, t);
	}

	xwarn(out, "getsockopt: %s: %s",
		n->host, n->addrs.addrs[n->addrs.curaddr].ip);
	return 0;
}

/*
 * Write to the file descriptor.
 * Returns zero on system failure, non-zero on success.
 * When the request has been written, update state to be
 * STATE_READ.
 * On connect/write requests, calls http_close_err() for further
 * changing of state.
 */
int
http_write(struct out *out, struct node *n, time_t t)
{
	ssize_t	 ssz;

	assert(-1 != n->xfer.pfd->fd);
	assert(NULL != n->xfer.wbuf);
	assert(n->xfer.wbufsz > 0);

	if ((POLLNVAL & n->xfer.pfd->revents) ||
	    (POLLERR & n->xfer.pfd->revents)) {
		xwarn(out, "poll errors: %s: %s", n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return 0;
	} else if (POLLHUP & n->xfer.pfd->revents) {
		xwarnx(out, "poll hup (write): %lld seconds, %s: %s", 
			t - n->xfer.start, n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return http_close_err(out, n, t);
	}

	assert(t >= n->xfer.lastio);
	if (t - n->xfer.lastio > n->timeout) {
		xwarnx(out, "write timeout: %lld seconds, %s: %s", 
			t - n->xfer.start, n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return http_close_err(out, n, t);
	}
	
	if ( ! (POLLOUT & n->xfer.pfd->revents) &&
	     ! (POLLIN & n->xfer.pfd->revents))
		return 1;

	if (n->addrs.https) {
		ssz = tls_write(n->xfer.tls,
			n->xfer.wbuf + n->xfer.wbufpos, 
			n->xfer.wbufsz);
		if (TLS_WANT_POLLOUT == ssz) {
			n->xfer.pfd->events = POLLOUT;
			return 1;
		} else if (TLS_WANT_POLLIN == ssz) {
			n->xfer.pfd->events = POLLIN;
			return 1;
		} else if (ssz < 0) {
			xwarnx(out, "tls_write: %s: %s: %s", 
				n->host, 
				n->addrs.addrs[n->addrs.curaddr].ip,
				tls_error(n->xfer.tls));
			return http_close_err(out, n, t);
		}
	} else {
		ssz = write(n->xfer.pfd->fd, n->xfer.wbuf + 
			n->xfer.wbufpos, n->xfer.wbufsz);
		if (ssz < 0) {
			xwarn(out, "write: %s: %s", n->host, 
				n->addrs.addrs[n->addrs.curaddr].ip);
			return http_close_err(out, n, t);
		}
	}

	n->xfer.lastio = t;
	n->xfer.wbufsz -= ssz;
	n->xfer.wbufpos += ssz;

	if (n->xfer.wbufsz > 0)
		return 1;

	free(n->xfer.wbuf);
	n->xfer.wbuf = NULL;
	n->state = STATE_READ;
	free(n->xfer.rbuf);
	n->xfer.rbuf = NULL;
	n->xfer.rbufsz = 0;
	if (n->addrs.https)
		n->xfer.pfd->events = POLLOUT|POLLIN;
	else
		n->xfer.pfd->events = POLLIN;
	return 1;
}

/*
 * Read from the file descriptor.
 * Returns zero on system failure, non-zero on success.
 * When the file descriptor has been read, sets state to
 * STATE_CONNECT_WAITING.
 */
int
http_read(struct out *out, struct node *n, time_t t)
{
	ssize_t	 ssz;
	char	 buf[1024 * 5];
	void	*pp;

	assert(STATE_READ == n->state);
	assert(-1 != n->xfer.pfd->fd);

	/* Check for poll(2) errors and readability. */

	if ((POLLNVAL & n->xfer.pfd->revents) ||
	    (POLLERR & n->xfer.pfd->revents)) {
		xwarnx(out, "poll errors: %s: %s", n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return 0;
	} else if (POLLHUP & n->xfer.pfd->revents) {
		xwarnx(out, "poll hup (read): %lld seconds, %s: %s", 
			t - n->xfer.start, n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return http_close_err(out, n, t);
	}

	assert(t >= n->xfer.lastio);
	if (t - n->xfer.lastio > n->timeout) {
		xwarnx(out, "read timeout: %lld seconds, %s: %s", 
			t - n->xfer.start, n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return http_close_err(out, n, t);
	}

	if ( ! (POLLOUT & n->xfer.pfd->revents) &&
	     ! (POLLIN & n->xfer.pfd->revents))
		return 1;

	/* Read into a static buffer. */

	if (n->addrs.https) {
		ssz = tls_read(n->xfer.tls, buf, sizeof(buf));
		if (TLS_WANT_POLLOUT == ssz) {
			n->xfer.pfd->events = POLLOUT;
			return 1;
		} else if (TLS_WANT_POLLIN == ssz) {
			n->xfer.pfd->events = POLLIN;
			return 1;
		} else if (ssz < 0) {
			xwarnx(out, "tls_read: %s: %s: %s", 
				n->host, 
				n->addrs.addrs[n->addrs.curaddr].ip,
				tls_error(n->xfer.tls));
			return http_close_err(out, n, t);
		}
	} else {
		ssz = read(n->xfer.pfd->fd, buf, sizeof(buf));
		if (ssz < 0) {
			xwarn(out, "read: %s: %s", n->host, 
				n->addrs.addrs[n->addrs.curaddr].ip);
			return http_close_err(out, n, t);
		}
	}

	n->xfer.lastio = t;

	if (0 == ssz)
		return http_close_done(out, n, t);

	/* Copy static into dynamic buffer. */

	pp = realloc(n->xfer.rbuf, n->xfer.rbufsz + ssz);
	if (NULL == pp) {
		xwarn(out, NULL);
		return 0;
	}
	n->xfer.rbuf = pp;
	memcpy(n->xfer.rbuf + n->xfer.rbufsz, buf, ssz);
	n->xfer.rbufsz += ssz;
	return 1;
}

