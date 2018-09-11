#include <sys/queue.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tls.h>
#include <unistd.h>

#include "extern.h"
#include "slant.h"

static	volatile sig_atomic_t sigged;
static	int debug = 1;

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
			free(n[i].recs->version);
			free(n[i].recs->byqmin);
			free(n[i].recs->bymin);
			free(n[i].recs->byhour);
			free(n[i].recs->byday);
			free(n[i].recs->byweek);
			free(n[i].recs->byyear);
			free(n[i].recs);
		}
	}

	free(n);
}

static int
nodes_update(WINDOW *errwin, time_t waittime, struct node *n, size_t sz)
{
	size_t	 i;
	time_t	 t = time(NULL);
	int	 dirty = 0;

	for (i = 0; i < sz; i++) {
		switch (n[i].state) {
		case STATE_CONNECT_WAITING:
			if (n[i].waitstart + waittime >= t) 
				break;
			n[i].state = STATE_CONNECT_READY;
			n[i].dirty = 1;
			break;
		case STATE_CONNECT_READY:
			if ( ! http_init_connect(errwin, &n[i]))
				return -1;
			break;
		case STATE_CONNECT:
			if ( ! http_connect(errwin, &n[i]))
				return -1;
			break;
		case STATE_WRITE:
			if ( ! http_write(errwin, &n[i]))
				return -1;
			break;
		case STATE_CLOSE_ERR:
			if ( ! http_close_err(errwin, &n[i]))
				return -1;
			break;
		case STATE_CLOSE_DONE:
			if ( ! http_close_done(errwin, &n[i]))
				return -1;
			break;
		case STATE_READ:
			if ( ! http_read(errwin, &n[i]))
				return -1;
			break;
		default:
			abort();
		}

		if (n[i].dirty)
			dirty++;
	}

	return dirty;
}

static int
cmp_mem(const void *p1, const void *p2)
{
	const struct node *n1 = p1, *n2 = p2;

	if (NULL == n1->recs ||
	    0 == n1->recs->byqminsz)
		return 1;
	if (NULL == n2->recs ||
	    0 == n2->recs->byqminsz)
		return -1;
	if (n1->recs->byqmin[0].mem <
	    n2->recs->byqmin[0].mem)
		return 1;
	if (n1->recs->byqmin[0].mem >
	    n2->recs->byqmin[0].mem)
		return -1;
	return 0;
}

static int
cmp_cpu(const void *p1, const void *p2)
{
	const struct node *n1 = p1, *n2 = p2;

	if (NULL == n1->recs ||
	    0 == n1->recs->byqminsz)
		return 1;
	if (NULL == n2->recs ||
	    0 == n2->recs->byqminsz)
		return -1;
	if (n1->recs->byqmin[0].cpu <
	    n2->recs->byqmin[0].cpu)
		return 1;
	if (n1->recs->byqmin[0].cpu >
	    n2->recs->byqmin[0].cpu)
		return -1;
	return 0;
}

static int
cmp_host(const void *p1, const void *p2)
{
	const struct node *n1 = p1, *n2 = p2;

	return strcmp(n1->host, n2->host);
}

static void
xloghead(WINDOW *errwin)
{
	char	 	 buf[32];
	struct tm	*tm;
	time_t		 t = time(NULL);

	tm = localtime(&t);
	strftime(buf, sizeof(buf), "%F %T", tm);
	waddstr(errwin, buf);
	fprintf(stderr, "%s: ", buf);
	wprintw(errwin, " %lc ", L'\x2502');
}

void
xwarn(WINDOW *errwin, const char *fmt, ...)
{
	va_list  ap;
	int	 er = errno;

	xloghead(errwin);
	va_start(ap, fmt);
	vwprintw(errwin, fmt, ap);
	va_end(ap);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	wprintw(errwin, "%s%s\n", 
		NULL == fmt ? "" : ": ", strerror(er));
	fprintf(stderr, "%s%s\n", 
		NULL == fmt ? "" : ": ", strerror(er));
	wrefresh(errwin);
	fflush(stderr);
}

