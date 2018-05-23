#include <sys/queue.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#include "extern.h"
#include "slant.h"

static time_t
get_last(const struct node *n)
{

	if (NULL == n->recs)
		return 0;

	if (n->recs->byqminsz &&
	    n->recs->byqmin[0].entries)
		return n->recs->byqmin[0].ctime;
	if (n->recs->byminsz &&
	    n->recs->bymin[0].entries)
		return n->recs->bymin[0].ctime;
	if (n->recs->byhoursz &&
	    n->recs->byhour[0].entries)
		return n->recs->byhour[0].ctime;
	if (n->recs->bydaysz &&
	    n->recs->byday[0].entries)
		return n->recs->byday[0].ctime;
	if (n->recs->byweeksz &&
	    n->recs->byweek[0].entries)
		return n->recs->byweek[0].ctime;
	if (n->recs->byyearsz &&
	    n->recs->byyear[0].entries)
		return n->recs->byyear[0].ctime;

	return 0;
}

static void
draw_main_separator(void)
{

	printw("%lc", L'\x2502');
}

static void
draw_sub_separator(void)
{

	printw("%lc", L'\x250a');
}

static void
draw_bars(double vv)
{
	size_t	 i;
	double	 v;

	for (i = 1; i <= 10; i++) {
		v = i * 10.0;
		if (v > vv)
			break;
		if (i >= 8)
			attron(COLOR_PAIR(2));
		else if (i >= 5)
			attron(COLOR_PAIR(1));
		addch('|');
		if (i >= 8)
			attroff(COLOR_PAIR(2));
		else if (i >= 5)
			attroff(COLOR_PAIR(1));
	}
	for ( ; i <= 10; i++)
		addch(' ');
	addch(' ');
}

static void
draw_pct(double vv)
{
	if (vv >= 80.0)
		attron(COLOR_PAIR(2));
	else if (vv >= 50.0)
		attron(COLOR_PAIR(1));
	printw("%5.1f%%", vv);
	if (vv >= 80.0)
		attroff(COLOR_PAIR(2));
	else if (vv >= 50.0)
		attroff(COLOR_PAIR(1));
}

static void
draw_interval(time_t last, time_t now)
{
	time_t	 ospan, span, hr, min;

	if (0 == last) {
		addstr("---:--:--");
		return;
	}

	if ((span = now - last) < 0)
		span = 0;
	
	ospan = span;

	if (ospan >= 120)
		attron(A_BOLD | COLOR_PAIR(2));
	else if (ospan >= 60)
		attron(A_BOLD | COLOR_PAIR(1));

	hr = span / (60 * 60);
	span -= hr * 60 * 60;
	min = span / 60;
	span -= min * 60;
	printw("%3lld:%.2lld:%.2lld", 
		(long long)hr, (long long)min, 
		(long long)span);

	if (ospan >= 120)
		attroff(A_BOLD | COLOR_PAIR(2));
	else if (ospan >= 60)
		attroff(A_BOLD | COLOR_PAIR(1));
}

static void
draw_xfer(double vv, int left)
{
	char	 nbuf[16];

	if (vv >= 1024 * 1024 * 1024)
		snprintf(nbuf, sizeof(nbuf), "%.1fG", 
			vv / (1024 * 1024 * 1024));
	else if (vv >= 1024 * 1024)
		snprintf(nbuf, sizeof(nbuf), 
			"%.1fM", vv / (1024 * 1024));
	else if (vv >= 1024)
		snprintf(nbuf, sizeof(nbuf), "%.1fK", vv / 1024);
	else if (vv < 0.001)
		snprintf(nbuf, sizeof(nbuf), "%gB", 0.0);
	else 
		snprintf(nbuf, sizeof(nbuf), "%.0fB", vv);

	if (left)
		printw("%-6s", nbuf);
	else
		printw("%6s", nbuf);
}

static void
draw_inet(const struct node *n)
{
	double	 vv;

	if (NULL != n->recs &&
	    n->recs->byqminsz &&
	    n->recs->byqmin[0].entries) {
		vv = n->recs->byqmin[0].netrx /
			n->recs->byqmin[0].entries;
		attron(A_BOLD);
		draw_xfer(vv, 0);
		attroff(A_BOLD);
		addch('/');
		vv = n->recs->byqmin[0].nettx /
			n->recs->byqmin[0].entries;
		attron(A_BOLD);
		draw_xfer(vv, 1);
		attroff(A_BOLD);
	} else
		addstr("------/------");

	draw_sub_separator();

	if (NULL != n->recs &&
	    n->recs->byhoursz &&
	    n->recs->byhour[0].entries) {
		vv = n->recs->byhour[0].netrx /
			n->recs->byhour[0].entries;
		attron(A_BOLD);
		draw_xfer(vv, 0);
		attroff(A_BOLD);
		addch('/');
		vv = n->recs->byhour[0].nettx /
			n->recs->byhour[0].entries;
		attron(A_BOLD);
		draw_xfer(vv, 1);
		attroff(A_BOLD);
	} else
		addstr("------/------");

	draw_sub_separator();

	if (NULL != n->recs &&
	    n->recs->bydaysz &&
	    n->recs->byday[0].entries) {
		vv = n->recs->byday[0].netrx /
			n->recs->byday[0].entries;
		attron(A_BOLD);
		draw_xfer(vv, 0);
		attroff(A_BOLD);
		addch('/');
		vv = n->recs->byday[0].nettx /
			n->recs->byday[0].entries;
		attron(A_BOLD);
		draw_xfer(vv, 1);
		attroff(A_BOLD);
	} else
		addstr("------/------");
}

