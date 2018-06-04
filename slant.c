#include <sys/queue.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tls.h>
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
		if (NULL != n[i].xfer.tls)
			tls_close(n[i].xfer.tls);
		if (-1 != n[i].xfer.pfd->fd)
			close(n[i].xfer.pfd->fd);
		tls_free(n[i].xfer.tls);
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
			n[i].dirty = 1;
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
		case STATE_CLOSE_ERR:
			if ( ! http_close_err(&n[i]))
				return -1;
			break;
		case STATE_CLOSE_DONE:
			if ( ! http_close_done(&n[i]))
				return -1;
			break;
		case STATE_READ:
			if ( ! http_read(&n[i]))
				return -1;
			break;
		default:
			abort();
		}

		if (n[i].dirty) {
			dirty++;
			n[i].dirty = 0;
		}
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
	struct draw	 d;
	sigset_t	 mask, oldmask;
	time_t		 last, now;

	if (-1 == pledge("tty rpath dns inet stdio", NULL))
		err(EXIT_FAILURE, NULL);

	/* Start up TLS handling really early. */

	if (tls_init() < 0)
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

	/* 
	 * All data initialised.
	 * Get our window system ready to roll.
	 */

	if (NULL == setlocale(LC_ALL, ""))
		err(EXIT_FAILURE, NULL);

	if (NULL == initscr() ||
	    ERR == start_color() ||
	    ERR == cbreak() ||
	    ERR == noecho() ||
	    ERR == nonl())
		exit(EXIT_FAILURE);

	memset(&d, 0, sizeof(struct draw));
	curs_set(0);

	init_pair(1, COLOR_YELLOW, COLOR_BLACK);
	init_pair(2, COLOR_RED, COLOR_BLACK);

	/*
	 * All terminal windows initialised.
	 * From here on out, we want to use ncurses for reporting
	 * whatever we can.
	 */

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
	 * Initialise DNS stuff.
	 * TODO: put DNS init into the main loop.
	 */

	for (i = 0; i < (size_t)argc; i++) {
		nodes[i].state = STATE_RESOLVING;
		if ( ! dns_resolve(nodes[i].host, &nodes[i].addrs))
			goto out;
		nodes[i].state = STATE_CONNECT_READY;
	}

	/* 
	 * FIXME: rpath needed by libressl.
	 * We can relieve this by pre-loading our certs.
	 */

	if (-1 == pledge("tty rpath inet stdio", NULL))
		err(EXIT_FAILURE, NULL);

	/*
	 * Main loop: continue trying to pull down data from all of the
	 * addresses we have on file.
	 */

	ts.tv_sec = 1;
	ts.tv_nsec = 0;
	last = 0;

	while ( ! sigged) {
		if ((c = nodes_update(nodes, argc)) < 0)
			break;

		now = time(NULL);

		/*
		 * Update if our data is dirty (c > 0) or if we're on
		 * the first iteration, just to show something.
		 * If we've nothing to show but more than one second has
		 * passed, then simply update the time displays.
		 * FIXME: we should only do this once a second at most,
		 * so multiple updates in <1 second granularity doesn't
		 * cause too many updates.
		 */

		if (c || first) {
			draw(stdscr, &d, nodes, argc, now);
			refresh();
			first = 0;
		} else if (now > last) {
			drawtimes(stdscr, &d, nodes, argc, now);
			refresh();
		}

		last = now;

		if (ppoll(pfds, argc, &ts, &oldmask) < 0 && 
		    EINTR != errno) {
			warn("poll");
			break;
		}
	}

out:
	endwin();
	nodes_free(nodes, argc);
	free(pfds);
	return EXIT_SUCCESS;
usage:
	fprintf(stderr, "usage: %s addr...\n", getprogname());
	return EXIT_FAILURE;
}
