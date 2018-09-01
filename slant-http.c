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
	
#if 0
	/* This almost always reports superfluous errors. */
	if (c < 0) {
		warnx("%s: tls_close: %s", n->host,
			tls_error(n->xfer.tls));
		return -1;
	}
#endif

	close(n->xfer.pfd->fd);
	n->xfer.pfd->fd = -1;
	return 1;
}

static int
http_close_err_ok(struct node *n)
{

	n->addrs.curaddr = 
		(n->addrs.curaddr + 1) % n->addrs.addrsz;
	n->state = STATE_CONNECT_WAITING;
	n->waitstart = time(NULL);
	return 1;
}

int
http_close_err(WINDOW *errwin, struct node *n)
{
	int	 c;

	if ((c = http_close_inner(errwin, n)) < 0)
		return 0;
	else if (c > 0)
		return http_close_err_ok(n);

	n->state = STATE_CLOSE_ERR;
	return 1;
}

static int
http_close_done_ok(WINDOW *errwin, struct node *n)
{
	char	*end, *sv, *start = n->xfer.rbuf;
	size_t	 len, sz = n->xfer.rbufsz;
	int	 rc, httpok = 0;

	n->state = STATE_CONNECT_WAITING;
	n->waitstart = time(NULL);

	while (NULL != (end = memmem(start, sz, "\r\n", 2))) {
		sv = start;
		len = end - start;
		start = end + 2;
		sz -= len + 2;
		if (0 == len)
			break;
		sv[len] = '\0';
		if (0 == strncmp(sv, "HTTP/1.0 ", 9)) {
			sv += 9;
			if (0 == strncmp(sv, "200 ", 4)) {
				httpok = 1;
				continue;
			}
			xwarnx(errwin, "bad HTTP response: %s: %.3s",
				n->host, sv);
			return 1;
		}
	}

	if ( ! httpok) {
		xwarnx(errwin, "no HTTP response: %s", n->host);
		rc = 1;
	} else if ((rc = jsonobj_parse(n, start, sz)) > 0) {
		n->dirty = 1;
		n->lastseen = time(NULL);
	}

	free(n->xfer.rbuf);
	n->xfer.rbuf = NULL;
	n->xfer.rbufsz = 0;
	return rc >= 0;
}

int
http_close_done(WINDOW *errwin, struct node *n)
{
	int	 c;

	if ((c = http_close_inner(errwin, n)) < 0)
		return 0;
	else if (c > 0)
		return http_close_done_ok(errwin, n);

	n->state = STATE_CLOSE_DONE;
	return 1;
}

/*
 * Prepare the write buffer. 
 * Return zero on failure, non-zero on success.
 */
