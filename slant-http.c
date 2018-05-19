#include <sys/queue.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "extern.h"
#include "slant.h"

static int
http_err_connect(struct node *n)
{

	close(n->pfd->fd);
	n->pfd->fd = -1;
	n->curaddr = (n->curaddr + 1) % n->addrs.addrsz;
	n->state = STATE_CONNECT_WAITING;
	n->waitstart = time(NULL);
	return 1;
}

static int
http_done(struct node *n)
{
	char	*end, *sv, *start = n->rbuf;
	size_t	 len, sz = n->rbufsz;
	int	 rc, httpok = 0;

	close(n->pfd->fd);
	n->pfd->fd = -1;
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
			warnx("%s: bad response: %.3s", n->host, sv);
			return 1;
		}
	}

	if ( ! httpok) {
		warnx("%s: no HTTP response code", n->host);
		rc = 1;
	} else
		rc = jsonobj_parse(n, start, sz);

	free(n->rbuf);
	n->rbuf = NULL;
	n->rbufsz = 0;
	return rc;
}

/*
 * Prepare the write buffer. 
 * Return zero on failure, non-zero on success.
 */
static int
http_write_ready(struct node *n)
{
	int	 c;

	warnx("%s: ready for writes", n->host);

	n->wbufsz = n->wbufpos = 0;
	free(n->wbuf);
	n->wbuf = NULL;

	c = asprintf(&n->wbuf,
		"GET %s HTTP/1.0\r\n"
		"Host: %s\r\n"
		"\r\n",
		n->path, n->host);

	if (c < 0) {
		warn(NULL);
		return 0;
	}

	n->wbufsz = c;
	n->state = STATE_WRITE;
	n->pfd->events = POLLOUT;
	return 1;
}

/*
 * Initialise a socket descriptor to the current endpoint.
 * Moves into STATE_CONNECT for async connection, STATE_WRITE on
 * success, or calls http_err_connect() on connection failure.
 * Returns zero on system failure, non-zero on success.
 */
int
http_init_connect(struct node *n)
{
	int		 family, c, flags;

	warnx("%s: setting up: %s", n->host, 
		n->addrs.addrs[n->curaddr].ip);

	memset(&n->ss, 0, sizeof(struct sockaddr_storage));

	if (4 == n->addrs.addrs[n->curaddr].family) {
		family = PF_INET;
		((struct sockaddr_in *)&n->ss)->sin_family = AF_INET;
		((struct sockaddr_in *)&n->ss)->sin_port = 
			htons(n->addrs.addrs[n->curaddr].port);
		c = inet_pton(AF_INET, 
			n->addrs.addrs[n->curaddr].ip,
			&((struct sockaddr_in *)&n->ss)->sin_addr);
		n->sslen = sizeof(struct sockaddr_in);
	} else {
		family = PF_INET6;
		((struct sockaddr_in6 *)&n->ss)->sin6_family = AF_INET6;
		((struct sockaddr_in6 *)&n->ss)->sin6_port =
			htons(n->addrs.addrs[n->curaddr].port);
		c = inet_pton(AF_INET6, 
			n->addrs.addrs[n->curaddr].ip,
			&((struct sockaddr_in6 *)&n->ss)->sin6_addr);
		n->sslen = sizeof(struct sockaddr_in6);
	} 

	if (c < 0) {
		warn("%s: cannot convert: %s", 
			n->host, n->addrs.addrs[n->curaddr].ip);
		return 0;
	} else if (0 == c) {
		warnx("%s: cannot convert: %s", 
			n->host, n->addrs.addrs[n->curaddr].ip);
		return 0;
	}

	n->pfd->events = POLLOUT;
	n->pfd->fd = socket(family, SOCK_STREAM, 0);

	if (-1 == n->pfd->fd) {
		warn("socket");
		return 0;
	}

	/* Set up non-blocking mode. */

	if (-1 == (flags = fcntl(n->pfd->fd, F_GETFL, 0))) {
		warn("fcntl");
		return 0;
	}
	if (-1 == fcntl(n->pfd->fd, F_SETFL, flags | O_NONBLOCK)) {
		warn("fcntl");
		return 0;
	}

	warnx("%s: ready: %s", n->host, 
		n->addrs.addrs[n->curaddr].ip);
	n->state = STATE_CONNECT;

	/* This is from connect(2): asynchronous connection. */

	c = connect(n->pfd->fd, (struct sockaddr *)&n->ss, n->sslen);
	if (c && (EINTR == errno || EINPROGRESS == errno)) {
		warnx("%s: asynchronous connect: %s", n->host, 
			n->addrs.addrs[n->curaddr].ip);
		return 1;
	} else if (c == 0)
		return http_write_ready(n);

	if (ETIMEDOUT == errno ||
	    ECONNREFUSED == errno ||
	    EHOSTUNREACH == errno ||
	    ENETUNREACH == errno) {
		warnx("%s: address not reached: %s", 
			n->host, n->addrs.addrs[n->curaddr].ip);
		return http_err_connect(n);
	}

	warn("%s: connect: %s", n->host, 
		n->addrs.addrs[n->curaddr].ip);
	return 0;
}

