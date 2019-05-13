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
#include "config.h"

#if HAVE_SYS_QUEUE
# include <sys/queue.h>
#endif
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
#define DEFINE_draw_rates(_NAME, _MEMRX, _MEMTX, _DRAW_RATE) \
static void \
_NAME(unsigned int bits, WINDOW *win, const struct node *n) \
{ \
	double	 vv; \
	if (LINE_QMIN & bits) { \
		bits &= ~LINE_QMIN; \
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
	if (LINE_MIN & bits) { \
		bits &= ~LINE_MIN; \
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
	if (LINE_HOUR & bits) { \
		bits &= ~LINE_HOUR; \
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
	if (LINE_DAY & bits) { \
		bits &= ~LINE_DAY; \
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
	if (LINE_WEEK & bits) { \
		bits &= ~LINE_WEEK; \
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
	if (LINE_YEAR & bits) { \
		bits &= ~LINE_YEAR; \
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
#define DEFINE_draw_pcts(_NAME, _MEMBER, _DRAW_PCT) \
static void \
_NAME(unsigned int bits, WINDOW *win, const struct node *n) \
{ \
	double	 vv; \
	const struct recset *r = n->recs; \
	if (LINE_QMIN_BARS & bits) { \
		bits &= ~LINE_QMIN_BARS; \
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
	if (LINE_MIN_BARS & bits) { \
		bits &= ~LINE_MIN_BARS; \
		if (NULL != r && \
		    r->byminsz && r->bymin[0].entries) { \
			vv = r->bymin[0]._MEMBER / \
			     r->bymin[0].entries; \
			draw_bars(win, vv); \
		} else \
			wprintw(win, "%10s", " "); \
		if (bits) \
			waddch(win, ' '); \
	} \
	if (LINE_HOUR_BARS & bits) { \
		bits &= ~LINE_HOUR_BARS; \
		if (NULL != r && \
		    r->byhoursz && r->byhour[0].entries) { \
			vv = r->byhour[0]._MEMBER / \
			     r->byhour[0].entries; \
			draw_bars(win, vv); \
		} else \
			wprintw(win, "%10s", " "); \
		if (bits) \
			waddch(win, ' '); \
	} \
	if (LINE_DAY_BARS & bits) { \
		bits &= ~LINE_DAY_BARS; \
		if (NULL != r && \
		    r->bydaysz && r->byday[0].entries) { \
			vv = r->byday[0]._MEMBER / \
			     r->byday[0].entries; \
			draw_bars(win, vv); \
		} else \
			wprintw(win, "%10s", " "); \
		if (bits) \
			waddch(win, ' '); \
	} \
	if (LINE_WEEK_BARS & bits) { \
		bits &= ~LINE_WEEK_BARS; \
		if (NULL != r && \
		    r->byweeksz && r->byweek[0].entries) { \
			vv = r->byweek[0]._MEMBER / \
			     r->byweek[0].entries; \
			draw_bars(win, vv); \
		} else \
			wprintw(win, "%10s", " "); \
		if (bits) \
			waddch(win, ' '); \
	} \
	if (LINE_YEAR_BARS & bits) { \
		bits &= ~LINE_YEAR_BARS; \
		if (NULL != r && \
		    r->byyearsz && r->byyear[0].entries) { \
			vv = r->byyear[0]._MEMBER / \
			     r->byyear[0].entries; \
			draw_bars(win, vv); \
		} else \
			wprintw(win, "%10s", " "); \
		if (bits) \
			waddch(win, ' '); \
	} \
	if (LINE_QMIN & bits) { \
		bits &= ~LINE_QMIN; \
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
	if (LINE_MIN & bits) { \
		bits &= ~LINE_MIN; \
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
	if (LINE_HOUR & bits) { \
		bits &= ~LINE_HOUR; \
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
	if (LINE_DAY & bits) { \
		bits &= ~LINE_DAY; \
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
	if (LINE_WEEK & bits) { \
		bits &= ~LINE_WEEK; \
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
	if (LINE_YEAR & bits) { \
		bits &= ~LINE_YEAR; \
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
 * Get colunm widths of a field.
 * Used with DEFINE_draw_rates.
 */
static size_t
size_rate(unsigned int bits)
{
	size_t	sz = 0;

	if (LINE_QMIN & bits) {
		bits &= ~LINE_QMIN;
		sz += 13 + (bits ? 1 : 0);
	}
	if (LINE_MIN & bits) {
		bits &= ~LINE_MIN;
		sz += 13 + (bits ? 1 : 0);
	}
	if (LINE_HOUR & bits) {
		bits &= ~LINE_HOUR;
		sz += 13 + (bits ? 1 : 0);
	}
	if (LINE_DAY & bits) {
		bits &= ~LINE_DAY;
		sz += 13;
	}
	if (LINE_WEEK & bits) {
		bits &= ~LINE_WEEK;
		sz += 13;
	}
	if (LINE_YEAR & bits) {
		bits &= ~LINE_YEAR;
		sz += 13;
	}

	assert(0 == bits);
	return sz;
}

/*
 * Get colunm widths of a percentage box.
 * Used with DEFINE_draw_pcts.
 */
static size_t
size_pct(unsigned int bits)
{
	size_t sz = 0;

	if (LINE_QMIN_BARS & bits) {
		bits &= ~LINE_QMIN_BARS;
		sz += 10 + (bits ? 1 : 0);
	}
	if (LINE_MIN_BARS & bits) {
		bits &= ~LINE_MIN_BARS;
		sz += 10 + (bits ? 1 : 0);
	}
	if (LINE_HOUR_BARS & bits) {
		bits &= ~LINE_HOUR_BARS;
		sz += 10 + (bits ? 1 : 0);
	}
	if (LINE_DAY_BARS & bits) {
		bits &= ~LINE_DAY_BARS;
		sz += 10 + (bits ? 1 : 0);
	}
	if (LINE_WEEK_BARS & bits) {
		bits &= ~LINE_WEEK_BARS;
		sz += 10 + (bits ? 1 : 0);
	}
	if (LINE_YEAR_BARS & bits) {
		bits &= ~LINE_YEAR_BARS;
		sz += 10 + (bits ? 1 : 0);
	}
	if (LINE_QMIN & bits) {
		bits &= ~LINE_QMIN;
		sz += 6 + (bits ? 1 : 0);
	}
	if (LINE_MIN & bits) {
		bits &= ~LINE_MIN;
		sz += 6 + (bits ? 1 : 0);
	}
	if (LINE_HOUR & bits) {
		bits &= ~LINE_HOUR;
		sz += 6 + (bits ? 1 : 0);
	}
	if (LINE_DAY & bits) {
		bits &= ~LINE_DAY;
		sz += 6;
	}
	if (LINE_WEEK & bits) {
		bits &= ~LINE_WEEK;
		sz += 6;
	}
	if (LINE_YEAR & bits) {
		bits &= ~LINE_YEAR;
		sz += 6;
	}

	return sz;
}

/*
 * Get column widths of the host box.
 * Used with draw_host().
 */
static size_t
size_host(const struct draw *d, unsigned int bits)
{
	size_t	 sz = 0;

	if ((bits & HOST_RECORD)) {
		bits &= ~HOST_RECORD;
		sz += 9 + (bits ? 1 : 0);
	}
	if ((bits & HOST_SLANT_VERSION)) {
		bits &= ~HOST_SLANT_VERSION;
		sz += 8 + (bits ? 1 : 0);
	}
	if ((bits & HOST_UPTIME)) {
		bits &= ~HOST_UPTIME;
		sz += 10 + (bits ? 1 : 0);
	}
	if ((bits & HOST_CLOCK_DRIFT)) {
		bits &= ~HOST_CLOCK_DRIFT;
		sz += 9 + (bits ? 1 : 0);
	}
	if ((bits & HOST_MACHINE)) {
		bits &= ~HOST_MACHINE;
		sz += d->maxmachsz + (bits ? 1 : 0);
	}
	if ((bits & HOST_OSVERSION)) {
		bits &= ~HOST_OSVERSION;
		sz += d->maxosversz + (bits ? 1 : 0);
	}
	if ((bits & HOST_OSRELEASE)) {
		bits &= ~HOST_OSRELEASE;
		sz += d->maxosrelsz + (bits ? 1 : 0);
	}
	if ((bits & HOST_OSSYSNAME)) {
		bits &= ~HOST_OSSYSNAME;
		sz += d->maxosnamesz + (bits ? 1 : 0);
	}

	assert(bits == 0);
	return sz;
}

/*
 * Get column widths of the link box.
 * Used with draw_link().
 */
static size_t
size_link(const struct draw *d, unsigned int bits)
{
	size_t	 sz = 0;

	if (LINK_IP & bits) {
		bits &= ~LINK_IP;
		sz += d->maxipsz + ((bits & LINK_STATE) ? 1 : 0);
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
 * Draw a long-term time span in days, hours, minutes.
 */
static void
draw_elapsed_longterm(WINDOW *win, time_t span)
{
	time_t	 day, hr, min;

	if (span < 0)
		span = 0;

	day = span / (24 * 60 * 60);
	span -= day * 24 * 60 * 60;

	hr = span / (60 * 60);
	span -= hr * 60 * 60;

	min = span / 60;
	span -= min * 60;
	wprintw(win, "%3lldd%.2lldh%.2lldm", 
		(long long)day, (long long)hr, 
		(long long)min);
}

/*
 * Draw a time difference "span", which is [+-]hh:mm:ss.
 * Colour it red if >60 seconds, yellow if >30 seconds.
 */
static void
draw_time_diff(struct out *out, time_t span)
{
	time_t	 hr, min, sv;
	int	 minus = span < 0;
	char	 hrbuf[4];

	sv = span = llabs(span);

	if (sv > 60)
		wattron(out->mainwin, A_BOLD | COLOR_PAIR(2));
	else if (sv > 30)
		wattron(out->mainwin, A_BOLD | COLOR_PAIR(1));

	hr = span / (60 * 60);
	span -= hr * 60 * 60;
	min = span / 60;
	span -= min * 60;

	/* Truncate at 99 hours for two-digit hours. */

	if (hr > 99)
		hr = 99;

	snprintf(hrbuf, sizeof(hrbuf), "%s%lld", 
		0 == sv ? " " : (minus ? "-" : "+"),
		(long long)hr);
	wprintw(out->mainwin, "%3s:%.2lld:%.2lld", 
		hrbuf, (long long)min, (long long)span);

	if (sv > 60)
		wattroff(out->mainwin, A_BOLD | COLOR_PAIR(2));
	else if (sv > 30)
		wattroff(out->mainwin, A_BOLD | COLOR_PAIR(1));
}

/*
 * Draw a time span in hours, minutes, seconds.
 */
static void
draw_elapsed(WINDOW *win, time_t span)
{
	time_t	 hr, min;

	if (span < 0)
		span = 0;

	hr = span / (60 * 60);
	span -= hr * 60 * 60;
	min = span / 60;
	span -= min * 60;
	wprintw(win, "%3lld:%.2lld:%.2lld", 
		(long long)hr, (long long)min, 
		(long long)span);
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
	time_t	 ospan, span;
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

	draw_elapsed(win, span);

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
draw_host(unsigned int bits, const struct draw *d,
	time_t timeo, time_t t, struct out *out, const struct node *n, 
	size_t *lastrecord, struct drawbox *box)
{
	int	 x, y;

	*lastrecord = 0;

	if ((bits & HOST_RECORD)) {
		bits &= ~HOST_RECORD;
		getyx(out->mainwin, y, x);
		*lastrecord = x;
		draw_interval(out->mainwin, 15, 
			n->waittime, get_last(n), t);
		if (bits)
			waddch(out->mainwin, ' ');
	}
	if ((bits & HOST_SLANT_VERSION)) {
		bits &= ~HOST_SLANT_VERSION;
		if (NULL == n->recs)
			waddstr(out->mainwin, "--------");
		else
			wprintw(out->mainwin, "%8s", n->recs->version);
		if (bits)
			waddch(out->mainwin, ' ');
	}
	if ((bits & HOST_UPTIME)) {
		bits &= ~HOST_UPTIME;
		if (NULL == n->recs) 
			waddstr(out->mainwin, "---d--h--m");
		else
			draw_elapsed_longterm(out->mainwin, 
				t - n->recs->system.boot);
		if (bits)
			waddch(out->mainwin, ' ');
	}
	if ((bits & HOST_CLOCK_DRIFT)) {
		bits &= ~HOST_CLOCK_DRIFT;
		if (NULL == n->recs || ! n->recs->has_timestamp)
			waddstr(out->mainwin, "---------");
		else
			draw_time_diff(out, n->drift);
		if (bits)
			waddch(out->mainwin, ' ');
	}
	if ((bits & HOST_MACHINE)) {
		bits &= ~HOST_MACHINE;
		if (n->recs == NULL || 
		    !n->recs->has_system ||
		    !n->recs->system.has_machine)
			waddstr(out->mainwin, "---");
		else
			wprintw(out->mainwin, "%*s", (int)d->maxmachsz,
				n->recs->system.machine);
		if (bits)
			waddstr(out->mainwin, " ");
	}
	if ((bits & HOST_OSVERSION)) {
		bits &= ~HOST_OSVERSION;
		if (n->recs == NULL || 
		    !n->recs->has_system ||
		    !n->recs->system.has_osversion)
			waddstr(out->mainwin, "---");
		else
			wprintw(out->mainwin, "%*s", (int)d->maxosversz,
				n->recs->system.osversion);
		if (bits)
			waddstr(out->mainwin, " ");
	}
	if ((bits & HOST_OSRELEASE)) {
		bits &= ~HOST_OSRELEASE;
		if (n->recs == NULL || 
		    !n->recs->has_system ||
		    !n->recs->system.has_osrelease)
			waddstr(out->mainwin, "---");
		else
			wprintw(out->mainwin, "%*s", (int)d->maxosrelsz,
				n->recs->system.osrelease);
		if (bits)
			waddstr(out->mainwin, " ");
	}
	if ((bits & HOST_OSSYSNAME)) {
		bits &= ~HOST_OSSYSNAME;
		if (n->recs == NULL || 
		    !n->recs->has_system ||
		    !n->recs->system.has_sysname)
			waddstr(out->mainwin, "---");
		else
			wprintw(out->mainwin, "%*s", (int)d->maxosnamesz,
				n->recs->system.sysname);
		if (bits)
			waddstr(out->mainwin, " ");
	}

	assert(0 == bits);
}

static void
draw_link(unsigned int bits, const struct draw *d, 
	time_t timeo, time_t t, WINDOW *win, const struct node *n, 
	size_t *lastseen, struct drawbox *box)
{
	int	 x, y;

	*lastseen = 0;

	if (LINK_IP & bits) {
		bits &= ~LINK_IP;
		wprintw(win, "%*s", (int)d->maxipsz,
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

DEFINE_draw_pcts(draw_files, nfiles, draw_pct)

DEFINE_draw_pcts(draw_procs, nprocs, draw_pct)

DEFINE_draw_pcts(draw_rprocs, rprocs, draw_rpct)

DEFINE_draw_pcts(draw_mem, mem, draw_pct)

DEFINE_draw_pcts(draw_cpu, cpu, draw_pct)

DEFINE_draw_rates(draw_net, netrx, nettx, draw_xfer)

DEFINE_draw_rates(draw_disc, discread, discwrite, draw_xfer)

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
 * Compute the width of a give box with its bits.
 * FIXME: this can somehow be merged with the header code.
 */
static size_t
compute_width_box(const struct drawbox *box, unsigned int bits,
	const struct draw *d)
{
	size_t	 sz = 0;

	if (0 == bits)
		return 0;

	/* Leading bar plus padding. */

	sz += 3;

	switch (box->cat) {
	case DRAWCAT_CPU:
	case DRAWCAT_FILES:
	case DRAWCAT_MEM:
	case DRAWCAT_PROCS:
	case DRAWCAT_RPROCS:
		sz += size_pct(bits);
		break;
	case DRAWCAT_DISC:
	case DRAWCAT_NET:
		sz += size_rate(bits);
		break;
	case DRAWCAT_LINK:
		sz += size_link(d, bits);
		break;
	case DRAWCAT_HOST:
		sz += size_host(d, bits);
		break;
	}

	return sz;
}

/* 
 * We'll need to recompute our known column widths if any dynamic data
 * has changed.
 * FIXME: this whole section is quite expensive and should cache
 * information more intelligently.
 */
static void
compute_max_dyncol(struct draw *d, const struct node *n, size_t nsz)
{
	size_t	 i, sz;

	/* Start with the hostname (has a default size). */

	for (d->maxhostsz = strlen("hostname"), i = 0; i < nsz; i++)
		if ((sz = strlen(n[i].host)) > d->maxhostsz)
			d->maxhostsz = sz;

	/* IP address. */

	for (d->maxipsz = i = 0; i < nsz; i++) {
		sz = strlen(n[i].addrs.addrs[n[i].addrs.curaddr].ip);
		if (sz > d->maxipsz)
			d->maxipsz = sz;
	}

	/* Machine. */

	for (d->maxmachsz = 3, i = 0; i < nsz; i++)
		if (n[i].recs != NULL && n[i].recs->has_system) {
			sz = n[i].recs->system.has_machine ?
				strlen(n[i].recs->system.machine) : 0;
			if (sz > d->maxmachsz)
				d->maxmachsz = sz;
		}

	/* OS version. */

	for (d->maxosversz = 3, i = 0; i < nsz; i++)
		if (n[i].recs != NULL && n[i].recs->has_system) {
			sz = n[i].recs->system.has_osversion ?
				strlen(n[i].recs->system.osversion) : 0;
			if (sz > d->maxosversz)
				d->maxosversz = sz;
		}

	/* OS release. */

	for (d->maxosrelsz = 3, i = 0; i < nsz; i++)
		if (n[i].recs != NULL && n[i].recs->has_system) {
			sz = n[i].recs->system.has_osrelease ?
				strlen(n[i].recs->system.osrelease) : 0;
			if (sz > d->maxosrelsz)
				d->maxosrelsz = sz;
		}

	/* OS name. */

	for (d->maxosnamesz = 3, i = 0; i < nsz; i++)
		if (n[i].recs != NULL && n[i].recs->has_system) {
			sz = n[i].recs->system.has_sysname ?
				strlen(n[i].recs->system.sysname) : 0;
			if (sz > d->maxosnamesz)
				d->maxosnamesz = sz;
		}
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
	size_t	 	sz, i, j, line, maxline;
	struct draw	dummy;

	memset(&dummy, 0, sizeof(struct draw));
	compute_max_dyncol(&dummy, n, nsz);

	/* Look for the maximum length of all lines. */

	for (sz = dummy.maxhostsz + 1, i = 0; i < d->boxsz; i++) {
		maxline = 0;
		for (j = 0; j < 6; j++) {
			line = compute_width_box(&d->box[i], 
				 d->box[i].lines[j].line, &dummy);
			if (line > maxline)
				maxline = line;
		}
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
	size_t	 i;

	draw_main_separator(out->mainwin);
	waddch(out->mainwin, ' ');

	/* First, compute all box widths. */

	box->len = 0;
	switch (box->cat) {
	case DRAWCAT_CPU:
	case DRAWCAT_FILES:
	case DRAWCAT_MEM:
	case DRAWCAT_PROCS:
	case DRAWCAT_RPROCS:
		for (i = 0; i < 6; i++) {
			box->lines[i].len = 
				size_pct(box->lines[i].line);
			if (box->lines[i].len > box->len)
				box->len = box->lines[i].len;
		}
		break;
	case DRAWCAT_DISC:
	case DRAWCAT_NET:
		for (i = 0; i < 6; i++) {
			box->lines[i].len = 
				size_rate(box->lines[i].line);
			if (box->lines[i].len > box->len)
				box->len = box->lines[i].len;
		}
		break;
	case DRAWCAT_LINK:
		for (i = 0; i < 6; i++) {
			box->lines[i].len = size_link
				(d, box->lines[i].line);
			if (box->lines[i].len > box->len)
				box->len = box->lines[i].len;
		}
		break;
	case DRAWCAT_HOST:
		for (i = 0; i < 6; i++) {
			box->lines[i].len = size_host
				(d, box->lines[i].line);
			if (box->lines[i].len > box->len)
				box->len = box->lines[i].len;
		}
		break;
	}

	/* Next, write our header within the maximum space. */

	switch (box->cat) {
	case DRAWCAT_CPU:
		draw_centre(out->mainwin, "cpu", box->len);
		break;
	case DRAWCAT_FILES:
		draw_centre(out->mainwin, "files", box->len);
		break;
	case DRAWCAT_MEM:
		draw_centre(out->mainwin, "mem", box->len);
		break;
	case DRAWCAT_PROCS:
		draw_centre(out->mainwin, "procs", box->len);
		break;
	case DRAWCAT_RPROCS:
		if (box->len < 9)
			draw_centre(out->mainwin, 
				"run", box->len);
		else
			draw_centre(out->mainwin, 
				"running", box->len);
		break;
	case DRAWCAT_NET:
		if (box->len < 12)
			draw_centre(out->mainwin, 
				"inet", box->len);
		else
			draw_centre(out->mainwin, 
				"inet rx:tx", box->len);
		break;
	case DRAWCAT_DISC:
		if (box->len < 12)
			draw_centre(out->mainwin, 
				"disc r:w", box->len);
		else
			draw_centre(out->mainwin, 
				"disc rd:wr", box->len);
		break;
	case DRAWCAT_LINK:
		if (box->len < 12)
			draw_centre(out->mainwin, 
				"link", box->len);
		else
			draw_centre(out->mainwin, 
				"link state", box->len);
		break;
	case DRAWCAT_HOST:
		if (box->len < 12)
			wprintw(out->mainwin, "%*s", 
				(int)box->len, "host");
		else
			wprintw(out->mainwin, "%*s", 
				(int)box->len, "host state");
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
		draw_link(bits, d, n->waittime, t, 
			out->mainwin, n, lastseen, box);
		break;
	case DRAWCAT_HOST:
		draw_host(bits, d, n->waittime, t,
			out, n, lastrecord, box);
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
draw(struct out *out, struct draw *d,
	const struct node *n, size_t nsz, time_t t)
{
	size_t		 i, j, k, l;
	int		 maxy, maxx;

	/* Don't let us run off the window. */

	getmaxyx(out->mainwin, maxy, maxx);
	if (nsz * d->maxline > (size_t)maxy - 1)
		nsz = (maxy - 1) / d->maxline;

	compute_max_dyncol(d, n, nsz);

	if (d->header)
		draw_header(out, d);

	for (i = j = 0; i < nsz; i++, j += d->maxline) {
		wmove(out->mainwin, j + d->header, 1);
		wattron(out->mainwin, A_BOLD);
		wprintw(out->mainwin, "%*s", 
			(int)d->maxhostsz, n[i].host);
		wattroff(out->mainwin, A_BOLD);
		waddch(out->mainwin, ' ');

		for (l = 0; l < d->maxline; l++)  {
			wmove(out->mainwin, 
				j + l + d->header, 
				d->maxhostsz + 2);
			wclrtoeol(out->mainwin);
			for (k = 0; k < d->boxsz; k++)
				draw_box(out, &n[i], &d->box[k], 
					d->box[k].lines[l].line, 
					&d->box[k].lines[l].lastseen, 
					&d->box[k].lines[l].lastrecord, 
					d->box[k].lines[l].len,
					d, t);
		}
	}
}

/*
 * If we have no new data but one second has elapsed, then redraw the
 * interval for all second-level timers.
 * We do this by overwriting only that data, which reduces screen update
 * and keeps our display running tight.
 */
void
drawtimes(struct out *out, const struct draw *d, 
	const struct node *n, size_t nsz, time_t t)
{
	size_t	 i, j, k, l;
	int	 maxy, maxx;

	/* Don't let us run off the window. */

	getmaxyx(out->mainwin, maxy, maxx);
	if (nsz * d->maxline > (size_t)maxy - 1)
		nsz = (maxy - 1) / d->maxline;

	/*
	 * For each box, look through the maximum lines and see if they
	 * have second-granularity times (lastseen, lastrecord).
	 * If they do, then update each host.
	 */

	for (i = 0; i < d->boxsz; i++) { 
		for (l = 0; l < d->maxline; l++)  {
			if (0 == d->box[i].lines[l].lastseen)
				continue;
			for (k = j = 0; j < nsz; j++, k += d->maxline) {
				wmove(out->mainwin, 
					k + l + d->header, 
					d->box[i].lines[l].lastseen);
				draw_interval(out->mainwin, 
					n[j].waittime, n[j].waittime, 
					n[j].lastseen, t);
			}
		}
		for (l = 0; l < d->maxline; l++) {
			if (0 == d->box[i].lines[l].lastrecord) 
				continue;
			for (k = j = 0; j < nsz; j++, k += d->maxline) {
				wmove(out->mainwin, 
					k + l + d->header, 
					d->box[i].lines[l].lastrecord);
				draw_interval(out->mainwin, 15, 
					n[j].waittime, 
					get_last(&n[j]), t);
			}
		}
	}
}
