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
#include <float.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#include "extern.h"
#include "slant.h"

static const char *const states[] = {
	"strt", /* STATE_STARTUP */
	"rslv", /* STATE_RESOLVING */
	"idle", /* STATE_CONNECT_WAITING */
	"cnrd", /* STATE_CONNECT_READY */
	"cnct", /* STATE_CONNECT */
	"cldn", /* STATE_CLOSE_DONE */
	"cler", /* STATE_CLOSE_ERR */
	"wrte", /* STATE_WRITE */
	"read" /* STATE_READ */
};

/*
 * Define a function for drawing rates.
 * This is a bit messy to functionify because of accessing the member
 * variable in each structure.
 * It can be passed in, but would need some smarts.
 */
#define DEFINE_draw_rates(_NAME, _MEMRX, _MEMTX, _DRAW_RATE, \
	_QMIN, _MIN, _HOUR, _DAY, _WEEK, _YEAR) \
static void \
_NAME(unsigned int bits, WINDOW *win, const struct node *n) \
{ \
	double	 vv; \
	if (_QMIN & bits) { \
		bits &= ~_QMIN; \
		if (NULL != n->recs && \
		    n->recs->byqminsz && \
		    n->recs->byqmin[0].entries) { \
			vv = n->recs->byqmin[0]._MEMRX / \
				(double)n->recs->byqmin[0].entries; \
			wattron(win, A_BOLD); \
			_DRAW_RATE(win, vv, 0); \
			wattroff(win, A_BOLD); \
			waddch(win, ':'); \
			vv = n->recs->byqmin[0]._MEMTX / \
				(double)n->recs->byqmin[0].entries; \
			wattron(win, A_BOLD); \
			_DRAW_RATE(win, vv, 1); \
			wattroff(win, A_BOLD); \
		} else \
			waddstr(win, "------:------"); \
		if (bits) \
			draw_sub_separator(win); \
	} \
	if (_MIN & bits) { \
		bits &= ~_MIN; \
		if (NULL != n->recs && \
		    n->recs->byminsz && \
		    n->recs->bymin[0].entries) { \
			vv = n->recs->bymin[0]._MEMRX / \
				(double)n->recs->bymin[0].entries; \
			_DRAW_RATE(win, vv, 0); \
			waddch(win, ':'); \
			vv = n->recs->bymin[0]._MEMTX / \
				(double)n->recs->bymin[0].entries; \
			_DRAW_RATE(win, vv, 1); \
		} else \
			waddstr(win, "------:------"); \
		if (bits) \
			draw_sub_separator(win); \
	} \
	if (_HOUR & bits) { \
		bits &= ~_HOUR; \
		if (NULL != n->recs && \
		    n->recs->byhoursz && \
		    n->recs->byhour[0].entries) { \
			vv = n->recs->byhour[0]._MEMRX / \
				(double)n->recs->byhour[0].entries; \
			_DRAW_RATE(win, vv, 0); \
			waddch(win, ':'); \
			vv = n->recs->byhour[0]._MEMTX / \
				(double)n->recs->byhour[0].entries; \
			_DRAW_RATE(win, vv, 1); \
		} else \
			waddstr(win, "------:------"); \
		if (bits) \
			draw_sub_separator(win); \
	} \
	if (_DAY & bits) { \
		bits &= ~_DAY; \
		if (NULL != n->recs && \
		    n->recs->bydaysz && \
		    n->recs->byday[0].entries) { \
			vv = n->recs->byday[0]._MEMRX / \
				(double)n->recs->byday[0].entries; \
			_DRAW_RATE(win, vv, 0); \
			waddch(win, ':'); \
			vv = n->recs->byday[0]._MEMTX / \
				(double)n->recs->byday[0].entries; \
			_DRAW_RATE(win, vv, 1); \
		} else \
			waddstr(win, "------:------"); \
	} \
	if (_WEEK & bits) { \
		bits &= ~_WEEK; \
		if (NULL != n->recs && \
		    n->recs->byweeksz && \
		    n->recs->byweek[0].entries) { \
			vv = n->recs->byweek[0]._MEMRX / \
				(double)n->recs->byweek[0].entries; \
			_DRAW_RATE(win, vv, 0); \
			waddch(win, ':'); \
			vv = n->recs->byweek[0]._MEMTX / \
				(double)n->recs->byweek[0].entries; \
			_DRAW_RATE(win, vv, 1); \
		} else \
			waddstr(win, "------:------"); \
	} \
	if (_YEAR & bits) { \
		bits &= ~_YEAR; \
		if (NULL != n->recs && \
		    n->recs->byyearsz && \
		    n->recs->byyear[0].entries) { \
			vv = n->recs->byyear[0]._MEMRX / \
				(double)n->recs->byyear[0].entries; \
			_DRAW_RATE(win, vv, 0); \
			waddch(win, ':'); \
			vv = n->recs->byyear[0]._MEMTX / \
				(double)n->recs->byyear[0].entries; \
			_DRAW_RATE(win, vv, 1); \
		} else \
			waddstr(win, "------:------"); \
	} \
	assert(0 == bits); \
}