/*
 * Check for asynchronous connect(2).
 * Returns zero on system failure, non-zero on success.
 * Calls http_err_connect() if connection fails or transitions to
 * STATE_WRITE on success.
 */
int
http_connect(struct node *n)
{
	int	  c;
	int 	  error = 0;
	socklen_t len = sizeof(error);

	assert(-1 != n->pfd->fd);

	if ((POLLNVAL & n->pfd->revents) ||
	    (POLLERR & n->pfd->revents)) {
		warn("%s: poll: %s", n->host, 
			n->addrs.addrs[n->curaddr].ip);
		return 0;
	} else if (POLLHUP & n->pfd->revents) {
		warnx("%s: hangup: %s", n->host, 
			n->addrs.addrs[n->curaddr].ip);
		return http_err_connect(n);
	} else if ( ! (POLLOUT & n->pfd->revents))
		return 1;

	c = getsockopt(n->pfd->fd, 
		SOL_SOCKET, SO_ERROR, &error, &len);

	if (c < 0) {
		warn(NULL);
		return 0;
	} else if (0 == error) 
		return http_write_ready(n);

	errno = error;
	if (ETIMEDOUT == errno ||
	    ECONNREFUSED == errno ||
	    EHOSTUNREACH == errno ||
	    ENETUNREACH == errno) {
		warnx("%s: address not reached: %s", 
			n->host, n->addrs.addrs[n->curaddr].ip);
		return http_err_connect(n);
	}
	warn(NULL);
	return 0;
}

/*
 * Write to the file descriptor.
 * Returns zero on system failure, non-zero on success.
 * When the request has been written, update state to be
 * STATE_READ.
 * On connect/write requests, calls http_err_connect() for further
 * changing of state.
 */
int
http_write(struct node *n)
{
	ssize_t	 ssz;

	assert(-1 != n->pfd->fd);
	assert(NULL != n->wbuf);
	assert(n->wbufsz > 0);

	if ((POLLNVAL & n->pfd->revents) ||
	    (POLLERR & n->pfd->revents)) {
		warn("%s: poll errors: %s", n->host, 
			n->addrs.addrs[n->curaddr].ip);
		return 0;
	} else if (POLLHUP & n->pfd->revents) {
		warnx("%s: hangup: %s", n->host, 
			n->addrs.addrs[n->curaddr].ip);
		return http_err_connect(n);
	} else if ( ! (POLLOUT & n->pfd->revents))
		return 1;

	ssz = write(n->pfd->fd, 
		n->wbuf + n->wbufpos, n->wbufsz);
	if (ssz < 0) {
		warn("%s: write: %s", n->host, 
			n->addrs.addrs[n->curaddr].ip);
		return 0;
	}
	n->wbufsz -= ssz;
	n->wbufpos += ssz;

	if (n->wbufsz > 0)
		return 1;

	warnx("%s: finished writing: %s", n->host, 
		n->addrs.addrs[n->curaddr].ip);

	free(n->wbuf);
	n->wbuf = NULL;
	n->state = STATE_READ;
	free(n->rbuf);
	n->rbuf = NULL;
	n->rbufsz = 0;
	n->pfd->events = POLLIN;
	return 1;
}

/*
 * Read from the file descriptor.
 * Returns zero on system failure, non-zero on success.
 * When the file descriptor has been read, sets state to
 * STATE_CONNECT_WAITING.
 */
int
http_read(struct node *n)
{
	ssize_t	 ssz;
	char	 buf[1024 * 5];
	void	*pp;

	assert(STATE_READ == n->state);
	assert(-1 != n->pfd->fd);

	/* Check for poll(2) errors and readability. */

	if ((POLLNVAL & n->pfd->revents) ||
	    (POLLERR & n->pfd->revents)) {
		warnx("%s: poll errors: %s", n->host, 
			n->addrs.addrs[n->curaddr].ip);
		return 0;
	} else if ( ! (POLLIN & n->pfd->revents))
		return 1;

	/* Read into a static buffer. */

	ssz = read(n->pfd->fd, buf, sizeof(buf));
	if (ssz < 0) {
		warn("%s: read: %s", n->host, 
			n->addrs.addrs[n->curaddr].ip);
		return 0;
	} else if (0 == ssz) {
		warnx("%s: finished reading: %s", n->host, 
			n->addrs.addrs[n->curaddr].ip);
		return http_done(n);
	}

	/* Copy static into dynamic buffer. */

	pp = realloc(n->rbuf, n->rbufsz + ssz);
	if (NULL == pp) {
		warn(NULL);
		return 0;
	}
	n->rbuf = pp;
	memcpy(n->rbuf + n->rbufsz, buf, ssz);
	n->rbufsz += ssz;
	return 1;
}

