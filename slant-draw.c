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
 * If the time is greater than 60 seconds, draw it as yellow; if more
 * than 120 seconds, draw as red.
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
			if (ospan >= waittime * 4)
				wattron(win, attrs = b2);
			else if (ospan >= waittime * 2)
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
	time_t t, WINDOW *win, const struct node *n, size_t *lastseen)
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
		*lastseen= x;
		draw_interval(win, timeo, timeo, n->lastseen, t);
	}

	assert(0 == bits);
}

static void
draw_disc(unsigned int bits, WINDOW *win, const struct node *n)
{
	double	 vv;

	if (DISC_QMIN & bits) {
		bits &= ~DISC_QMIN;
		if (NULL != n->recs &&
		    n->recs->byqminsz &&
		    n->recs->byqmin[0].entries) {
			vv = n->recs->byqmin[0].discread /
				(double)n->recs->byqmin[0].entries;
			wattron(win, A_BOLD);
			draw_xfer(win, vv, 0);
			wattroff(win, A_BOLD);
			waddch(win, ':');
			vv = n->recs->byqmin[0].discwrite /
				(double)n->recs->byqmin[0].entries;
			wattron(win, A_BOLD);
			draw_xfer(win, vv, 1);
			wattroff(win, A_BOLD);
		} else
			waddstr(win, "------:------");
		if (bits)
			draw_sub_separator(win);
	}

	if (DISC_MIN & bits) {
		bits &= ~DISC_MIN;
		if (NULL != n->recs &&
		    n->recs->byminsz &&
		    n->recs->bymin[0].entries) {
			vv = n->recs->bymin[0].discread /
				(double)n->recs->bymin[0].entries;
			draw_xfer(win, vv, 0);
			waddch(win, ':');
			vv = n->recs->bymin[0].discwrite /
				(double)n->recs->bymin[0].entries;
			draw_xfer(win, vv, 1);
		} else
			waddstr(win, "------:------");
		if (bits)
			draw_sub_separator(win);
	}

	if (DISC_HOUR & bits) {
		bits &= ~DISC_HOUR;
		if (NULL != n->recs &&
		    n->recs->byhoursz &&
		    n->recs->byhour[0].entries) {
			vv = n->recs->byhour[0].discread /
				(double)n->recs->byhour[0].entries;
			draw_xfer(win, vv, 0);
			waddch(win, ':');
			vv = n->recs->byhour[0].discwrite /
				(double)n->recs->byhour[0].entries;
			draw_xfer(win, vv, 1);
		} else
			waddstr(win, "------:------");
		if (bits)
			draw_sub_separator(win);
	}

	if (DISC_DAY & bits) {
		bits &= ~DISC_DAY;
		if (NULL != n->recs &&
		    n->recs->bydaysz &&
		    n->recs->byday[0].entries) {
			vv = n->recs->byday[0].discread /
				(double)n->recs->byday[0].entries;
			draw_xfer(win, vv, 0);
			waddch(win, ':');
			vv = n->recs->byday[0].discwrite /
				(double)n->recs->byday[0].entries;
			draw_xfer(win, vv, 1);
		} else
			waddstr(win, "------:------");
	}

	if (DISC_WEEK & bits) {
		bits &= ~DISC_WEEK;
		if (NULL != n->recs &&
		    n->recs->byweeksz &&
		    n->recs->byweek[0].entries) {
			vv = n->recs->byweek[0].discread /
				(double)n->recs->byweek[0].entries;
			draw_xfer(win, vv, 0);
			waddch(win, ':');
			vv = n->recs->byweek[0].discwrite /
				(double)n->recs->byweek[0].entries;
			draw_xfer(win, vv, 1);
		} else
			waddstr(win, "------:------");
	}

	if (DISC_YEAR & bits) {
		bits &= ~DISC_YEAR;
		if (NULL != n->recs &&
		    n->recs->byyearsz &&
		    n->recs->byyear[0].entries) {
			vv = n->recs->byyear[0].discread /
				(double)n->recs->byyear[0].entries;
			draw_xfer(win, vv, 0);
			waddch(win, ':');
			vv = n->recs->byyear[0].discwrite /
				(double)n->recs->byyear[0].entries;
			draw_xfer(win, vv, 1);
		} else
			waddstr(win, "------:------");
	}

	assert(0 == bits);
}

