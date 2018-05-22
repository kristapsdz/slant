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
draw_pct(double vv, char tail)
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
	addch(tail);
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
		attron(COLOR_PAIR(2));
	else if (ospan >= 60)
		attron(COLOR_PAIR(1));

	hr = span / (60 * 60);
	span -= hr * 60 * 60;
	min = span / 60;
	span -= min * 60;
	printw("%3lld:%.2lld:%.2lld", 
		(long long)hr, (long long)min, 
		(long long)span);

	if (ospan >= 120)
		attroff(COLOR_PAIR(2));
	else if (ospan >= 60)
		attroff(COLOR_PAIR(1));
}

static void
draw_mem(const struct node *n)
{
	double	 vv;

	if (NULL == n->recs) {
		printw("[%32s", "]");
		return;
	}

	addch('[');

	if (n->recs->byqminsz &&
	    n->recs->byqmin[0].entries) {
		vv = n->recs->byqmin[0].mem /
			n->recs->byqmin[0].entries;
		draw_bars(vv);
		draw_pct(vv, '|');
	} else
		printw("%18s", "|");

	if (n->recs->byhoursz &&
	    n->recs->byhour[0].entries) {
		vv = n->recs->byhour[0].mem /
			n->recs->byhour[0].entries;
		draw_pct(vv, '|');
	} else
		printw("%7s", "|");

	if (n->recs->bydaysz &&
	    n->recs->byday[0].entries) {
		vv = n->recs->byday[0].mem /
			n->recs->byday[0].entries;
		draw_pct(vv, ']');
	} else
		printw("%7s", "]");
}

static void
draw_cpu(const struct node *n)
{
	double	 vv;

	if (NULL == n->recs) {
		printw("[%32s", "]");
		return;
	}

	addch('[');

	if (n->recs->byqminsz &&
	    n->recs->byqmin[0].entries) {
		vv = n->recs->byqmin[0].cpu /
			n->recs->byqmin[0].entries;
		draw_bars(vv);
		draw_pct(vv, '|');
	} else
		printw("%18s", "|");

	if (n->recs->byhoursz &&
	    n->recs->byhour[0].entries) {
		vv = n->recs->byhour[0].cpu /
			n->recs->byhour[0].entries;
		draw_pct(vv, '|');
	} else
		printw("%7s", "|");

	if (n->recs->bydaysz &&
	    n->recs->byday[0].entries) {
		vv = n->recs->byday[0].cpu /
			n->recs->byday[0].entries;
		draw_pct(vv, ']');
	} else
		printw("%7s", "]");
}

void
draw(const struct node *n, size_t nsz)
{
	size_t	 i, sz, maxhostsz, maxipsz;
	time_t	 t = time(NULL);

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
	 * [|||||||||| xxx.x%|xxx.x%|xxx.x%] ([10 6|6|6]=31)
	 * [|||||||||| xxx.x%|xxx.x%|xxx.x%] ([10 6|6|6]=31)
	 * hh:mm:ss                          (last entry)
	 * hh:mm:ss                          (last seen)
	 */

	for (i = 0; i < nsz; i++) {
		move(i, 0);
		clrtoeol();
		printw("%*s ", (int)maxhostsz, n[i].host);
		draw_cpu(&n[i]);
		addch(' ');
		draw_mem(&n[i]);
		printw(" %*s ", (int)maxipsz,
			n[i].addrs.addrs[n[i].addrs.curaddr].ip);
		draw_interval(get_last(&n[i]), t);
		addch(' ');
		draw_interval(n[i].lastseen, t);
	}
}