static int
http_write_ready(WINDOW *errwin, struct node *n)
{
	int	 c;

	if (n->addrs.https) {
		c = tls_connect_socket(n->xfer.tls, 
			n->xfer.pfd->fd, n->host);
		if (c < 0) {
			xwarnx(errwin, "tls_connect_socket: %s: %s", 
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
		xwarn(errwin, NULL);
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
http_init_connect(WINDOW *errwin, struct node *n)
{
	int		   family, c, flags;
	socklen_t	   sslen;
	struct tls_config *cfg;

	memset(&n->xfer.ss, 0, sizeof(struct sockaddr_storage));

	if (NULL == n->xfer.tls) {
		if (NULL == (n->xfer.tls = tls_client())) {
			xwarn(errwin, "tls_client");
			return 0;
		}
	} else
		tls_reset(n->xfer.tls);

	if (NULL != n->xfer.tls) {
		if (NULL == (cfg = tls_config_new())) {
			xwarn(errwin, "tls_config_new");
			return 0;
		}
		tls_config_set_protocols(cfg, TLS_PROTOCOLS_ALL);
		if (-1 == tls_configure(n->xfer.tls, cfg)) {
			xwarnx(errwin, "tls_configure: %s: %s", n->host,
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
		xwarn(errwin, "cannot convert: %s: %s", n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return 0;
	} else if (0 == c) {
		xwarnx(errwin, "cannot convert: %s: %s", n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return 0;
	}

	n->xfer.pfd->events = POLLOUT;
	n->xfer.pfd->fd = socket(family, SOCK_STREAM, 0);

	if (-1 == n->xfer.pfd->fd) {
		xwarn(errwin, "socket");
		return 0;
	}

	/* Set up non-blocking mode. */

	if (-1 == (flags = fcntl(n->xfer.pfd->fd, F_GETFL, 0))) {
		xwarn(errwin, "fcntl");
		return 0;
	}
	if (-1 == fcntl(n->xfer.pfd->fd, F_SETFL, flags|O_NONBLOCK)) {
		xwarn(errwin, "fcntl");
		return 0;
	}

	n->state = STATE_CONNECT;

	/* This is from connect(2): asynchronous connection. */

	c = connect(n->xfer.pfd->fd, 
		(struct sockaddr *)&n->xfer.ss, sslen);
	if (c && (EINTR == errno || EINPROGRESS == errno))
		return 1;
	else if (c == 0)
		return http_write_ready(errwin, n);

	if (ETIMEDOUT == errno ||
	    ECONNREFUSED == errno ||
	    EHOSTUNREACH == errno ||
	    ENETUNREACH == errno)
		return http_close_err(errwin, n);

	xwarn(errwin, "connect: %s: %s", n->host, 
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
http_connect(WINDOW *errwin, struct node *n)
{
	int	  c;
	int 	  error = 0;
	socklen_t len = sizeof(error);

	assert(-1 != n->xfer.pfd->fd);

	if ((POLLNVAL & n->xfer.pfd->revents) ||
	    (POLLERR & n->xfer.pfd->revents)) {
		xwarn(errwin, "poll: %s: %s", n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return 0;
	} else if (POLLHUP & n->xfer.pfd->revents) {
		return http_close_err(errwin, n);
	} else if ( ! (POLLOUT & n->xfer.pfd->revents))
		return 1;

	c = getsockopt(n->xfer.pfd->fd, 
		SOL_SOCKET, SO_ERROR, &error, &len);

	if (c < 0) {
		xwarn(errwin, "getsockopt");
		return 0;
	} else if (0 == error)
		return http_write_ready(errwin, n);

	errno = error;
	if (ETIMEDOUT == errno ||
	    ECONNREFUSED == errno ||
	    EHOSTUNREACH == errno ||
	    ENETUNREACH == errno)
		return http_close_err(errwin, n);

	xwarn(errwin, "getsockopt");
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
http_write(WINDOW *errwin, struct node *n)
{
	ssize_t	 ssz;

	assert(-1 != n->xfer.pfd->fd);
	assert(NULL != n->xfer.wbuf);
	assert(n->xfer.wbufsz > 0);

	if ((POLLNVAL & n->xfer.pfd->revents) ||
	    (POLLERR & n->xfer.pfd->revents)) {
		xwarn(errwin, "poll errors: %s: %s", n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return 0;
	} else if (POLLHUP & n->xfer.pfd->revents)
		return http_close_err(errwin, n);
	
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
			xwarnx(errwin, "tls_write: %s: %s: %s", 
				n->host, 
				n->addrs.addrs[n->addrs.curaddr].ip,
				tls_error(n->xfer.tls));
			return 0;
		}
	} else {
		ssz = write(n->xfer.pfd->fd, n->xfer.wbuf + 
			n->xfer.wbufpos, n->xfer.wbufsz);
		if (ssz < 0) {
			xwarn(errwin, "write: %s: %s", n->host, 
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
http_read(WINDOW *errwin, struct node *n)
{
	ssize_t	 ssz;
	char	 buf[1024 * 5];
	void	*pp;

	assert(STATE_READ == n->state);
	assert(-1 != n->xfer.pfd->fd);

	/* Check for poll(2) errors and readability. */

	if ((POLLNVAL & n->xfer.pfd->revents) ||
	    (POLLERR & n->xfer.pfd->revents)) {
		xwarnx(errwin, "poll errors: %s: %s", n->host, 
			n->addrs.addrs[n->addrs.curaddr].ip);
		return 0;
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
			xwarnx(errwin, "tls_read: %s: %s: %s", 
				n->host, 
				n->addrs.addrs[n->addrs.curaddr].ip,
				tls_error(n->xfer.tls));
			return 0;
		}
	} else {
		ssz = read(n->xfer.pfd->fd, buf, sizeof(buf));
		if (ssz < 0) {
			xwarn(errwin, "read: %s: %s", n->host, 
				n->addrs.addrs[n->addrs.curaddr].ip);
			return 0;
		}
	}

	if (0 == ssz)
		return http_close_done(errwin, n);

	/* Copy static into dynamic buffer. */

	pp = realloc(n->xfer.rbuf, n->xfer.rbufsz + ssz);
	if (NULL == pp) {
		xwarn(errwin, NULL);
		return 0;
	}
	n->xfer.rbuf = pp;
	memcpy(n->xfer.rbuf + n->xfer.rbufsz, buf, ssz);
	n->xfer.rbufsz += ssz;
	return 1;
}