/*
 * Define a function for drawing percentages (as bars and values).
 * This is a bit messy to functionify because of accessing the member
 * variable in each structure.
 * It can be passed in, but would need some smarts.
 */
#define DEFINE_draw_pcts(_NAME, _MEMBER, _DRAW_PCT, \
	_QMIN_BARS, _QMIN, _MIN, _HOUR, _DAY, _WEEK, _YEAR) \
static void \
_NAME(unsigned int bits, WINDOW *win, const struct node *n) \
{ \
	double	 vv; \
	const struct recset *r = n->recs; \
	if (_QMIN_BARS & bits) { \
		bits &= ~_QMIN_BARS; \
		if (NULL != r && \
		    r->byqminsz && r->byqmin[0].entries) { \
			vv = r->byqmin[0]._MEMBER / \
			     r->byqmin[0].entries; \
			draw_bars(win, vv); \
		} else \
			wprintw(win, "%10s", " "); \
		if (bits) \
			waddch(win, ' '); \
	} \
	if (_QMIN & bits) { \
		bits &= ~_QMIN; \
		if (NULL != r && \
		    r->byqminsz && r->byqmin[0].entries) { \
			vv = r->byqmin[0]._MEMBER / \
			     r->byqmin[0].entries; \
			wattron(win, A_BOLD); \
			_DRAW_PCT(win, vv); \
			wattroff(win, A_BOLD); \
		} else if (NULL != n->recs) { \
			wprintw(win, "%6s", " "); \
		} else  \
			wprintw(win, "------%"); \
		if (bits) \
			draw_sub_separator(win); \
	} \
	if (_MIN & bits) { \
		bits &= ~_MIN; \
		if (NULL != r && \
		    r->byminsz && r->bymin[0].entries) { \
			vv = r->bymin[0]._MEMBER / \
		   	     r->bymin[0].entries; \
			_DRAW_PCT(win, vv); \
		} else if (NULL != n->recs) { \
			wprintw(win, "%6s", " ");  \
		} else \
			wprintw(win, "------%"); \
		if (bits) \
			draw_sub_separator(win); \
	} \
	if (_HOUR & bits) { \
		bits &= ~_HOUR; \
		if (NULL != r && \
		    r->byhoursz && r->byhour[0].entries) { \
			vv = r->byhour[0]._MEMBER / \
		   	     r->byhour[0].entries; \
			_DRAW_PCT(win, vv); \
		} else if (NULL != n->recs) { \
			wprintw(win, "%6s", " ");  \
		} else \
			wprintw(win, "------%"); \
		if (bits) \
			draw_sub_separator(win); \
	} \
	if (_DAY & bits) { \
		bits &= ~_DAY; \
		if (NULL != r && \
		    r->bydaysz && r->byday[0].entries) { \
			vv = r->byday[0]._MEMBER / \
			     r->byday[0].entries; \
			_DRAW_PCT(win, vv); \
		} else if (NULL != n->recs) { \
			wprintw(win, "%6s", " ");  \
		} else \
			wprintw(win, "------%"); \
	} \
	if (_WEEK & bits) { \
		bits &= ~_WEEK; \
		if (NULL != r && \
		    r->byweeksz && r->byweek[0].entries) { \
			vv = r->byweek[0]._MEMBER / \
		  	     r->byweek[0].entries; \
			_DRAW_PCT(win, vv); \
		} else if (NULL != n->recs) { \
			wprintw(win, "%6s", " ");  \
		} else \
			wprintw(win, "------%"); \
	} \
	if (_YEAR & bits) { \
		bits &= ~_YEAR; \
		if (NULL != r && \
		    r->byyearsz && r->byyear[0].entries) { \
			vv = r->byyear[0]._MEMBER / \
			     r->byyear[0].entries; \
			_DRAW_PCT(win, vv); \
		} else if (NULL != n->recs) { \
			wprintw(win, "%6s", " ");  \
		} else \
			wprintw(win, "------%"); \
	} \
	assert(0 == bits); \
}

/*
 * Define a function for getting colunm widths of a field.
 * Used with DEFINE_draw_rates.
 */
#define DEFINE_size_rates(_NAME, _QMIN, \
	_MIN, _HOUR, _DAY, _WEEK, _YEAR) \
