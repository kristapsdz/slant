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
#include <sys/queue.h>
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
http_close_inner(WINDOW *errwin, struct node *n)
{
	int	 c;

	if ( ! n->addrs.https) {
		close(n->xfer.pfd->fd);
		n->xfer.pfd->fd = -1;
		return 1;
	}

	c = tls_close(n->xfer.tls);
	if (TLS_WANT_POLLIN == c) {
		n->xfer.pfd->events = POLLIN;
		return 0;
	} else if (TLS_WANT_POLLOUT == c) {
		n->xfer.pfd->events = POLLOUT;
		return 0;
	} 
	
	close(n->xfer.pfd->fd);
	n->xfer.pfd->fd = -1;
	return 1;
}

int
http_close_err(struct out *out, struct node *n)
{
	int	 c;

	if (0 != (c = http_close_inner(out->errwin, n))) {
		n->addrs.curaddr = 
			(n->addrs.curaddr + 1) % n->addrs.addrsz;
		n->state = STATE_CONNECT_WAITING;
		n->waitstart = time(NULL);
		return 1;
	}

	n->state = STATE_CLOSE_ERR;
	return 1;
}

static int
http_close_done_ok(struct out *out, struct node *n)
{
	char	*end, *sv, *start = n->xfer.rbuf;
	size_t	 len, sz = n->xfer.rbufsz;
	int	 rc, httpok = 0;
	time_t	 t = time(NULL);

	n->state = STATE_CONNECT_WAITING;
	n->waitstart = t;

	while (NULL != (end = memmem(start, sz, "\r\n", 2))) {
		sv = start;
		len = end - start;
		start = end + 2;
		sz -= len + 2;
		if (0 == len)
			break;
		else if (len < 13)
			continue;
		if (0 == memcmp(sv, "HTTP/1.0 200 ", 13)) 
			httpok = 1;
	}

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
		n->dirty = 1;
		n->lastseen = time(NULL);
	}

	free(n->xfer.rbuf);
	n->xfer.rbuf = NULL;
	n->xfer.rbufsz = 0;
	return rc >= 0;
}

int
http_close_done(struct out *out, struct node *n)
{
	int	 c;

	if ((c = http_close_inner(out->errwin, n)) > 0)
		return http_close_done_ok(out, n);

	n->state = STATE_CLOSE_DONE;
	return 1;
}

/*
 * Prepare the write buffer. 
 * Return zero on failure, non-zero on success.
 */
static int
http_write_ready(struct out *out, struct node *n)
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

	c = asprintf(&n->xfer.wbuf,
		"GET %s HTTP/1.0\r\n"
		"Host: %s\r\n"
		"\r\n",
		n->path, n->host);

	if (c < 0) {
		xwarn(out, NULL);
		return 0;
	}

	n->xfer.wbufsz = c;
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
http_init_connect(struct out *out, struct node *n)
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
	n->xfer.start = time(NULL);

	/* This is from connect(2): asynchronous connection. */

	c = connect(n->xfer.pfd->fd, 
		(struct sockaddr *)&n->xfer.ss, sslen);
	if (0 == c)
		return http_write_ready(out, n);

	/* Asynchronous connect... */

	if (EINTR == errno || 
	    EINPROGRESS == errno)
		return 1;

	if (ETIMEDOUT == errno ||
	    ECONNREFUSED == errno ||
	    EHOSTUNREACH == errno ||
	    ENETUNREACH == errno) {
		xwarn(out, "connect (transient): %s: %s", 
			n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return http_close_err(out, n);
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
http_connect(struct out *out, struct node *n)
{
	int	  c;
	int 	  error = 0;
	time_t	  t = time(NULL);
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
		return http_close_err(out, n);
	} else if ( ! (POLLOUT & n->xfer.pfd->revents))
		return 1;

	c = getsockopt(n->xfer.pfd->fd, 
		SOL_SOCKET, SO_ERROR, &error, &len);

	if (c < 0) {
		xwarn(out, "getsockopt: %s: %s",
			n->host, n->addrs.addrs[n->addrs.curaddr].ip);
		return 0;
	} else if (0 == error)
		return http_write_ready(out, n);

	errno = error;
	if (ETIMEDOUT == errno ||
	    ECONNREFUSED == errno ||
	    EHOSTUNREACH == errno ||
	    ENETUNREACH == errno) {
		xwarn(out, "getsockopt (transient): %s: %s",
			n->host, n->addrs.addrs[n->addrs.curaddr].ip);
		return http_close_err(out, n);
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
http_write(struct out *out, struct node *n)
{
	ssize_t	 ssz;
	time_t	 t = time(NULL);

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
		return http_close_err(out, n);
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
			return 0;
		}
	} else {
		ssz = write(n->xfer.pfd->fd, n->xfer.wbuf + 
			n->xfer.wbufpos, n->xfer.wbufsz);
		if (ssz < 0) {
			xwarn(out, "write: %s: %s", n->host, 
				n->addrs.addrs[n->addrs.curaddr].ip);
			return 0;
		}
	}
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
http_read(struct out *out, struct node *n)
{
	ssize_t	 ssz;
	char	 buf[1024 * 5];
	void	*pp;
	time_t	 t = time(NULL);

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
		return http_close_err(out, n);
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
			return 0;
		}
	} else {
		ssz = read(n->xfer.pfd->fd, buf, sizeof(buf));
		if (ssz < 0) {
			xwarn(out, "read: %s: %s", n->host, 
				n->addrs.addrs[n->addrs.curaddr].ip);
			return 0;
		}
	}

	if (0 == ssz)
		return http_close_done(out, n);

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