static void
draw_inet(unsigned int bits, WINDOW *win, const struct node *n)
{
	double	 vv;

	if (NET_QMIN & bits) {
		bits &= ~NET_QMIN;
		if (NULL != n->recs &&
		    n->recs->byqminsz &&
		    n->recs->byqmin[0].entries) {
			vv = n->recs->byqmin[0].netrx /
				(double)n->recs->byqmin[0].entries;
			wattron(win, A_BOLD);
			draw_xfer(win, vv, 0);
			wattroff(win, A_BOLD);
			waddch(win, ':');
			vv = n->recs->byqmin[0].nettx /
				(double)n->recs->byqmin[0].entries;
			wattron(win, A_BOLD);
			draw_xfer(win, vv, 1);
			wattroff(win, A_BOLD);
		} else
			waddstr(win, "------:------");
		if (bits)
			draw_sub_separator(win);
	}

	if (NET_MIN & bits) {
		bits &= ~NET_MIN;
		if (NULL != n->recs &&
		    n->recs->byminsz &&
		    n->recs->bymin[0].entries) {
			vv = n->recs->bymin[0].netrx /
				(double)n->recs->bymin[0].entries;
			draw_xfer(win, vv, 0);
			waddch(win, ':');
			vv = n->recs->bymin[0].nettx /
				(double)n->recs->bymin[0].entries;
			draw_xfer(win, vv, 1);
		} else
			waddstr(win, "------:------");
		if (bits)
			draw_sub_separator(win);
	}

	if (NET_HOUR & bits) {
		bits &= ~NET_HOUR;
		if (NULL != n->recs &&
		    n->recs->byhoursz &&
		    n->recs->byhour[0].entries) {
			vv = n->recs->byhour[0].netrx /
				(double)n->recs->byhour[0].entries;
			draw_xfer(win, vv, 0);
			waddch(win, ':');
			vv = n->recs->byhour[0].nettx /
				(double)n->recs->byhour[0].entries;
			draw_xfer(win, vv, 1);
		} else
			waddstr(win, "------:------");
		if (bits)
			draw_sub_separator(win);
	}

	if (NET_DAY & bits) {
		bits &= ~NET_DAY;
		if (NULL != n->recs &&
		    n->recs->bydaysz &&
		    n->recs->byday[0].entries) {
			vv = n->recs->byday[0].netrx /
				(double)n->recs->byday[0].entries;
			draw_xfer(win, vv, 0);
			waddch(win, ':');
			vv = n->recs->byday[0].nettx /
				(double)n->recs->byday[0].entries;
			draw_xfer(win, vv, 1);
		} else
			waddstr(win, "------:------");
	}

	if (NET_WEEK & bits) {
		bits &= ~NET_WEEK;
		if (NULL != n->recs &&
		    n->recs->byweeksz &&
		    n->recs->byweek[0].entries) {
			vv = n->recs->byweek[0].netrx /
				(double)n->recs->byweek[0].entries;
			draw_xfer(win, vv, 0);
			waddch(win, ':');
			vv = n->recs->byweek[0].nettx /
				(double)n->recs->byweek[0].entries;
			draw_xfer(win, vv, 1);
		} else
			waddstr(win, "------:------");
	}

	if (NET_YEAR & bits) {
		bits &= ~NET_YEAR;
		if (NULL != n->recs &&
		    n->recs->byyearsz &&
		    n->recs->byyear[0].entries) {
			vv = n->recs->byyear[0].netrx /
				(double)n->recs->byyear[0].entries;
			draw_xfer(win, vv, 0);
			waddch(win, ':');
			vv = n->recs->byyear[0].nettx /
				(double)n->recs->byyear[0].entries;
			draw_xfer(win, vv, 1);
		} else
			waddstr(win, "------:------");
	}

	assert(0 == bits);
}

