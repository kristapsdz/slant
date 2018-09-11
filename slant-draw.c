#include <sys/queue.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <curses.h>
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
draw_link(const struct draw *d, size_t maxipsz, time_t timeo,
	time_t t, WINDOW *win, const struct node *n, size_t *lastseen)
{
	int	 x, y, bits = d->box_link;

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
		getyx(win, y, x);
		*lastseen= x;
		draw_interval(win, timeo, timeo, n->lastseen, t);
	}
}

static void
draw_disc(const struct draw *d, WINDOW *win, const struct node *n)
{
	double	 vv;
	int	 bits = d->box_disc;

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
}

static void
draw_inet(const struct draw *d, WINDOW *win, const struct node *n)
{
	double	 vv;
	int	 bits = d->box_net;

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
}

static void
draw_procs(const struct draw *d, WINDOW *win, const struct node *n)
{
	double	 vv;
	int	 bits = d->box_procs;

	if (PROCS_QMIN_BARS & bits) {
		bits &= ~PROCS_QMIN_BARS;
		if (NULL != n->recs &&
		    n->recs->byqminsz &&
		    n->recs->byqmin[0].entries) {
			vv = n->recs->byqmin[0].nprocs /
				n->recs->byqmin[0].entries;
			draw_bars(win, vv);
		} else
			wprintw(win, "%10s", " ");
		if (bits)
			waddch(win, ' ');
	}

	if (PROCS_QMIN & bits) {
		bits &= ~PROCS_QMIN;
		if (NULL != n->recs &&
		    n->recs->byqminsz &&
		    n->recs->byqmin[0].entries) {
			vv = n->recs->byqmin[0].nprocs /
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

	if (PROCS_MIN & bits) {
		bits &= ~PROCS_MIN;
		if (NULL != n->recs &&
		    n->recs->byminsz &&
		    n->recs->bymin[0].entries) {
			vv = n->recs->bymin[0].nprocs /
				n->recs->bymin[0].entries;
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
		if (NULL != n->recs &&
	 	    n->recs->byhoursz &&
		    n->recs->byhour[0].entries) {
			vv = n->recs->byhour[0].nprocs /
				n->recs->byhour[0].entries;
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
		if (NULL != n->recs &&
		    n->recs->bydaysz &&
		    n->recs->byday[0].entries) {
			vv = n->recs->byday[0].nprocs /
				n->recs->byday[0].entries;
			draw_pct(win, vv);
		} else if (NULL != n->recs) {
			wprintw(win, "%6s", " "); 
		} else
			wprintw(win, "------%");
	}
}

static void
draw_mem(const struct draw *d, WINDOW *win, const struct node *n)
{
	double	 vv;
	int	 bits = d->box_mem;

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
}

static void
draw_cpu(const struct draw *d, WINDOW *win, const struct node *n)
{
	double	 vv;
	int	 bits = d->box_cpu;

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

	if (d->box_cpu) {
		bits = d->box_cpu;
		sz += 3;
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
	}

	if (d->box_mem) {
		bits = d->box_mem;
		sz += 3;
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
	}

	if (d->box_procs) {
		bits = d->box_procs;
		sz += 3;
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
	}

	if (d->box_net) {
		bits = d->box_net;
		sz += 3;
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
	}

	if (d->box_disc) {
		bits = d->box_disc;
		sz += 3;
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
	}

	if (d->box_link) {
		bits = d->box_link;
		sz += 3;
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
	}

	if (d->box_host) {
		sz += 2;
		/* "Last" time. */
		sz += 9;
	}

	return sz;
}

static void
draw_header(WINDOW *win, const struct draw *d, 
	size_t maxhostsz, size_t maxipsz)
{
	size_t	 sz;
	int	 bits;

	wmove(win, 0, 1);
	wclrtoeol(win);
	wprintw(win, "%*s", (int)maxhostsz, "hostname");
	waddch(win, ' ');

	if (d->box_cpu) {
		bits = d->box_cpu;
		draw_main_separator(win);
		waddch(win, ' ');
		sz = 0;
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
		draw_centre(win, "cpu", sz);
		waddch(win, ' ');
	}

	if (d->box_mem) {
		bits = d->box_mem;
		draw_main_separator(win);
		waddch(win, ' ');
		sz = 0;
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
		draw_centre(win, "mem", sz);
		waddch(win, ' ');
	}

	if (d->box_procs) {
		bits = d->box_procs;
		draw_main_separator(win);
		waddch(win, ' ');
		sz = 0;
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
		draw_centre(win, "procs", sz);
		waddch(win, ' ');
	}

	if (d->box_net) {
		bits = d->box_net;
		draw_main_separator(win);
		waddch(win, ' ');
		sz = 0;
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
		if (sz < 12)
			draw_centre(win, "inet", sz);
		else
			draw_centre(win, "inet rx:tx", sz);
		waddch(win, ' ');
	}

	if (d->box_disc) {
		bits = d->box_disc;
		draw_main_separator(win);
		waddch(win, ' ');
		sz = 0;
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
		if (sz < 17)
			draw_centre(win, "disc r:w", sz);
		else
			draw_centre(win, "disc read:write", sz);
		waddch(win, ' ');
	}

	if (d->box_link) {
		bits = d->box_link;
		draw_main_separator(win);
		waddch(win, ' ');
		sz = 0;
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
		if (sz < 12)
			draw_centre(win, "link", sz);
		else
			draw_centre(win, "link state", sz);
		waddch(win, ' ');
	}

	if (d->box_host) {
		draw_main_separator(win);
		waddch(win, ' ');
		wprintw(win, "%9s", "last");
	}
}

void
draw(WINDOW *win, struct draw *d, time_t timeo,
	const struct node *n, size_t nsz, time_t t)
{
	size_t	 i, sz, maxhostsz, maxipsz,
		 lastseenpos, intervalpos, chhead;
	int	 x, y, maxy, maxx;

	/* Don't let us run off the window. */

	getmaxyx(win, maxy, maxx);
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
		wmove(win, i + 1, 1);
		wclrtoeol(win);
		wattron(win, A_BOLD);
		wprintw(win, "%*s", (int)maxhostsz, n[i].host);
		wattroff(win, A_BOLD);
		waddch(win, ' ');
		if (d->box_cpu) {
			draw_main_separator(win);
			waddch(win, ' ');
			draw_cpu(d, win, &n[i]);
			waddch(win, ' ');
		}
		if (d->box_mem) {
			draw_main_separator(win);
			waddch(win, ' ');
			draw_mem(d, win, &n[i]);
			waddch(win, ' ');
		}
		if (d->box_procs) {
			draw_main_separator(win);
			waddch(win, ' ');
			draw_procs(d, win, &n[i]);
			waddch(win, ' ');
		}
		if (d->box_net) {
			draw_main_separator(win);
			waddch(win, ' ');
			draw_inet(d, win, &n[i]);
			waddch(win, ' ');
		}
		if (d->box_disc) {
			draw_main_separator(win);
			waddch(win, ' ');
			draw_disc(d, win, &n[i]);
			waddch(win, ' ');
		}
		if (d->box_link) {
			draw_main_separator(win);
			waddch(win, ' ');
			draw_link(d, maxipsz, timeo, t,
				win, &n[i], &lastseenpos);
			waddch(win, ' ');
		} else
			lastseenpos = 0;
		if (d->box_host) {
			draw_main_separator(win);
			waddch(win, ' ');
			getyx(win, y, x);
			intervalpos = x;
			draw_interval(win, 15, 
				timeo, get_last(&n[i]), t);
		} else
			intervalpos = 0;
	}

	/* Remember for updating times. */

	chhead = intervalpos != d->intervalpos ||
		lastseenpos != d->lastseenpos;

	d->intervalpos = intervalpos;
	d->lastseenpos = lastseenpos;

	if (chhead)
		draw_header(win, d, maxhostsz, maxipsz);
}

/*
 * If we have no new data but one second has elapsed, then redraw the
 * interval from last collection and last ping time.
 * We do this by overwriting only that data, which reduces screen update
 * and keeps our display running tight.
 */
void
drawtimes(WINDOW *win, const struct draw *d, time_t timeo,
	const struct node *n, size_t nsz, time_t t)
{
	size_t	 i;
	int	 maxy, maxx;

	/* Hasn't collected data yet... */

	if (0 == d->intervalpos && 0 == d->lastseenpos)
		return;

	/* Don't let us run off the window. */

	getmaxyx(win, maxy, maxx);
	if (nsz > (size_t)maxy - 1)
		nsz = maxy - 1;

	for (i = 0; i < nsz; i++) {
		if (d->intervalpos) {
			wmove(win, i + 1, d->intervalpos);
			draw_interval(win, 15, timeo, get_last(&n[i]), t);
		}
		if (d->lastseenpos) {
			wmove(win, i + 1, d->lastseenpos);
			draw_interval(win, timeo, timeo, n[i].lastseen, t);
		}
	}
}