static size_t \
_NAME(unsigned int bits) \
{ \
	size_t sz = 0; \
	if (_QMIN & bits) { \
		bits &= ~_QMIN; \
		sz += 13 + (bits ? 1 : 0); \
	} \
	if (_MIN & bits) { \
		bits &= ~_MIN; \
		sz += 13 + (bits ? 1 : 0); \
	} \
	if (_HOUR & bits) { \
		bits &= ~_HOUR; \
		sz += 13 + (bits ? 1 : 0); \
	} \
	if (_DAY & bits) { \
		bits &= ~_DAY; \
		sz += 13; \
	} \
	if (_WEEK & bits) { \
		bits &= ~_WEEK; \
		sz += 13; \
	} \
	if (_YEAR & bits) { \
		bits &= ~_YEAR; \
		sz += 13; \
	} \
	assert(0 == bits); \
	return sz; \
}

/*
 * Define a function for getting colunm widths of a percentage box.
 * Used with DEFINE_draw_pcts.
 */
#define DEFINE_size_pct(_NAME, _QMIN_BARS, _QMIN, \
	_MIN, _HOUR, _DAY, _WEEK, _YEAR) \
static size_t \
_NAME(unsigned int bits) \
{ \
	size_t sz = 0; \
	if (_QMIN_BARS & bits) { \
		bits &= ~_QMIN_BARS; \
		sz += 10 + (bits ? 1 : 0); \
	} \
	if (_QMIN & bits) { \
		bits &= ~_QMIN; \
		sz += 6 + (bits ? 1 : 0); \
	} \
	if (_MIN & bits) { \
		bits &= ~_MIN; \
		sz += 6 + (bits ? 1 : 0); \
	} \
	if (_HOUR & bits) { \
		bits &= ~_HOUR; \
		sz += 6 + (bits ? 1 : 0); \
	} \
	if (_DAY & bits) { \
		bits &= ~_DAY; \
		sz += 6; \
	} \
	if (_WEEK & bits) { \
		bits &= ~_WEEK; \
		sz += 6; \
	} \
	if (_YEAR & bits) { \
		bits &= ~_YEAR; \
		sz += 6; \
	} \
	return sz; \
}

/*
 * Get column widths of the link box.
 * Used with draw_link().
 */
static size_t
size_link(size_t maxipsz, unsigned int bits)
{
	size_t	 sz = 0;

	if (LINK_IP & bits) {
		bits &= ~LINK_IP;
		sz += maxipsz + ((bits & LINK_STATE) ? 1 : 0);
	}
	if (LINK_STATE & bits) {
		bits &= ~LINK_STATE;
		sz += 4 + (bits ? 1 : 0);
	}
	if (LINK_ACCESS & bits) {
		bits &= ~LINK_ACCESS;
		sz += 9;
	}
	assert(0 == bits);
	return sz;
}

/*
 * Return the last time for which we have some data.
 * This can come from any of the intervals.
 * Return zero if there's no last time.
 * We can legitimately return zero if that's what we're getting from the
 * server, which of course would be bogus.
 */
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

/*
 * Separator for main columns. 
 */
static void
draw_main_separator(WINDOW *win)
{

	wprintw(win, "%lc", L'\x2502');
}

/*
 * Separator for sub columns.
 */
static void
draw_sub_separator(WINDOW *win)
{

	wprintw(win, "%lc", L'\x250a');
}

/*
 * Horizontal bar graph: partial <5%.
 */
static void
draw_bar_light(WINDOW *win)
{

	wprintw(win, "%lc", L'\x2758');
}

/*
 * Horizontal bar graph: partial >=5%.
 */
static void
draw_bar_medium(WINDOW *win)
{

	wprintw(win, "%lc", L'\x2759');
}

/*
 * Horizontal bar graph: full 10%.
 */
static void
draw_bar_heavy(WINDOW *win)
{

	wprintw(win, "%lc", L'\x275A');
}

/*
 * Draw a bar graph of up to vv%, where a bar is 10%.
 * This draws up to <50% as normal colour, >=50% as yellow, then >=80%
 * as a red bar.
 * Partial bars are also shown.
 */
