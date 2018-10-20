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

#include <kcgi.h>
#include <kcgijson.h>

#include "extern.h"
#include "slant.h"
#include "json.h"

static	volatile sig_atomic_t sigged;

static void
dosig(int code)
{

	sigged = 1;
}

void
recset_free(struct recset *r)
{

	if (NULL == r)
		return;

	free(r->version);
	jsmn_system_clear(&r->system);
	jsmn_record_free_array(r->byqmin, r->byqminsz);
	jsmn_record_free_array(r->bymin, r->byminsz);
	jsmn_record_free_array(r->byhour, r->byhoursz);
	jsmn_record_free_array(r->byday, r->bydaysz);
	jsmn_record_free_array(r->byweek, r->byweeksz);
	jsmn_record_free_array(r->byyear, r->byyearsz);
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
		free(n[i].host);
		free(n[i].xfer.wbuf);
		free(n[i].xfer.rbuf);
		recset_free(n[i].recs);
		free(n[i].recs);
	}

	free(n);
}

/*
 * Run a single step of the state machine.
 * For each node, we examine the current state (n->state) and perform
 * some task based upon that.
 * Return <0 on some sort of error condition or the number of nodes that
 * have changed, which means some sort of window update event should
 * occur to reflect the change.
 */
static int
nodes_update(struct out *out, struct node *n, size_t sz, time_t t)
{
	size_t	 i;
	int	 dirty = 0;

	for (i = 0; i < sz; i++) {
		switch (n[i].state) {
		case STATE_CONNECT_WAITING:
			if (n[i].waitstart + n[i].waittime >= t) 
				break;
			n[i].state = STATE_CONNECT_READY;
			n[i].dirty = 1;
			break;
		case STATE_CONNECT_READY:
			if ( ! http_init_connect(out, &n[i], t))
				return -1;
			break;
		case STATE_CONNECT:
			if ( ! http_connect(out, &n[i], t))
				return -1;
			break;
		case STATE_WRITE:
			if ( ! http_write(out, &n[i], t))
				return -1;
			break;
		case STATE_CLOSE_ERR:
			if ( ! http_close_err(out, &n[i], t))
				return -1;
			break;
		case STATE_CLOSE_DONE:
			if ( ! http_close_done(out, &n[i], t))
				return -1;
			break;
		case STATE_READ:
			if ( ! http_read(out, &n[i], t))
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

/*
 * Sort comparator for memory usage.
 * Needs to be run once per iteration.
 */
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

/*
 * Sort comparator for CPU time.
 * Needs to be run once per iteration.
 */
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

/*
 * Sort comparator for hostnames.
 * Needs to only be run once for the list.
 */
static int
cmp_host(const void *p1, const void *p2)
{
	const struct node *n1 = p1, *n2 = p2;

	return strcmp(n1->host, n2->host);
}

/*
 * Common leading material for all logging messages.
 */
static void
xloghead(struct out *out)
{
	char	 	 buf[32];
	struct tm	*tm;
	time_t		 t = time(NULL);

	assert(NULL != out->errwin);
	tm = localtime(&t);
	strftime(buf, sizeof(buf), "%F %T", tm);
	waddstr(out->errwin, buf);
	fprintf(out->errs, "%s: ", buf);
	wprintw(out->errwin, " %lc ", L'\x2502');
}

/*
 * Emit warning message with errno.
 */
void
xwarn(struct out *out, const char *fmt, ...)
{
	va_list  ap;
	int	 er = errno;

	if (NULL != out->errwin) {
		xloghead(out);
		if (NULL != fmt) {
			va_start(ap, fmt);
			vwprintw(out->errwin, fmt, ap);
			va_end(ap);
		}
		wprintw(out->errwin, "%s%s\n", 
			NULL == fmt ? "" : ": ", strerror(er));
		wrefresh(out->errwin);
	}

	if (NULL != fmt) {
		va_start(ap, fmt);
		vfprintf(out->errs, fmt, ap);
		va_end(ap);
	}
	fprintf(out->errs, "%s%s\n", 
		NULL == fmt ? "" : ": ", strerror(er));
	fflush(out->errs);
}

/*
 * Emit warning message.
 */
void
xwarnx(struct out *out, const char *fmt, ...)
{
	va_list ap;

	if (NULL != out->errwin) {
		xloghead(out);
		wattron(out->errwin, A_BOLD);
		waddstr(out->errwin, "Warning");
		wattroff(out->errwin, A_BOLD);
		waddstr(out->errwin, ": ");
		if (NULL != fmt) {
			va_start(ap, fmt);
			vwprintw(out->errwin, fmt, ap);
			va_end(ap);
		}
		waddch(out->errwin, '\n');
		wrefresh(out->errwin);
	}

	fprintf(out->errs, "Warning: ");
	va_start(ap, fmt);
	vfprintf(out->errs, fmt, ap);
	va_end(ap);
	fputc('\n', out->errs);
	fflush(out->errs);
}

/*
 * Emit debugging message if "out->debug" has been set.
 */
void
xdbg(struct out *out, const char *fmt, ...)
{
	va_list ap;

	if ( ! out->debug) 
		return;

	if (NULL != out->errwin) {
		xloghead(out);
		if (NULL != fmt) {
			va_start(ap, fmt);
			vwprintw(out->errwin, fmt, ap);
			va_end(ap);
		}
		waddch(out->errwin, '\n');
		wrefresh(out->errwin);
	}

	if (NULL != fmt) {
		va_start(ap, fmt);
		vfprintf(out->errs, fmt, ap);
		va_end(ap);
	}
	fputc('\n', out->errs);
	fflush(out->errs);
}

/*
 * Construct a default layout depending on "maxx", the maximum number of
 * available columns, "maxy" for rows, and "n" nodes of length "nsz".
 * Put the results in "d".
 * This copies from the configuration, if applicable, or sets defaults.
 * Return zero on failure (fatal), non-zero on success.
 */
static int
layout(struct config *cfg, struct out *out, size_t maxx, 
	size_t maxy, const struct node *n, size_t nsz, struct draw *d)
{
	size_t	 i;

	if (NULL != cfg->draw) {
		d->header = cfg->draw->header;
		d->errlog = cfg->draw->errlog;
		if (d->errlog >= maxy - d->header) 
			return 0;
	} else {
		d->header = 1;
		if (maxy > 60)
			d->errlog = 10;
		else if (maxy > 40)
			d->errlog = 5;
	}

#define	ORD_CPU 	0 /* Default order... */
#define	ORD_MEM		1
#define	ORD_PROCS	2
#define	ORD_NET		3
#define	ORD_DISC	4
#define	ORD_LINK	5
#define	ORD_RPROCS	6
#define	ORD_HOST	7

	if (NULL != cfg->draw && cfg->draw->boxsz) {
		d->boxsz = cfg->draw->boxsz;
		d->box = calloc(d->boxsz, sizeof(struct drawbox));
		if (NULL == d->box)
			return -1;
		for (i = 0; i < d->boxsz; i++)
			d->box[i] = cfg->draw->box[i];
	} else {
		d->boxsz = 8;
		d->box = calloc(d->boxsz, sizeof(struct drawbox));
		if (NULL == d->box)
			return -1;

		d->box[ORD_CPU].cat = DRAWCAT_CPU;
		d->box[ORD_MEM].cat = DRAWCAT_MEM;
		d->box[ORD_NET].cat = DRAWCAT_NET;
		d->box[ORD_DISC].cat = DRAWCAT_DISC;
		d->box[ORD_PROCS].cat = DRAWCAT_PROCS;
		d->box[ORD_LINK].cat = DRAWCAT_LINK;
		d->box[ORD_RPROCS].cat = DRAWCAT_RPROCS;
		d->box[ORD_HOST].cat = DRAWCAT_HOST;

		d->box[ORD_CPU].args = CPU_QMIN_BARS | 
			CPU_QMIN | CPU_HOUR;
		d->box[ORD_MEM].args = MEM_QMIN_BARS | 
			MEM_QMIN | MEM_HOUR;
		d->box[ORD_NET].args = NET_QMIN | NET_HOUR;
		d->box[ORD_DISC].args = DISC_QMIN | DISC_HOUR;
		d->box[ORD_PROCS].args = PROCS_QMIN_BARS | 
			PROCS_QMIN | PROCS_HOUR;
		d->box[ORD_LINK].args = LINK_IP | 
			LINK_STATE | LINK_ACCESS;
		d->box[ORD_RPROCS].args = RPROCS_QMIN;
		d->box[ORD_HOST].args = HOST_ACCESS;

		if (maxx > compute_width(n, nsz, d)) 
			return 1;

		d->box[ORD_CPU].args &= ~CPU_QMIN_BARS;
		d->box[ORD_MEM].args &= ~MEM_QMIN_BARS;
		d->box[ORD_PROCS].args &= ~PROCS_QMIN_BARS;

		if (maxx > compute_width(n, nsz, d)) 
			return 1;

		d->box[ORD_CPU].args &= ~CPU_HOUR;
		d->box[ORD_MEM].args &= ~MEM_HOUR;
		d->box[ORD_NET].args &= ~NET_HOUR;
		d->box[ORD_DISC].args &= ~DISC_HOUR;
		d->box[ORD_PROCS].args &= ~PROCS_HOUR;

		if (maxx > compute_width(n, nsz, d)) 
			return 1;

		d->box[ORD_LINK].args &= ~(LINK_IP | LINK_STATE);
	}

	return maxx > compute_width(n, nsz, d);
}

int
main(int argc, char *argv[])
{
	int	 	 c, first = 1, maxy, maxx;
	size_t		 i, sz;
	const char	*cfgfile = NULL;
	struct node	*n = NULL;
	struct pollfd	*pfds = NULL;
	struct timespec	 ts;
	sigset_t	 mask, oldmask;
	time_t		 last, now;
	char		*cp;
	struct draw	 d;
	struct config	 cfg;
	struct out	 out;
	
	if (NULL == getenv("HOME")) 
		errx(EXIT_FAILURE, "no HOME directory defined");
	if (NULL == setlocale(LC_ALL, ""))
		err(EXIT_FAILURE, NULL);

	/* Initial pledge. */

	if (-1 == pledge("cpath wpath tty rpath dns inet stdio", NULL))
		err(EXIT_FAILURE, NULL);

	memset(&out, 0, sizeof(struct out));
	memset(&d, 0, sizeof(struct draw));
	memset(&cfg, 0, sizeof(struct config));

	out.debug = 1;

	/*
	 * Open $HOME/.slant-errlog to catch any errors.
	 * This way our error buffer in the window is saved.
	 * Do this with the "cpath wpath" cause we might create the file
	 * in doing so.
	 */

	c = asprintf(&cp, "%s/.slant-errlog", getenv("HOME"));
	if (c < 0)
		err(EXIT_FAILURE, NULL);
	if (NULL == (out.errs = fopen(cp, "a")))
		err(EXIT_FAILURE, "%s", cp);
	free(cp);

	/* Repledge by dropping the error log pledges. */

	if (-1 == pledge("tty rpath dns inet stdio", NULL))
		err(EXIT_FAILURE, NULL);

	/* Start up TLS handling really early. */

	if (tls_init() < 0)
		err(EXIT_FAILURE, NULL);

	/* 
	 * Establish our signal handling: have TERM, QUIT, and INT
	 * interrupt the poll and cause us to exit.
	 * Otherwise, we block the signal.
	 * TODO: handle SINGWINCH.
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

	/* Parse arguments. */

	while (-1 != (c = getopt(argc, argv, "f:o:w:"))) 
		switch (c) {
		case 'f':
			cfgfile = strdup(optarg);
			break;
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
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	/*
	 * Parse our configuration file.
	 * This will tell us all we need to know about our runtime.
	 * Use "cp" as a temporary variable in case we need to create
	 * the configuration file name on-demand.
	 */

	cp = NULL;
	if (NULL == cfgfile) {
		c = asprintf(&cp, "%s/.slantrc", getenv("HOME"));
		if (c < 0)
			err(EXIT_FAILURE, NULL);
		cfgfile = cp;
	} 

	c = config_parse(cfgfile, &cfg, argc, argv);
	free(cp);
	if ( ! c)
		return EXIT_FAILURE;

	/* 
	 * Initialise data.
	 * On any failure---which in this block will be memory
	 * failure---just exit the program immediately.
	 * We don't really have any state to clean up at this point.
	 */
	
	if (0 == cfg.urlsz)
		errx(EXIT_FAILURE, "no urls given");

	n = calloc(cfg.urlsz, sizeof(struct node));
	if (NULL == n)
		err(EXIT_FAILURE, NULL);

	pfds = calloc(cfg.urlsz, sizeof(struct pollfd));
	if (NULL == pfds)
		err(EXIT_FAILURE, NULL);

	for (i = 0; i < cfg.urlsz; i++) {
		pfds[i].fd = -1;
		n[i].xfer.pfd = &pfds[i];
		n[i].state = STATE_STARTUP;
		n[i].url = cfg.urls[i].url;
		if (cfg.urls[i].waittime)
			n[i].waittime = cfg.urls[i].waittime;
		else
			n[i].waittime = cfg.waittime;
		dns_parse_url(&out, &n[i]);
	}

	/* 
	 * All data initialised.
	 * Get our window system ready to roll.
	 * Once we initialise the screen, we're going to need to use our
	 * "errs" and "errwin" to report errors, as stderr will just
	 * munge on the screen.
	 */

	if (NULL == initscr() ||
	    ERR == start_color() ||
	    ERR == cbreak() ||
	    ERR == noecho() ||
	    ERR == nonl())
		exit(EXIT_FAILURE);

	curs_set(0);
	init_pair(1, COLOR_YELLOW, COLOR_BLACK);
	init_pair(2, COLOR_RED, COLOR_BLACK);
	getmaxyx(stdscr, maxy, maxx);

	/* Configure what we see, bailing if it's not possible. */

	c = layout(&cfg, &out, maxx, maxy, n, cfg.urlsz, &d);
	if (c < 0) {
		endwin();
		warn(NULL);
		goto out;
	} else if (0 == c) {
		endwin();
		warnx("insufficient screen dimensions");
		goto out;
	}

	assert((size_t)maxy > d.errlog);
	out.mainwin = subwin(stdscr, maxy - d.errlog, maxx, 0, 0);
	if (d.errlog) {
		out.errwin = subwin(stdscr, 0, maxx, maxy - d.errlog, 0);
		scrollok(out.errwin, 1);
	}

	for (i = 0; i < cfg.urlsz; i++) {
		n[i].state = STATE_RESOLVING;
		if ( ! dns_resolve(&out, n[i].host, &n[i].addrs))
			goto out;
		n[i].state = STATE_CONNECT_READY;
	}

	/* 
	 * FIXME: rpath needed by libressl.
	 * We can relieve this by pre-loading our certs.
	 */

	if (-1 == pledge("tty rpath inet stdio", NULL))
		err(EXIT_FAILURE, NULL);

	/* Main loop. */

	ts.tv_sec = 1;
	ts.tv_nsec = 0;
	last = 0;

	while ( ! sigged) {
		now = time(NULL);
		if ((c = nodes_update(&out, n, cfg.urlsz, now)) < 0)
			break;

		/* Re-sort, if applicable. */

		sz = sizeof(struct node);
		switch (d.order) {
		case DRAWORD_CMDLINE:
			break;
		case DRAWORD_CPU:
			qsort(n, cfg.urlsz, sz, cmp_cpu);
			break;
		case DRAWORD_HOST:
			/* Only do this once. */
			if (0 == first)
				break;
			qsort(n, cfg.urlsz, sz, cmp_host);
			break;
		case DRAWORD_MEM:
			qsort(n, cfg.urlsz, sz, cmp_mem);
			break;
		}

		/*
		 * Update if our data is dirty (c > 0) or if we're on
		 * the first iteration, just to show something.
		 * If we've nothing to show but more than one second has
		 * passed, then simply update the time displays.
		 */

		if ((c || first) && now > last) {
			draw(&out, &d, first, n, cfg.urlsz, now);
			for (i = 0; i < cfg.urlsz; i++) 
				n[i].dirty = 0;
			wrefresh(out.mainwin);
			first = 0;
		} else if (now > last) {
			drawtimes(&out, &d, n, cfg.urlsz, now);
			wrefresh(out.mainwin);
		}

		last = now;
		if (ppoll(pfds, cfg.urlsz, &ts, &oldmask) < 0 && 
		    EINTR != errno) {
			xwarn(&out, "poll");
			break;
		}
	}

out:
	if ( ! isendwin()) {
		if (NULL != out.errwin)
			delwin(out.errwin);
		delwin(out.mainwin);
		endwin();
	}
	nodes_free(n, cfg.urlsz);
	config_free(&cfg);
	free(d.box);
	free(pfds);
	return EXIT_SUCCESS;
usage:
	fprintf(stderr, "usage: %s "
		"[-f conf] "
		"[-o order] "
		"[url...]\n",
		getprogname());
	return EXIT_FAILURE;
}