static void
draw_rprocs(unsigned int bits, WINDOW *win, const struct node *n)
{
	double	 vv;
	const struct recset *r = n->recs;

	if (RPROCS_QMIN & bits) {
		bits &= ~RPROCS_QMIN;
		if (NULL != r && r->byqminsz && r->byqmin[0].entries) {
			vv = r->byqmin[0].rprocs / r->byqmin[0].entries;
			wattron(win, A_BOLD);
			draw_rpct(win, vv);
			wattroff(win, A_BOLD);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " ");
		} else 
			wprintw(win, "------%");
		if (bits)
			draw_sub_separator(win);
	}

	if (RPROCS_MIN & bits) {
		bits &= ~RPROCS_MIN;
		if (NULL != r && r->byminsz && r->bymin[0].entries) {
			vv = r->bymin[0].rprocs / r->bymin[0].entries;
			draw_rpct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
		if (bits)
			draw_sub_separator(win);
	}

	if (RPROCS_HOUR & bits) {
		bits &= ~PROCS_HOUR;
		if (NULL != r && r->byhoursz && r->byhour[0].entries) {
			vv = r->byhour[0].rprocs / r->byhour[0].entries;
			draw_rpct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
		if (bits)
			draw_sub_separator(win);
	}

	if (RPROCS_DAY & bits) {
		bits &= ~PROCS_DAY;
		if (NULL != r && r->bydaysz && r->byday[0].entries) {
			vv = r->byday[0].rprocs / r->byday[0].entries;
			draw_rpct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
	}

	if (RPROCS_WEEK & bits) {
		bits &= ~PROCS_WEEK;
		if (NULL != r && r->byweeksz && r->byweek[0].entries) {
			vv = r->byweek[0].rprocs / r->byweek[0].entries;
			draw_rpct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
	}

	if (RPROCS_YEAR & bits) {
		bits &= ~PROCS_YEAR;
		if (NULL != r && r->byyearsz && r->byyear[0].entries) {
			vv = r->byyear[0].rprocs / r->byyear[0].entries;
			draw_rpct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
	}

	assert(0 == bits);
}

static void
draw_procs(unsigned int bits, WINDOW *win, const struct node *n)
{
	double	 vv;
	const struct recset *r = n->recs;

	if (PROCS_QMIN_BARS & bits) {
		bits &= ~PROCS_QMIN_BARS;
		if (NULL != r && r->byqminsz && r->byqmin[0].entries) {
			vv = r->byqmin[0].nprocs / r->byqmin[0].entries;
			draw_bars(win, vv);
		} else
			wprintw(win, "%10s", " ");
		if (bits)
			waddch(win, ' ');
	}

	if (PROCS_QMIN & bits) {
		bits &= ~PROCS_QMIN;
		if (NULL != r && r->byqminsz && r->byqmin[0].entries) {
			vv = r->byqmin[0].nprocs / r->byqmin[0].entries;
			wattron(win, A_BOLD);
			draw_pct(win, vv);
			wattroff(win, A_BOLD);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " ");
		} else 
			wprintw(win, "------%");
		if (bits)
			draw_sub_separator(win);
	}

	if (PROCS_MIN & bits) {
		bits &= ~PROCS_MIN;
		if (NULL != r && r->byminsz && r->bymin[0].entries) {
			vv = r->bymin[0].nprocs / r->bymin[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
		if (bits)
			draw_sub_separator(win);
	}

	if (PROCS_HOUR & bits) {
		bits &= ~PROCS_HOUR;
		if (NULL != r && r->byhoursz && r->byhour[0].entries) {
			vv = r->byhour[0].nprocs / r->byhour[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
		if (bits)
			draw_sub_separator(win);
	}

	if (PROCS_DAY & bits) {
		bits &= ~PROCS_DAY;
		if (NULL != r && r->bydaysz && r->byday[0].entries) {
			vv = r->byday[0].nprocs / r->byday[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
	}

	if (PROCS_WEEK & bits) {
		bits &= ~PROCS_WEEK;
		if (NULL != r && r->byweeksz && r->byweek[0].entries) {
			vv = r->byweek[0].nprocs / r->byweek[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
	}

	if (PROCS_YEAR & bits) {
		bits &= ~PROCS_YEAR;
		if (NULL != r && r->byyearsz && r->byyear[0].entries) {
			vv = r->byyear[0].nprocs / r->byyear[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
	}

	assert(0 == bits);
}

static void
draw_mem(unsigned int bits, WINDOW *win, const struct node *n)
{
	double	 vv;

	if (MEM_QMIN_BARS & bits) {
		bits &= ~MEM_QMIN_BARS;
		if (NULL != n->recs &&
		    n->recs->byqminsz &&
		    n->recs->byqmin[0].entries) {
			vv = n->recs->byqmin[0].mem /
				n->recs->byqmin[0].entries;
			draw_bars(win, vv);
		} else
			wprintw(win, "%10s", " ");
		if (bits)
			waddch(win, ' ');
	}

	if (MEM_QMIN & bits) {
		bits &= ~MEM_QMIN;
		if (NULL != n->recs &&
		    n->recs->byqminsz &&
		    n->recs->byqmin[0].entries) {
			vv = n->recs->byqmin[0].mem /
				n->recs->byqmin[0].entries;
			wattron(win, A_BOLD);
			draw_pct(win, vv);
			wattroff(win, A_BOLD);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " ");
		} else 
			wprintw(win, "------%");
		if (bits)
			draw_sub_separator(win);
	}

	if (MEM_MIN & bits) {
		bits &= ~MEM_MIN;
		if (NULL != n->recs &&
		    n->recs->byminsz &&
		    n->recs->bymin[0].entries) {
			vv = n->recs->bymin[0].mem /
				n->recs->bymin[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
		if (bits)
			draw_sub_separator(win);
	}

	if (MEM_HOUR & bits) {
		bits &= ~MEM_HOUR;
		if (NULL != n->recs &&
	 	    n->recs->byhoursz &&
		    n->recs->byhour[0].entries) {
			vv = n->recs->byhour[0].mem /
				n->recs->byhour[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
		if (bits)
			draw_sub_separator(win);
	}

	if (MEM_DAY & bits) {
		bits &= ~MEM_DAY;
		if (NULL != n->recs &&
		    n->recs->bydaysz &&
		    n->recs->byday[0].entries) {
			vv = n->recs->byday[0].mem /
				n->recs->byday[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
	}

	if (MEM_WEEK & bits) {
		bits &= ~MEM_WEEK;
		if (NULL != n->recs &&
		    n->recs->byweeksz &&
		    n->recs->byweek[0].entries) {
			vv = n->recs->byweek[0].mem /
				n->recs->byweek[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
	}

	if (MEM_YEAR & bits) {
		bits &= ~MEM_YEAR;
		if (NULL != n->recs &&
		    n->recs->byyearsz &&
		    n->recs->byyear[0].entries) {
			vv = n->recs->byyear[0].mem /
				n->recs->byyear[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
	}

	assert(0 == bits);
}

static void
draw_cpu(unsigned int bits, WINDOW *win, const struct node *n)
{
	double	 vv;

	if (CPU_QMIN_BARS & bits) {
		bits &= ~CPU_QMIN_BARS;
		if (NULL != n->recs &&
		    n->recs->byqminsz &&
		    n->recs->byqmin[0].entries) {
			vv = n->recs->byqmin[0].cpu /
				n->recs->byqmin[0].entries;
			draw_bars(win, vv);
		} else
			wprintw(win, "%10s", " ");
		if (bits)
			waddch(win, ' ');
	}

	if (CPU_QMIN & bits) {
		bits &= ~CPU_QMIN;
		if (NULL != n->recs &&
		    n->recs->byqminsz &&
		    n->recs->byqmin[0].entries) {
			vv = n->recs->byqmin[0].cpu /
				n->recs->byqmin[0].entries;
			wattron(win, A_BOLD);
			draw_pct(win, vv);
			wattroff(win, A_BOLD);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " ");
		} else 
			wprintw(win, "------%");
		if (bits)
			draw_sub_separator(win);
	}

	if (CPU_MIN & bits) {
		bits &= ~CPU_MIN;
		if (NULL != n->recs &&
		    n->recs->byminsz &&
		    n->recs->bymin[0].entries) {
			vv = n->recs->bymin[0].cpu /
				n->recs->bymin[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
		if (bits)
			draw_sub_separator(win);
	}

	if (CPU_HOUR & bits) {
		bits &= ~CPU_HOUR;
		if (NULL != n->recs &&
	 	    n->recs->byhoursz &&
		    n->recs->byhour[0].entries) {
			vv = n->recs->byhour[0].cpu /
				n->recs->byhour[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
		if (bits)
			draw_sub_separator(win);
	}

	if (CPU_DAY & bits) {
		bits &= ~CPU_DAY;
		if (NULL != n->recs &&
		    n->recs->bydaysz &&
		    n->recs->byday[0].entries) {
			vv = n->recs->byday[0].cpu /
				n->recs->byday[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
	}

	if (CPU_WEEK & bits) {
		bits &= ~CPU_WEEK;
		if (NULL != n->recs &&
		    n->recs->byweeksz &&
		    n->recs->byweek[0].entries) {
			vv = n->recs->byweek[0].cpu /
				n->recs->byweek[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
	}

	if (CPU_YEAR & bits) {
		bits &= ~CPU_YEAR;
		if (NULL != n->recs &&
		    n->recs->byyearsz &&
		    n->recs->byyear[0].entries) {
			vv = n->recs->byyear[0].cpu /
				n->recs->byyear[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
	}

	assert(0 == bits);
}

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

size_t
compute_width(const struct node *n, size_t nsz, 
	const struct draw *d)
{
	size_t	 sz, maxhostsz, maxipsz, i;
	int	 bits;

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

	for (sz = maxhostsz + 1, i = 0; i < d->boxsz; i++) {
		if (0 == d->box[i].args)
			continue;
		bits = d->box[i].args;
		sz += 3;
		switch (d->box[i].cat) {
		case DRAWCAT_CPU:
			if (CPU_QMIN_BARS & bits) {
				bits &= ~CPU_QMIN_BARS;
				sz += 10 + (bits ? 1 : 0);
			}
			if (CPU_QMIN & bits) {
				bits &= ~CPU_QMIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (CPU_MIN & bits) {
				bits &= ~CPU_MIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (CPU_HOUR & bits) {
				bits &= ~CPU_HOUR;
				sz += 6 + (bits ? 1 : 0);
			}
			if (CPU_DAY & bits) {
				bits &= ~CPU_DAY;
				sz += 6;
			}
			if (CPU_WEEK & bits) {
				bits &= ~CPU_WEEK;
				sz += 6;
			}
			if (CPU_YEAR & bits) {
				bits &= ~CPU_YEAR;
				sz += 6;
			}
			assert(0 == bits);
			break;
		case DRAWCAT_MEM:
			if (MEM_QMIN_BARS & bits) {
				bits &= ~MEM_QMIN_BARS;
				sz += 10 + (bits ? 1 : 0);
			}
			if (MEM_QMIN & bits) {
				bits &= ~MEM_QMIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (MEM_MIN & bits) {
				bits &= ~MEM_MIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (MEM_HOUR & bits) {
				bits &= ~MEM_HOUR;
				sz += 6 + (bits ? 1 : 0);
			}
			if (MEM_DAY & bits) {
				bits &= ~MEM_DAY;
				sz += 6;
			}
			if (MEM_WEEK & bits) {
				bits &= ~MEM_WEEK;
				sz += 6;
			}
			if (MEM_YEAR & bits) {
				bits &= ~MEM_YEAR;
				sz += 6;
			}
			assert(0 == bits);
			break;
		case DRAWCAT_PROCS:
			if (PROCS_QMIN_BARS & bits) {
				bits &= ~PROCS_QMIN_BARS;
				sz += 10 + (bits ? 1 : 0);
			}
			if (PROCS_QMIN & bits) {
				bits &= ~PROCS_QMIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (PROCS_MIN & bits) {
				bits &= ~PROCS_MIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (PROCS_HOUR & bits) {
				bits &= ~PROCS_HOUR;
				sz += 6 + (bits ? 1 : 0);
			}
			if (PROCS_DAY & bits) {
				bits &= ~PROCS_DAY;
				sz += 6;
			}
			if (PROCS_WEEK & bits) {
				bits &= ~PROCS_WEEK;
				sz += 6;
			}
			if (PROCS_YEAR & bits) {
				bits &= ~PROCS_YEAR;
				sz += 6;
			}
			assert(0 == bits);
			break;
		case DRAWCAT_RPROCS:
			if (RPROCS_QMIN & bits) {
				bits &= ~RPROCS_QMIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (RPROCS_MIN & bits) {
				bits &= ~RPROCS_MIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (RPROCS_HOUR & bits) {
				bits &= ~RPROCS_HOUR;
				sz += 6 + (bits ? 1 : 0);
			}
			if (RPROCS_DAY & bits) {
				bits &= ~RPROCS_DAY;
				sz += 6;
			}
			assert(0 == bits);
			break;
		case DRAWCAT_NET:
			if (NET_QMIN & bits) {
				bits &= ~NET_QMIN;
				sz += 13 + (bits ? 1 : 0);
			}
			if (NET_MIN & bits) {
				bits &= ~NET_MIN;
				sz += 13 + (bits ? 1 : 0);
			}
			if (NET_HOUR & bits) {
				bits &= ~NET_HOUR;
				sz += 13 + (bits ? 1 : 0);
			}
			if (NET_DAY & bits) {
				bits &= ~NET_DAY;
				sz += 13;
			}
			if (NET_WEEK & bits) {
				bits &= ~NET_WEEK;
				sz += 13;
			}
			if (NET_YEAR & bits) {
				bits &= ~NET_YEAR;
				sz += 13;
			}
			assert(0 == bits);
			break;
		case DRAWCAT_DISC:
			if (DISC_QMIN & bits) {
				bits &= ~DISC_QMIN;
				sz += 13 + (bits ? 1 : 0);
			}
			if (DISC_MIN & bits) {
				bits &= ~DISC_MIN;
				sz += 13 + (bits ? 1 : 0);
			}
			if (DISC_HOUR & bits) {
				bits &= ~DISC_HOUR;
				sz += 13 + (bits ? 1 : 0);
			}
			if (DISC_DAY & bits) {
				bits &= ~DISC_DAY;
				sz += 13;
			}
			if (DISC_WEEK & bits) {
				bits &= ~DISC_WEEK;
				sz += 13;
			}
			if (DISC_YEAR & bits) {
				bits &= ~DISC_YEAR;
				sz += 13;
			}
			assert(0 == bits);
			break;
		case DRAWCAT_LINK:
			if (LINK_IP & bits) {
				bits &= ~LINK_IP;
				sz += maxipsz + 
					((bits & LINK_STATE) ? 1 : 0);
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
			break;
		case DRAWCAT_HOST:
			/* "Last" time. */
			sz += 9;
			break;
		}
	}

	return sz;
}

static void
draw_header(struct out *out, const struct draw *d, 
	size_t maxhostsz, size_t maxipsz)
{
	size_t	 i, sz;
	int	 bits;

	wmove(out->mainwin, 0, 1);
	wclrtoeol(out->mainwin);
	wprintw(out->mainwin, "%*s", (int)maxhostsz, "hostname");
	waddch(out->mainwin, ' ');

	for (i = 0; i < d->boxsz; i++) {
		if (0 == (bits = d->box[i].args))
			continue;
		sz = 0;
		draw_main_separator(out->mainwin);
		waddch(out->mainwin, ' ');
		switch (d->box[i].cat) {
		case DRAWCAT_CPU:
			if (CPU_QMIN_BARS & bits) {
				bits &= ~CPU_QMIN_BARS;
				sz += 10 + (bits ? 1 : 0);
			}
			if (CPU_QMIN & bits) {
				bits &= ~CPU_QMIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (CPU_MIN & bits) {
				bits &= ~CPU_MIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (CPU_HOUR & bits) {
				bits &= ~CPU_HOUR;
				sz += 6 + (bits ? 1 : 0);
			}
			if (CPU_DAY & bits) {
				bits &= ~CPU_DAY;
				sz += 6;
			}
			if (CPU_WEEK & bits) {
				bits &= ~CPU_WEEK;
				sz += 6;
			}
			if (CPU_YEAR & bits) {
				bits &= ~CPU_YEAR;
				sz += 6;
			}
			assert(0 == bits);
			draw_centre(out->mainwin, "cpu", sz);
			break;
		case DRAWCAT_MEM:
			if (MEM_QMIN_BARS & bits) {
				bits &= ~MEM_QMIN_BARS;
				sz += 10 + (bits ? 1 : 0);
			}
			if (MEM_QMIN & bits) {
				bits &= ~MEM_QMIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (MEM_MIN & bits) {
				bits &= ~MEM_MIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (MEM_HOUR & bits) {
				bits &= ~MEM_HOUR;
				sz += 6 + (bits ? 1 : 0);
			}
			if (MEM_DAY & bits) {
				bits &= ~MEM_DAY;
				sz += 6;
			}
			if (MEM_WEEK & bits) {
				bits &= ~MEM_WEEK;
				sz += 6;
			}
			if (MEM_YEAR & bits) {
				bits &= ~MEM_YEAR;
				sz += 6;
			}
			assert(0 == bits);
			draw_centre(out->mainwin, "mem", sz);
			break;
		case DRAWCAT_PROCS:
			if (PROCS_QMIN_BARS & bits) {
				bits &= ~PROCS_QMIN_BARS;
				sz += 10 + (bits ? 1 : 0);
			}
			if (PROCS_QMIN & bits) {
				bits &= ~PROCS_QMIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (PROCS_MIN & bits) {
				bits &= ~PROCS_MIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (PROCS_HOUR & bits) {
				bits &= ~PROCS_HOUR;
				sz += 6 + (bits ? 1 : 0);
			}
			if (PROCS_DAY & bits) {
				bits &= ~PROCS_DAY;
				sz += 6;
			}
			if (PROCS_WEEK & bits) {
				bits &= ~PROCS_WEEK;
				sz += 6;
			}
			if (PROCS_YEAR & bits) {
				bits &= ~PROCS_YEAR;
				sz += 6;
			}
			assert(0 == bits);
			draw_centre(out->mainwin, "procs", sz);
			break;
		case DRAWCAT_RPROCS:
			if (RPROCS_QMIN & bits) {
				bits &= ~RPROCS_QMIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (RPROCS_MIN & bits) {
				bits &= ~RPROCS_MIN;
				sz += 6 + (bits ? 1 : 0);
			}
			if (RPROCS_HOUR & bits) {
				bits &= ~RPROCS_HOUR;
				sz += 6 + (bits ? 1 : 0);
			}
			if (RPROCS_DAY & bits) {
				bits &= ~RPROCS_DAY;
				sz += 6;
			}
			if (RPROCS_WEEK & bits) {
				bits &= ~RPROCS_WEEK;
				sz += 6;
			}
			if (RPROCS_YEAR & bits) {
				bits &= ~RPROCS_YEAR;
				sz += 6;
			}
			assert(0 == bits);
			if (sz < 9) {
				draw_centre(out->mainwin, "run", sz);
				break;
			}
			draw_centre(out->mainwin, "running", sz);
			break;
		case DRAWCAT_NET:
			if (NET_QMIN & bits) {
				bits &= ~NET_QMIN;
				sz += 13 + (bits ? 1 : 0);
			}
			if (NET_MIN & bits) {
				bits &= ~NET_MIN;
				sz += 13 + (bits ? 1 : 0);
			}
			if (NET_HOUR & bits) {
				bits &= ~NET_HOUR;
				sz += 13 + (bits ? 1 : 0);
			}
			if (NET_DAY & bits) {
				bits &= ~NET_DAY;
				sz += 13;
			}
			if (NET_WEEK & bits) {
				bits &= ~NET_WEEK;
				sz += 13;
			}
			if (NET_YEAR & bits) {
				bits &= ~NET_YEAR;
				sz += 13;
			}
			assert(0 == bits);
			if (sz < 12) {
				draw_centre(out->mainwin, "inet", sz);
				break;
			}
			draw_centre(out->mainwin, "inet rx:tx", sz);
			break;
		case DRAWCAT_DISC:
			if (DISC_QMIN & bits) {
				bits &= ~DISC_QMIN;
				sz += 13 + (bits ? 1 : 0);
			}
			if (DISC_MIN & bits) {
				bits &= ~DISC_MIN;
				sz += 13 + (bits ? 1 : 0);
			}
			if (DISC_HOUR & bits) {
				bits &= ~DISC_HOUR;
				sz += 13 + (bits ? 1 : 0);
			}
			if (DISC_DAY & bits) {
				bits &= ~DISC_DAY;
				sz += 13;
			}
			if (DISC_WEEK & bits) {
				bits &= ~DISC_WEEK;
				sz += 13;
			}
			if (DISC_YEAR & bits) {
				bits &= ~DISC_YEAR;
				sz += 13;
			}
			assert(0 == bits);
			if (sz < 17) {
				draw_centre(out->mainwin, "disc r:w", sz);
				break;
			}
			draw_centre(out->mainwin, "disc read:write", sz);
			break;
		case DRAWCAT_LINK:
			if (LINK_IP & bits) {
				bits &= ~LINK_IP;
				sz += maxipsz + 
					((bits & LINK_STATE) ? 1 : 0);
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
			if (sz < 12) {
				draw_centre(out->mainwin, "link", sz);
				break;
			}
			draw_centre(out->mainwin, "link state", sz);
			break;
		case DRAWCAT_HOST:
			wprintw(out->mainwin, "%9s", "last");
			break;
		}
		waddch(out->mainwin, ' ');
	}
}

void
draw(struct out *out, struct draw *d, int first,
	const struct node *n, size_t nsz, time_t t)
{
	size_t		 i, j, sz, maxhostsz, maxipsz,
			 lastseenpos = 0, intervalpos = 0, chhead;
	int		 x, y, maxy, maxx;
	unsigned int	 bits;

	/* Don't let us run off the window. */

	getmaxyx(out->mainwin, maxy, maxx);
	if (nsz > (size_t)maxy - 1)
		nsz = maxy - 1;

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

	for (i = 0; i < nsz; i++) {
		wmove(out->mainwin, i + d->header, 1);
		wclrtoeol(out->mainwin);
		wattron(out->mainwin, A_BOLD);
		wprintw(out->mainwin, "%*s", (int)maxhostsz, n[i].host);
		wattroff(out->mainwin, A_BOLD);
		waddch(out->mainwin, ' ');

		for (j = 0; j < d->boxsz; j++) {
			if (0 == (bits = d->box[j].args))
				continue;
			draw_main_separator(out->mainwin);
			waddch(out->mainwin, ' ');
			switch (d->box[j].cat) {
			case DRAWCAT_CPU:
				draw_cpu(bits, out->mainwin, &n[i]);
				break;
			case DRAWCAT_MEM:
				draw_mem(bits, out->mainwin, &n[i]);
				break;
			case DRAWCAT_NET:
				draw_inet(bits, out->mainwin, &n[i]);
				break;
			case DRAWCAT_DISC:
				draw_disc(bits, out->mainwin, &n[i]);
				break;
			case DRAWCAT_LINK:
				draw_link(bits, maxipsz, 
					n[i].waittime, t, 
					out->mainwin, &n[i], 
					&lastseenpos);
				break;
			case DRAWCAT_HOST:
				getyx(out->mainwin, y, x);
				intervalpos = x;
				draw_interval(out->mainwin, 15, 
					n[i].waittime, get_last(&n[i]), t);
				break;
			case DRAWCAT_PROCS:
				draw_procs(bits, out->mainwin, &n[i]);
				break;
			case DRAWCAT_RPROCS:
				draw_rprocs(bits, out->mainwin, &n[i]);
				break;
			}
			waddch(out->mainwin, ' ');
		}
	}

	/* Remember for updating times. */
	/* FIXME: muliple lastseen/intervals? */

	chhead = intervalpos != d->intervalpos ||
		lastseenpos != d->lastseenpos;

	d->intervalpos = intervalpos;
	d->lastseenpos = lastseenpos;

	if (d->header && (chhead || first))
		draw_header(out, d, maxhostsz, maxipsz);
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
	size_t	 i;
	int	 maxy, maxx;

	/* Hasn't collected data yet... */

	if (0 == d->intervalpos && 0 == d->lastseenpos)
		return;

	/* Don't let us run off the window. */

	getmaxyx(out->mainwin, maxy, maxx);
	if (nsz > (size_t)maxy - 1)
		nsz = maxy - 1;

	for (i = 0; i < nsz; i++) {
		if (d->intervalpos) {
			wmove(out->mainwin, i + 1, d->intervalpos);
			draw_interval(out->mainwin, 15, 
				n[i].waittime, get_last(&n[i]), t);
		}
		if (d->lastseenpos) {
			wmove(out->mainwin, i + 1, d->lastseenpos);
			draw_interval(out->mainwin, n[i].waittime, 
				n[i].waittime, n[i].lastseen, t);
		}
	}
}