static void
draw_bars(WINDOW *win, double vv)
{
	size_t	 i;
	double	 v;

	/* 
	 * First we draw solid marks that show we're at least at this
	 * percentage of usage.
	 * 10%  = |x         |
	 * 15%  = |x         |
	 * 50%  = |xxxxx     |
	 * 100% = |xxxxxxxxxx|
	 */

	for (i = 1; i <= 10; i++) {
		v = i * 10.0;
		if (v > vv)
			break;
		if (i >= 8)
			wattron(win, COLOR_PAIR(2));
		else if (i >= 5)
			wattron(win, COLOR_PAIR(1));
		draw_bar_heavy(win);
		if (i >= 8)
			wattroff(win, COLOR_PAIR(2));
		else if (i >= 5)
			wattroff(win, COLOR_PAIR(1));
	}

	/* If we're at 100%, bail. */

	if (i > 10)
		return;

	/*
	 * Now add either an light mark or medium mark depending on how
	 * much is in the remainder.
	 */

	if (i >= 8)
		wattron(win, COLOR_PAIR(2));
	else if (i >= 5)
		wattron(win, COLOR_PAIR(1));

	v = i * 10.0;
	assert(v > vv);
	if (v - vv <= 5.0)
		draw_bar_medium(win);
	else
		draw_bar_light(win);
	if (i >= 8)
		wattroff(win, COLOR_PAIR(2));
	else if (i >= 5)
		wattroff(win, COLOR_PAIR(1));

	for (++i; i <= 10; i++)
		waddch(win, ' ');
}

/*
 * Draw percentage that should be 100%.
 * Anything less than 100% is red.
 */
static void
draw_rpct(WINDOW *win, double vv)
{
	if (vv < 100.0 - FLT_EPSILON)
		wattron(win, COLOR_PAIR(2));
	wprintw(win, "%5.1f%%", vv);
	if (vv < 100.0 - FLT_EPSILON)
		wattroff(win, COLOR_PAIR(2));
}

/*
 * Draw the percentage attached to a bar graph (or not).
 * More than 50% inclusive gets a yellow colour, more than 80 has red.
 */
static void
draw_pct(WINDOW *win, double vv)
{
	if (vv >= 80.0)
		wattron(win, COLOR_PAIR(2));
	else if (vv >= 50.0)
		wattron(win, COLOR_PAIR(1));
	wprintw(win, "%5.1f%%", vv);
	if (vv >= 80.0)
		wattroff(win, COLOR_PAIR(2));
	else if (vv >= 50.0)
		wattroff(win, COLOR_PAIR(1));
}

/*
 * Draw the amount of time elased from "last" to "now", unless "last" is
 * zero, in which case draw something that indicates no time exists.
 * Bound below at zero elapsed time.
 * If the time is "worrisome", draw it as yellow; if "probably wrong",
 * draw as red.
 */
static void
draw_interval(WINDOW *win, time_t waittime, 
	time_t timeo, time_t last, time_t now)
{
	time_t	 ospan, span, hr, min;
	int	 attrs = 0, b1, b2;

	if (0 == last) {
		waddstr(win, "---:--:--");
		return;
	}

	b1 = A_BOLD | COLOR_PAIR(1);
	b2 = A_BOLD | COLOR_PAIR(2);

	if ((span = now - last) < 0)
		span = 0;
	
	ospan = span;

	/*
	 * Be smart about when we colour our last-seen times.
	 * If we're on a short interval (waittime < 30), then give us
	 * some leeway---connections might take time.
	 * Otherwise, assume connections happen "instantly".
	 */

	if (waittime == timeo) {
		/* Time since last contacting remote. */
		if (waittime <= 30) {
			if (ospan >= waittime * 8)
				wattron(win, attrs = b2);
			else if (ospan >= waittime * 4)
				wattron(win, attrs = b1);
		} else {
			if (ospan >= waittime * 6)
				wattron(win, attrs = b2);
			else if (ospan >= waittime * 3)
				wattron(win, attrs = b1);
		}
	} else {
		/* Time since remote collection of data. */
		if (ospan >= timeo + (waittime * 8))
			wattron(win, attrs = b2);
		else if (ospan >= timeo + (waittime * 4))
			wattron(win, attrs = b1);
	}

	hr = span / (60 * 60);
	span -= hr * 60 * 60;
	min = span / 60;
	span -= min * 60;
	wprintw(win, "%3lld:%.2lld:%.2lld", 
		(long long)hr, (long long)min, 
		(long long)span);

	if (attrs)
		wattroff(win, attrs);
}

static void
draw_xfer(WINDOW *win, double vv, int left)
{
	char	 nbuf[16];

	if (vv >= 1000 * 1000 * 1000)
		snprintf(nbuf, sizeof(nbuf), "%.1fG", 
			vv / (1024 * 1024 * 1024));
	else if (vv >= 1000 * 1000)
		snprintf(nbuf, sizeof(nbuf), 
			"%.1fM", vv / (1024 * 1024));
	else if (vv >= 1000)
		snprintf(nbuf, sizeof(nbuf), "%.1fK", vv / 1024);
	else if (vv < 0.001)
		snprintf(nbuf, sizeof(nbuf), "%gB", 0.0);
	else 
		snprintf(nbuf, sizeof(nbuf), "%.0fB", vv);

	if (left)
		wprintw(win, "%-6s", nbuf);
	else
		wprintw(win, "%6s", nbuf);
}

