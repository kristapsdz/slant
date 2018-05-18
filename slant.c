#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "slant.h"

static void
nodes_free(struct node *n, size_t sz)
{
	size_t	 i;

	for (i = 0; i < sz; i++) {
		if (-1 != n[i].pfd->fd)
			close(n[i].pfd->fd);
		free(n[i].url);
		free(n[i].host);
		free(n[i].wbuf);
		free(n[i].rbuf);
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

	for (i = 0; i < sz; i++)
		switch (n[i].state) {
		case STATE_CONNECT_WAITING:
			if (n[i].waitstart + 15 >= t) 
				break;
			n[i].state = STATE_CONNECT_READY;
			break;
		case STATE_CONNECT_READY:
			if ( ! http_init_connect(&n[i]))
				return 0;
			break;
		case STATE_CONNECT:
			if ( ! http_connect(&n[i]))
				return 0;
			break;
		case STATE_WRITE:
			if ( ! http_write(&n[i]))
				return 0;
			break;
		case STATE_READ:
			if ( ! http_read(&n[i]))
				return 0;
			break;
		default:
			abort();
		}

	return 1;
}

int
main(int argc, char *argv[])
{
	int	 	 c;
	size_t		 i;
	struct node	*nodes = NULL;
	struct pollfd	*pfds = NULL;

	while (-1 != (c = getopt(argc, argv, "")))
		goto usage;

	argc -= optind;
	argv += optind;

	if (0 == argc)
		goto usage;

	nodes = calloc(argc, sizeof(struct node));
	if (NULL == nodes)
		err(EXIT_FAILURE, NULL);

	pfds = calloc(argc, sizeof(struct pollfd));
	if (NULL == pfds)
		err(EXIT_FAILURE, NULL);

	for (i = 0; i < (size_t)argc; i++)
		pfds[i].fd = -1;

	for (i = 0; i < (size_t)argc; i++) {
		nodes[i].pfd = &pfds[i];
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

	for (i = 0; i < (size_t)argc; i++) {
		nodes[i].state = STATE_RESOLVING;
		if ( ! dns_resolve(nodes[i].host, &nodes[i].addrs))
			goto out;
		nodes[i].state = STATE_CONNECT_READY;
	}

	for (;;) {
		if ( ! nodes_update(nodes, argc))
			break;
		if (poll(pfds, argc, 5) < 0)
			break;
	}

out:
	nodes_free(nodes, argc);
	free(pfds);
	return EXIT_SUCCESS;
usage:
	fprintf(stderr, "usage: %s addr...\n", getprogname());
	return EXIT_FAILURE;
}