void
xwarnx(WINDOW *errwin, const char *fmt, ...)
{
	va_list ap;

	xloghead(errwin);
	wattron(errwin, A_BOLD);
	waddstr(errwin, "Warning");
	wattroff(errwin, A_BOLD);
	waddstr(errwin, ": ");
	fprintf(stderr, "Warning: ");
	va_start(ap, fmt);
	vwprintw(errwin, fmt, ap);
	va_end(ap);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	waddch(errwin, '\n');
	fputc('\n', stderr);
	wrefresh(errwin);
	fflush(stderr);
}

void
xdbg(WINDOW *errwin, const char *fmt, ...)
{
	va_list ap;

	if ( ! debug) 
		return;
	xloghead(errwin);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	va_start(ap, fmt);
	vwprintw(errwin, fmt, ap);
	va_end(ap);
	waddch(errwin, '\n');
	fputc('\n', stderr);
	wrefresh(errwin);
	fflush(stderr);
}

static int
layout(size_t maxx, const struct node *n, size_t nsz, struct draw *d)
{

	d->box_cpu = CPU_QMIN_BARS | CPU_QMIN | CPU_HOUR;
	d->box_mem = MEM_QMIN_BARS | MEM_QMIN | MEM_HOUR;
	d->box_net = NET_QMIN | NET_HOUR;
	d->box_disc = DISC_QMIN | DISC_HOUR;
	d->box_procs = PROCS_QMIN_BARS | PROCS_QMIN | PROCS_HOUR;
	d->box_link = LINK_IP | LINK_STATE | LINK_ACCESS;
	d->box_host = HOST_ACCESS;

	if (maxx > compute_width(n, nsz, d)) 
		return 1;

	d->box_cpu &= ~CPU_QMIN_BARS;
	d->box_mem &= ~MEM_QMIN_BARS;
	d->box_procs &= ~PROCS_QMIN_BARS;

	if (maxx > compute_width(n, nsz, d)) 
		return 1;

	d->box_cpu &= ~CPU_HOUR;
	d->box_mem &= ~MEM_HOUR;
	d->box_net &= ~NET_HOUR;
	d->box_disc &= ~DISC_HOUR;
	d->box_procs &= ~PROCS_HOUR;

	if (maxx > compute_width(n, nsz, d)) 
		return 1;

	d->box_link &= ~(LINK_IP | LINK_STATE);

	if (maxx > compute_width(n, nsz, d)) 
		return 1;

	warnx("screen too narrow");
	return 0;
}