static void
draw_link(unsigned int bits, size_t maxipsz, time_t timeo,
	time_t t, WINDOW *win, const struct node *n, 
	size_t *lastseen, struct drawbox *box)
{
	int	 x, y;

	*lastseen = 0;

	if (LINK_IP & bits) {
		bits &= ~LINK_IP;
		wprintw(win, "%*s", (int)maxipsz,
			n->addrs.addrs[n->addrs.curaddr].ip);
		if (LINK_STATE & bits)
			waddstr(win, ":");
	}

	if (LINK_STATE & bits) {
		bits &= ~LINK_STATE;
		waddstr(win, states[n->state]);
		if (LINK_ACCESS & bits)
			waddch(win, ' ');
	}

	if (LINK_ACCESS & bits) {
		bits &= ~LINK_ACCESS;
		getyx(win, y, x);
		*lastseen = x;
		draw_interval(win, timeo, timeo, n->lastseen, t);
	}

	assert(0 == bits);
}

DEFINE_draw_pcts(draw_files, nfiles, draw_pct,
	FILES_QMIN_BARS, FILES_QMIN, FILES_MIN, 
	FILES_HOUR, FILES_DAY, FILES_WEEK, FILES_YEAR)
DEFINE_size_pct(size_files, FILES_QMIN_BARS, FILES_QMIN, FILES_MIN, 
	FILES_HOUR, FILES_DAY, FILES_WEEK, FILES_YEAR)

DEFINE_draw_pcts(draw_procs, nprocs, draw_pct,
	PROCS_QMIN_BARS, PROCS_QMIN, PROCS_MIN, 
	PROCS_HOUR, PROCS_DAY, PROCS_WEEK, PROCS_YEAR)
DEFINE_size_pct(size_procs, PROCS_QMIN_BARS, PROCS_QMIN, PROCS_MIN, 
	PROCS_HOUR, PROCS_DAY, PROCS_WEEK, PROCS_YEAR)

DEFINE_draw_pcts(draw_rprocs, rprocs, draw_rpct,
	RPROCS_QMIN_BARS, RPROCS_QMIN, RPROCS_MIN, 
	RPROCS_HOUR, RPROCS_DAY, RPROCS_WEEK, RPROCS_YEAR)
DEFINE_size_pct(size_rprocs, RPROCS_QMIN_BARS, RPROCS_QMIN, 
	RPROCS_MIN, RPROCS_HOUR, RPROCS_DAY, RPROCS_WEEK, RPROCS_YEAR)

DEFINE_draw_pcts(draw_mem, mem, draw_pct,
	MEM_QMIN_BARS, MEM_QMIN, MEM_MIN, 
	MEM_HOUR, MEM_DAY, MEM_WEEK, MEM_YEAR)
DEFINE_size_pct(size_mem, MEM_QMIN_BARS, MEM_QMIN, 
	MEM_MIN, MEM_HOUR, MEM_DAY, MEM_WEEK, MEM_YEAR)

DEFINE_draw_pcts(draw_cpu, cpu, draw_pct,
	CPU_QMIN_BARS, CPU_QMIN, CPU_MIN, 
	CPU_HOUR, CPU_DAY, CPU_WEEK, CPU_YEAR)
DEFINE_size_pct(size_cpu, CPU_QMIN_BARS, CPU_QMIN, 
	CPU_MIN, CPU_HOUR, CPU_DAY, CPU_WEEK, CPU_YEAR)

DEFINE_draw_rates(draw_net, netrx, nettx, draw_xfer,
	NET_QMIN, NET_MIN, NET_HOUR, 
	NET_DAY, NET_WEEK, NET_YEAR)
DEFINE_size_rates(size_net, NET_QMIN, NET_MIN, 
	NET_HOUR, NET_DAY, NET_WEEK, NET_YEAR)

DEFINE_draw_rates(draw_disc, discread, discwrite, draw_xfer,
	NET_QMIN, NET_MIN, NET_HOUR, 
	NET_DAY, NET_WEEK, NET_YEAR)
DEFINE_size_rates(size_disc, DISC_QMIN, DISC_MIN, 
	DISC_HOUR, DISC_DAY, DISC_WEEK, DISC_YEAR)