static void
draw_mem(const struct node *n)
{
	double	 vv;

	if (NULL == n->recs) {
		draw_main_separator();
		printw("%31s", " ");
		draw_main_separator();
		return;
	}

	draw_main_separator();

	if (n->recs->byqminsz &&
	    n->recs->byqmin[0].entries) {
		vv = n->recs->byqmin[0].mem /
			n->recs->byqmin[0].entries;
		draw_bars(vv);
		attron(A_BOLD);
		draw_pct(vv);
		attroff(A_BOLD);
	} else
		printw("%17s", " ");

	draw_sub_separator();

	if (n->recs->byhoursz &&
	    n->recs->byhour[0].entries) {
		vv = n->recs->byhour[0].mem /
			n->recs->byhour[0].entries;
		attron(A_BOLD);
		draw_pct(vv);
		attroff(A_BOLD);
	} else
		printw("%6s", " ");

	draw_sub_separator();

	if (n->recs->bydaysz &&
	    n->recs->byday[0].entries) {
		vv = n->recs->byday[0].mem /
			n->recs->byday[0].entries;
		attron(A_BOLD);
		draw_pct(vv);
		attroff(A_BOLD);
	} else
		printw("%6s", " ");

	draw_main_separator();
}

static void
draw_cpu(const struct node *n)
{
	double	 vv;

	if (NULL == n->recs) {
		draw_main_separator();
		printw("%31s", " ");
		draw_main_separator();
		return;
	}

	draw_main_separator();

	if (n->recs->byqminsz &&
	    n->recs->byqmin[0].entries) {
		vv = n->recs->byqmin[0].cpu /
			n->recs->byqmin[0].entries;
		draw_bars(vv);
		attron(A_BOLD);
		draw_pct(vv);
		attroff(A_BOLD);
	} else
		printw("%17s", " ");

	draw_sub_separator();

	if (n->recs->byhoursz &&
	    n->recs->byhour[0].entries) {
		vv = n->recs->byhour[0].cpu /
			n->recs->byhour[0].entries;
		attron(A_BOLD);
		draw_pct(vv);
		attroff(A_BOLD);
	} else
		printw("%6s", " ");

	draw_sub_separator();

	if (n->recs->bydaysz &&
	    n->recs->byday[0].entries) {
		vv = n->recs->byday[0].cpu /
			n->recs->byday[0].entries;
		attron(A_BOLD);
		draw_pct(vv);
		attroff(A_BOLD);
	} else
		printw("%6s", " ");

	draw_main_separator();
}

static void
draw_header(struct draw *d, size_t maxhostsz, size_t maxipsz)
{

	move(0, 0);
	clrtoeol();
	printw("%*s", (int)maxhostsz, "hostname");
	addch(' ');
	draw_main_separator();
	printw("%31s", "CPU");
	draw_main_separator();
	addch(' ');
	draw_main_separator();
	printw("%31s", "memory");
	draw_main_separator();
	addch(' ');
	printw("%41s", "net rx/tx");
	addch(' ');
	printw("%*s", (int)maxipsz, "address");
	addch(' ');
	printw("%9s", "last");
	addch(' ');
	printw("%9s", "ping");
}

void
draw(struct draw *d, const struct node *n, 
	size_t nsz, time_t t)
{
	size_t	 i, sz, maxhostsz, maxipsz,
		 lastseenpos, intervalpos, chhead;

	maxhostsz = strlen("hostname");
	for (i = 0; i < nsz; i++) {
		sz = strlen(n[i].host);
		if (sz > maxhostsz)
			maxhostsz = sz;
	}

	maxipsz = strlen("address");
	for (i = 0; i < nsz; i++) {
		sz = strlen(n[i].addrs.addrs[n[i].addrs.curaddr].ip);
		if (sz > maxipsz)
			maxipsz = sz;
	}

	/*
	 * hostname                          (maxhostsz)
	 * [|||||||||| xxx.x%|xxx.x%|xxx.x%] ([10 6|6|6]=33)
	 * [|||||||||| xxx.x%|xxx.x%|xxx.x%] ([10 6|6|6]=33)
	 * rx:tx|rx:tx|rx:tx                 (6 1 6|13|13=41)
	 * ip                                (maxipsz)
	 * hh:mm:ss                          (last entry=9)
	 * hh:mm:ss                          (last seen=9)
	 */

	for (i = 0; i < nsz; i++) {
		move(i + 1, 0);
		clrtoeol();
		attron(A_BOLD);
		printw("%*s ", (int)maxhostsz, n[i].host);
		attroff(A_BOLD);
		draw_cpu(&n[i]);
		addch(' ');
		draw_mem(&n[i]);
		addch(' ');
		draw_inet(&n[i]);
		printw(" %*s ", (int)maxipsz,
			n[i].addrs.addrs[n[i].addrs.curaddr].ip);
		draw_interval(get_last(&n[i]), t);
		addch(' ');
		draw_interval(n[i].lastseen, t);
	}

	/* Remember for updating times. */

	intervalpos = 
		maxhostsz + 1 + 33 + 1 + 33 + 1 + 
		41 + 1 + maxipsz + 1;
	lastseenpos = 
		intervalpos + 9 + 1;

	chhead = intervalpos != d->intervalpos ||
		lastseenpos != d->lastseenpos;

	d->intervalpos = intervalpos;
	d->lastseenpos = lastseenpos;

	if (chhead)
		draw_header(d, maxhostsz, maxipsz);
}

void
drawtimes(const struct draw *d, 
	const struct node *n, size_t nsz, time_t t)
{
	size_t	 i;

	if (0 == d->intervalpos || 0 == d->lastseenpos)
		return;

	for (i = 0; i < nsz; i++) {
		move(i + 1, d->intervalpos);
		draw_interval(get_last(&n[i]), t);
		move(i + 1, d->lastseenpos);
		draw_interval(n[i].lastseen, t);
	}
}