int
main(int argc, char *argv[])
{
	int	 	 c, first = 1, maxy, maxx;
	size_t		 i, nsz;
	const char	*er;
	struct node	*n = NULL;
	struct pollfd	*pfds = NULL;
	struct timespec	 ts;
	struct draw	 d;
	sigset_t	 mask, oldmask;
	time_t		 last, now, waittime = 60;
	WINDOW		*errwin = NULL, *mainwin = NULL;
	char		*cp;

	if (-1 == pledge("cpath wpath tty rpath dns inet stdio", NULL))
		err(EXIT_FAILURE, NULL);

	/*
	 * Open $HOME/.slant-errlog to catch any errors.
	 * This is because our JSON has a tendency to fail and I'm not
	 * sure why.
	 * So we can capture the entire JSON buffer for post-analysis.
	 */

	if (NULL != getenv("HOME")) {
		c = asprintf(&cp, 
			"%s/.slant-errlog", getenv("HOME"));
		if (c < 0)
			err(EXIT_FAILURE, NULL);
		freopen(cp, "a", stderr);
		free(cp);
	}

	if (-1 == pledge("tty rpath dns inet stdio", NULL))
		err(EXIT_FAILURE, NULL);

	memset(&d, 0, sizeof(struct draw));

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

	while (-1 != (c = getopt(argc, argv, "o:w:"))) 
		switch (c) {
		case 'o':
			if (0 == strcmp(optarg, "host"))
				d.order = DRAWORD_HOST;
			else if (0 == strcmp(optarg, "cmdline"))
				d.order = DRAWORD_CMDLINE;
			else if (0 == strcmp(optarg, "cpu"))
				d.order = DRAWORD_CPU;
			else if (0 == strcmp(optarg, "mem"))
				d.order = DRAWORD_MEM;
			else
				goto usage;
			break;
		case 'w':
			waittime = strtonum(optarg, 15, INT_MAX, &er);
			if (NULL != er)
				errx(EXIT_FAILURE, "-w: %s", er);
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	if (0 == argc)
		goto usage;
	
	/* Initialise data. */

	nsz = argc;
	n = calloc(nsz, sizeof(struct node));
	if (NULL == n)
		err(EXIT_FAILURE, NULL);

	pfds = calloc(nsz, sizeof(struct pollfd));
	if (NULL == pfds)
		err(EXIT_FAILURE, NULL);

	for (i = 0; i < nsz; i++)
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

	curs_set(0);

	init_pair(1, COLOR_YELLOW, COLOR_BLACK);
	init_pair(2, COLOR_RED, COLOR_BLACK);

	/* FIXME: handle resize events. */

	getmaxyx(stdscr, maxy, maxx);

	mainwin = subwin(stdscr, maxy - 10, maxx, 0, 0);
	errwin = subwin(stdscr, 0, maxx, maxy - 10, 0);
	scrollok(errwin, 1);

	/*
	 * All terminal windows initialised.
	 * From here on out, we want to use ncurses for reporting
	 * whatever we can.
	 */

	for (i = 0; i < nsz; i++) {
		n[i].xfer.pfd = &pfds[i];
		n[i].state = STATE_STARTUP;
		n[i].url = strdup(argv[i]);

		if ( ! dns_parse_url(errwin, &n[i]))
			goto out;
		if (NULL == n[i].url ||
		    NULL == n[i].path ||
		    NULL == n[i].host) {
			warn(NULL);
			goto out;
		}
	}

	/*
	 * Initialise DNS stuff.
	 * TODO: put DNS init into the main loop.
	 */

	for (i = 0; i < nsz; i++) {
		n[i].state = STATE_RESOLVING;
		if ( ! dns_resolve(errwin, n[i].host, &n[i].addrs))
			goto out;
		n[i].state = STATE_CONNECT_READY;
	}

	/* 
	 * FIXME: rpath needed by libressl.
	 * We can relieve this by pre-loading our certs.
	 */

	if (-1 == pledge("tty rpath inet stdio", NULL))
		err(EXIT_FAILURE, NULL);

	/* Configure what we see. */

	if ( ! layout(maxx, n, nsz, &d))
		goto out;

	/*
	 * Main loop: continue trying to pull down data from all of the
	 * addresses we have on file.
	 */

	ts.tv_sec = 1;
	ts.tv_nsec = 0;
	last = 0;

	while ( ! sigged) {
		if ((c = nodes_update(errwin, waittime, n, nsz)) < 0)
			break;

		switch (d.order) {
		case DRAWORD_CMDLINE:
			break;
		case DRAWORD_CPU:
			qsort(n, nsz, sizeof(struct node), cmp_cpu);
			break;
		case DRAWORD_HOST:
			/* Only do this once. */
			if (0 == first)
				break;
			qsort(n, nsz, sizeof(struct node), cmp_host);
			break;
		case DRAWORD_MEM:
			qsort(n, nsz, sizeof(struct node), cmp_mem);
			break;
		}

		now = time(NULL);

		/*
		 * Update if our data is dirty (c > 0) or if we're on
		 * the first iteration, just to show something.
		 * If we've nothing to show but more than one second has
		 * passed, then simply update the time displays.
		 */

		if ((c || first) && now > last) {
			draw(mainwin, &d, waittime, n, nsz, now);
			for (i = 0; i < nsz; i++) 
				n[i].dirty = 0;
			wrefresh(mainwin);
			first = 0;
		} else if (now > last) {
			drawtimes(mainwin, &d, waittime, n, nsz, now);
			wrefresh(mainwin);
		}

		last = now;

		if (ppoll(pfds, nsz, &ts, &oldmask) < 0 && 
		    EINTR != errno) {
			warn("poll");
			break;
		}
	}

out:
	delwin(errwin);
	delwin(mainwin);
	endwin();
	nodes_free(n, nsz);
	free(pfds);
	return EXIT_SUCCESS;
usage:
	fprintf(stderr, "usage: %s "
		"[-o order] "
		"[-w waittime] "
		"addr...\n", getprogname());
	return EXIT_FAILURE;
}