static void
draw_centre(WINDOW *win, const char *v, size_t sz)
{
	size_t	 vsz = strlen(v), left, i;

	assert(vsz <= sz);

	left = (sz - vsz) / 2;

	for (i = 0; i < left; i++)
		waddch(win, ' ');
	waddstr(win, v);
	i += vsz;
	for ( ; i < sz; i++)
		waddch(win, ' ');
}

/*
 * Copute the width of a give box with its bits.
 * FIXME: this can somehow be merged with the header code.
 */
static size_t
compute_box(const struct drawbox *box, unsigned int bits,
	size_t maxipsz)
{
	size_t	 sz = 0;

	if (0 == bits)
		return 0;

	/* Leading bar plus padding. */

	sz += 3;

	switch (box->cat) {
	case DRAWCAT_CPU:
		sz += size_cpu(bits);
		break;
	case DRAWCAT_MEM:
		sz += size_mem(bits);
		break;
	case DRAWCAT_PROCS:
		sz += size_procs(bits);
		break;
	case DRAWCAT_RPROCS:
		sz += size_rprocs(bits);
		break;
	case DRAWCAT_FILES:
		sz += size_files(bits);
		break;
	case DRAWCAT_NET:
		sz += size_net(bits);
		break;
	case DRAWCAT_DISC:
		sz += size_disc(bits);
		break;
	case DRAWCAT_LINK:
		sz += size_link(maxipsz, bits);
		break;
	case DRAWCAT_HOST:
		/* "Last" time. */
		sz += 9;
		break;
	}

	return sz;
}

/*
 * Compute the width of all drawn boxes.
 * This is the *maximum* width, so if we're showing IP addresses, this
 * will also include IPV6 addresses.
 * Always returns >0, even in the degenerate case where we're showing no
 * boxes and our hostnames are empty (they shouldn't be).
 * FIXME: this can somehow be merged with the header code.
 */
size_t
compute_width(const struct node *n, 
	size_t nsz, const struct draw *d)
{
	size_t	 sz, maxhostsz, maxipsz, i, j, line, maxline;

	/* We always show our hostname. */

	maxhostsz = strlen("hostname");
	for (i = 0; i < nsz; i++)
		if ((sz = strlen(n[i].host)) > maxhostsz)
			maxhostsz = sz;

	/* We conditionally show our IPV4/IPV6 address. */

	maxipsz = strlen("address");
	for (i = 0; i < nsz; i++)
		for (j = 0; j < n[i].addrs.addrsz; j++) {
			sz = strlen(n[i].addrs.addrs[j].ip);
			if (sz > maxipsz)
				maxipsz = sz;
		}

	/* Look for the maximum length of all lines. */

	for (sz = maxhostsz + 1, i = 0; i < d->boxsz; i++) {
		maxline = compute_box(&d->box[i], 
			d->box[i].line1, maxipsz);
		line = compute_box(&d->box[i], 
			d->box[i].line2, maxipsz);
		if (line > maxline)
			maxline = line;
		sz += maxline;
	}

	return sz;
}

/*
 * Draw the header for any given box (column) and sets the maximum size
 * that the box will allow in its rows along with the size for each
 * line.
 * This requires knowledge of the (maximum possible) column width, so we
 * need to iterate through all lines in the box to find the maximum.
 */
static void
draw_header_box(struct out *out, 
	struct drawbox *box, const struct draw *d)
{

