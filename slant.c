#include <sys/queue.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "extern.h"
#include "slant.h"

static	volatile sig_atomic_t sigged;

static void
dosig(int code)
{

	sigged = 1;
}

static void
nodes_free(struct node *n, size_t sz)
{
	size_t	 i;

	for (i = 0; i < sz; i++) {
		if (-1 != n[i].xfer.pfd->fd)
			close(n[i].xfer.pfd->fd);
		free(n[i].url);
		free(n[i].host);
		free(n[i].xfer.wbuf);
		free(n[i].xfer.rbuf);
		if (NULL != n[i].recs) {
			free(n[i].recs->byqmin);
			free(n[i].recs->bymin);
			free(n[i].recs->byhour);
			free(n[i].recs);
		}
	}

	free(n);
}

static int
nodes_update(struct node *n, size_t sz)
{
	size_t	 i;
	time_t	 t = time(NULL);
	int	 dirty = 0;

	for (i = 0; i < sz; i++) {
		switch (n[i].state) {
		case STATE_CONNECT_WAITING:
			if (n[i].waitstart + 15 >= t) 
				break;
			n[i].state = STATE_CONNECT_READY;
			break;
		case STATE_CONNECT_READY:
			if ( ! http_init_connect(&n[i]))
				return -1;
			break;
		case STATE_CONNECT:
			if ( ! http_connect(&n[i]))
				return -1;
			break;
		case STATE_WRITE:
			if ( ! http_write(&n[i]))
				return -1;
			break;
		case STATE_READ:
			if ( ! http_read(&n[i]))
				return -1;
			break;
		default:
			abort();
		}
		dirty += n[i].dirty;
		n[i].dirty = 0;
	}

	return dirty;
}

int
main(int argc, char *argv[])
{
	int	 	 c, first = 1;
	size_t		 i;
	struct node	*nodes = NULL;
	struct pollfd	*pfds = NULL;
	struct timespec	 ts;
	sigset_t	 mask, oldmask;

	if (-1 == pledge("dns inet stdio", NULL))
		err(EXIT_FAILURE, NULL);

	/* 
	 * Establish our signal handling: have TERM, QUIT, and INT
	 * interrupt the poll and cause us to exit.
	 * Otherwise, we block the signal.
	 */

	if (sigemptyset(&mask) < 0)
		err(EXIT_FAILURE, NULL);
	if (SIG_ERR == signal(SIGTERM, dosig))
		err(EXIT_FAILURE, NULL);
	if (SIG_ERR == signal(SIGQUIT, dosig))
		err(EXIT_FAILURE, NULL);
	if (SIG_ERR == signal(SIGINT, dosig))
		err(EXIT_FAILURE, NULL);
	if (sigaddset(&mask, SIGTERM) < 0)
		err(EXIT_FAILURE, NULL);
	if (sigaddset(&mask, SIGQUIT) < 0)
		err(EXIT_FAILURE, NULL);
	if (sigaddset(&mask, SIGINT) < 0)
		err(EXIT_FAILURE, NULL);
	if (sigprocmask(SIG_BLOCK, &mask, &oldmask) < 0)
		err(EXIT_FAILURE, NULL);

	while (-1 != (c = getopt(argc, argv, "")))
		goto usage;

	argc -= optind;
	argv += optind;

	if (0 == argc)
		goto usage;
	
	/* Initialise data. */

	nodes = calloc(argc, sizeof(struct node));
	if (NULL == nodes)
		err(EXIT_FAILURE, NULL);

	pfds = calloc(argc, sizeof(struct pollfd));
	if (NULL == pfds)
		err(EXIT_FAILURE, NULL);

	for (i = 0; i < (size_t)argc; i++)
		pfds[i].fd = -1;

	for (i = 0; i < (size_t)argc; i++) {
		nodes[i].xfer.pfd = &pfds[i];
		nodes[i].state = STATE_STARTUP;
		nodes[i].url = strdup(argv[i]);

		if ( ! dns_parse_url(&nodes[i]))
			goto out;
		if (NULL == nodes[i].url ||
		    NULL == nodes[i].path ||
		    NULL == nodes[i].host) {
			warn(NULL);
			goto out;
		}
	}

	/* 
	 * All data initialised.
	 * We now want to do our DNS resolutions.
	 * TODO: put this into the main loop.
	 */

	for (i = 0; i < (size_t)argc; i++) {
		nodes[i].state = STATE_RESOLVING;
		if ( ! dns_resolve(nodes[i].host, &nodes[i].addrs))
			goto out;
		nodes[i].state = STATE_CONNECT_READY;
	}

	if (-1 == pledge("inet stdio", NULL))
		err(EXIT_FAILURE, NULL);

	/*
	 * Main loop: continue trying to pull down data from all of the
	 * addresses we have on file.
	 */

	ts.tv_sec = 1;
	ts.tv_nsec = 0;

	while ( ! sigged) {
		if ((c = nodes_update(nodes, argc)) < 0)
			break;

		if (c || first) {
			draw(nodes, argc);
			first = 0;
		}

		if (ppoll(pfds, argc, &ts, &oldmask) < 0 && 
		    EINTR != errno) {
			warn("poll");
			break;
		}
	}
out:
	nodes_free(nodes, argc);
	free(pfds);
	return EXIT_SUCCESS;
usage:
	fprintf(stderr, "usage: %s addr...\n", getprogname());
	return EXIT_FAILURE;
}