	draw_main_separator(out->mainwin);
	waddch(out->mainwin, ' ');
	switch (box->cat) {
	case DRAWCAT_CPU:
		box->len = box->len1 = size_cpu(box->line1);
		if ((box->len2 = size_cpu(box->line2)) > box->len)
			box->len = box->len2;
		draw_centre(out->mainwin, "cpu", box->len);
		break;
	case DRAWCAT_MEM:
		box->len = box->len1 = size_mem(box->line1);
		if ((box->len2 = size_mem(box->line2)) > box->len)
			box->len = box->len2;
		draw_centre(out->mainwin, "mem", box->len);
		break;
	case DRAWCAT_PROCS:
		box->len = box->len1 = size_procs(box->line1);
		if ((box->len2 = size_procs(box->line2)) > box->len)
			box->len = box->len2;
		draw_centre(out->mainwin, "procs", box->len);
		break;
	case DRAWCAT_RPROCS:
		box->len = box->len1 = size_rprocs(box->line1);
		if ((box->len2 = size_rprocs(box->line2)) > box->len)
			box->len = box->len2;
		if (box->len < 9)
			draw_centre(out->mainwin, "run", box->len);
		else
			draw_centre(out->mainwin, "running", box->len);
		break;
	case DRAWCAT_FILES:
		box->len = box->len1 = size_files(box->line1);
		if ((box->len2 = size_files(box->line2)) > box->len)
			box->len = box->len2;
		draw_centre(out->mainwin, "files", box->len);
		break;
	case DRAWCAT_NET:
		box->len = box->len1 = size_net(box->line1);
		if ((box->len2 = size_net(box->line2)) > box->len)
			box->len = box->len2;
		if (box->len < 12)
			draw_centre(out->mainwin, 
				"inet", box->len);
		else
			draw_centre(out->mainwin, 
				"inet rx:tx", box->len);
		break;
	case DRAWCAT_DISC:
		box->len = box->len1 = size_disc(box->line1);
		if ((box->len2 = size_disc(box->line2)) > box->len)
			box->len = box->len2;
		if (box->len < 12)
			draw_centre(out->mainwin, 
				"disc r:w", box->len);
		else
			draw_centre(out->mainwin, 
				"disc rd:wr", box->len);
		break;
	case DRAWCAT_LINK:
		box->len = box->len1 = 
			size_link(d->maxipsz, box->line1);
		box->len2 = size_link(d->maxipsz, box->line2);
		if (box->len2 > box->len)
			box->len = box->len2;
		if (box->len < 12)
			draw_centre(out->mainwin, 
				"link", box->len);
		else
			draw_centre(out->mainwin, 
				"link state", box->len);
		break;
	case DRAWCAT_HOST:
		box->len = box->len1 = box->len2 = 9;
		wprintw(out->mainwin, "%9s", "last");
		break;
	}

	waddch(out->mainwin, ' ');
}

/*
 * Draw the output header and computes the maximum space for each box
 * (set in d->box->len).
 * This is a single line of text above all other lines that identifies
 * the contents of all boxes (columns).
 */
static void
draw_header(struct out *out, struct draw *d)
{
	size_t	 i;

	wmove(out->mainwin, 0, 1);
	wclrtoeol(out->mainwin);
	wprintw(out->mainwin, "%*s", 
		(int)d->maxhostsz, "hostname");
	waddch(out->mainwin, ' ');

	/* Only draw if we have data. */

	for (i = 0; i < d->boxsz; i++)
		draw_header_box(out, &d->box[i], d);
}

/*
 * Draw a single content box for node "n".
 */
static void
draw_box(struct out *out, const struct node *n, struct drawbox *box, 
	unsigned int bits, size_t *lastseen, size_t *lastrecord,
	size_t len, const struct draw *d, time_t t)
{
	int	 x, y;
	size_t	 i, resid;

	draw_main_separator(out->mainwin);
	waddch(out->mainwin, ' ');

	/* 
	 * Nothing here: just print all spaces and exit.
	 * TODO: don't print any spaces, just move.
	 */

	if (0 == bits) {
		for (i = 0; i < box->len; i++)
			waddch(out->mainwin, ' ');
		waddch(out->mainwin, ' ');
		return;
	}

	/* Right-justify our content. */

	assert(len <= box->len);
	resid = box->len - len;
	for (i = 0; i < resid; i++)
		waddch(out->mainwin, ' ');

	switch (box->cat) {
	case DRAWCAT_CPU:
		draw_cpu(bits, out->mainwin, n);
		break;
	case DRAWCAT_MEM:
		draw_mem(bits, out->mainwin, n);
		break;
	case DRAWCAT_NET:
		draw_net(bits, out->mainwin, n);
		break;
	case DRAWCAT_DISC:
		draw_disc(bits, out->mainwin, n);
		break;
	case DRAWCAT_LINK:
		draw_link(bits, d->maxipsz, n->waittime, t, 
			out->mainwin, n, lastseen, box);
		break;
	case DRAWCAT_HOST:
		getyx(out->mainwin, y, x);
		*lastrecord = x;
		draw_interval(out->mainwin, 15, 
			n->waittime, get_last(n), t);
		break;
	case DRAWCAT_PROCS:
		draw_procs(bits, out->mainwin, n);
		break;
	case DRAWCAT_RPROCS:
		draw_rprocs(bits, out->mainwin, n);
		break;
	case DRAWCAT_FILES:
		draw_files(bits, out->mainwin, n);
		break;
	}

	waddch(out->mainwin, ' ');
}

void
draw(struct out *out, struct draw *d, int redraw_header,
	const struct node *n, size_t nsz, time_t t)
{
	size_t		 i, j, k, sz, maxhostsz, maxipsz, lines = 1;
	int		 maxy, maxx;

	/* Test how many lines we have per row (FIXME: cache). */

	for (i = 0; i < d->boxsz; i++)
		if (d->box[i].line2) {
			lines = 2;
			break;
		}

	/* Don't let us run off the window. */

	getmaxyx(out->mainwin, maxy, maxx);
	if (nsz * lines > (size_t)maxy - 1)
		nsz = (maxy - 1) / lines;

	/* 
	 * Compute our maximum hostname and IP address size.
	 * We only use the current IP address and the hosts that are
	 * shown (remember, there may be more hosts than rows).
	 * If these are different from the existing sizes, then we'll
	 * need to redraw our header and recompute the column widths.
	 */

	maxhostsz = strlen("hostname");
	for (i = 0; i < nsz; i++) {
		sz = strlen(n[i].host); /* FIXME: cache. */
		if (sz > maxhostsz)
			maxhostsz = sz;
	}
	if (maxhostsz != d->maxhostsz) {
		redraw_header = 1;
		d->maxhostsz = maxhostsz;
	}

	maxipsz = strlen("address");
	for (i = 0; i < nsz; i++) {
		sz = strlen(n[i].addrs.addrs[n[i].addrs.curaddr].ip);
		if (sz > maxipsz)
			maxipsz = sz;
	}
	if (maxipsz != d->maxipsz) {
		redraw_header = 1;
		d->maxipsz = maxipsz;
	}

	/* Conditionally draw (or redraw) the header. */

	if (d->header && redraw_header)
		draw_header(out, d);

	for (i = j = 0; i < nsz; i++, j += lines) {
		wmove(out->mainwin, j + d->header, 1);
		wclrtoeol(out->mainwin);
		if (lines > 1) {
			wmove(out->mainwin, j + 1 + d->header, 1);
			wclrtoeol(out->mainwin);
		}

		wmove(out->mainwin, j + d->header, 1);
		wattron(out->mainwin, A_BOLD);
		wprintw(out->mainwin, "%*s", 
			(int)d->maxhostsz, n[i].host);
		wattroff(out->mainwin, A_BOLD);
		waddch(out->mainwin, ' ');

		for (k = 0; k < d->boxsz; k++)
			draw_box(out, &n[i], &d->box[k], 
				d->box[k].line1, 
				&d->box[k].lastseen1, 
				&d->box[k].lastrecord1, 
				d->box[k].len1,
				d, t);

		if (lines > 1) {
			wmove(out->mainwin, 
				j + 1 + d->header, 
				d->maxhostsz + 2);
			for (k = 0; k < d->boxsz; k++)
				draw_box(out, &n[i], &d->box[k], 
					d->box[k].line2, 
					&d->box[k].lastseen2, 
					&d->box[k].lastrecord2, 
					d->box[k].len2,
					d, t);
		}
	}
}

/*
 * If we have no new data but one second has elapsed, then redraw the
 * interval from last collection and last ping time.
 * We do this by overwriting only that data, which reduces screen update
 * and keeps our display running tight.
 */
void
drawtimes(struct out *out, const struct draw *d, 
	const struct node *n, size_t nsz, time_t t)
{
	size_t	 i, j, k, lines = 1;
	int	 maxy, maxx;

	/* Test how many lines we have per row (FIXME: cache). */

	for (i = 0; i < d->boxsz; i++)
		if (d->box[i].line2) {
			lines = 2;
			break;
		}

	/* Don't let us run off the window. */

	getmaxyx(out->mainwin, maxy, maxx);
	if (nsz * lines > (size_t)maxy - 1)
		nsz = (maxy - 1) / lines;

	for (i = 0; i < d->boxsz; i++) { 
		if (d->box[i].lastseen1)
			for (k = j = 0; j < nsz; j++, k += lines) {
				wmove(out->mainwin, k + 1, 
					d->box[i].lastseen1);
				draw_interval(out->mainwin, 
					n[j].waittime, n[j].waittime, 
					n[j].lastseen, t);
			}
		if (d->box[i].lastseen2)
			for (k = j = 0; j < nsz; j++, k += lines) {
				wmove(out->mainwin, k + 1 + 1, 
					d->box[i].lastseen2);
				draw_interval(out->mainwin, 
					n[j].waittime, n[j].waittime, 
					n[j].lastseen, t);
			}
		if (d->box[i].lastrecord1)
			for (k = j = 0; j < nsz; j++, k += lines) {
				wmove(out->mainwin, k + 1, 
					d->box[i].lastrecord1);
				draw_interval(out->mainwin, 15, 
					n[j].waittime, 
					get_last(&n[j]), t);
			}
		if (d->box[i].lastrecord2)
			for (k = j = 0; j < nsz; j++, k += lines) {
				wmove(out->mainwin, k + 1 + 1, 
					d->box[i].lastrecord2);
				draw_interval(out->mainwin, 15, 
					n[j].waittime, 
					get_last(&n[j]), t);
			}
	}
}
