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
draw_main_separator(void)
{

	printw("%lc", L'\x2502');
}

/*
 * Separator for sub columns.
 */
static void
draw_sub_separator(void)
{

	printw("%lc", L'\x250a');
}

/*
 * Horizontal bar graph: partial <5%.
 */
static void
draw_bar_light(void)
{

	printw("%lc", L'\x2758');
}

/*
 * Horizontal bar graph: partial >=5%.
 */
static void
draw_bar_medium(void)
{

	printw("%lc", L'\x2759');
}

/*
 * Horizontal bar graph: full 10%.
 */
static void
draw_bar_heavy(void)
{

	printw("%lc", L'\x275A');
}

/*
 * Draw a bar graph of up to vv%, where a bar is 10%.
 * This draws up to <50% as normal colour, >=50% as yellow, then >=80%
 * as a red bar.
 * Partial bars are also shown.
 */
static void
draw_bars(double vv)
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
			attron(COLOR_PAIR(2));
		else if (i >= 5)
			attron(COLOR_PAIR(1));
		draw_bar_heavy();
		if (i >= 8)
			attroff(COLOR_PAIR(2));
		else if (i >= 5)
			attroff(COLOR_PAIR(1));
	}

	/* If we're at 100%, bail. */

	if (i > 10)
		return;

	/*
	 * Now add either an light mark or medium mark depending on how
	 * much is in the remainder.
	 */

	if (i >= 8)
		attron(COLOR_PAIR(2));
	else if (i >= 5)
		attron(COLOR_PAIR(1));

	v = i * 10.0;
	assert(v > vv);
	if (v - vv <= 5.0)
		draw_bar_medium();
	else
		draw_bar_light();
	if (i >= 8)
		attroff(COLOR_PAIR(2));
	else if (i >= 5)
		attroff(COLOR_PAIR(1));

	for (++i; i <= 10; i++)
		addch(' ');
}

/*
 * Draw the percentage attached to a bar graph (or not).
 * More than 50% inclusive gets a yellow colour, more than 80 has red.
 */
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

/*
 * Draw the amount of time elased from "last" to "now", unless "last" is
 * zero, in which case draw something that indicates no time exists.
 * Bound below at zero elapsed time.
 * If the time is greater than 60 seconds, draw it as yellow; if more
 * than 120 seconds, draw as red.
 */
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
draw_disc(const struct node *n)
{
	double	 vv;

	if (NULL != n->recs &&
	    n->recs->byqminsz &&
	    n->recs->byqmin[0].entries) {
		vv = n->recs->byqmin[0].discread /
			(double)n->recs->byqmin[0].entries;
		attron(A_BOLD);
		draw_xfer(vv, 0);
		attroff(A_BOLD);
		addch(':');
		vv = n->recs->byqmin[0].discwrite /
			(double)n->recs->byqmin[0].entries;
		attron(A_BOLD);
		draw_xfer(vv, 1);
		attroff(A_BOLD);
	} else
		addstr("------:------");

	draw_sub_separator();

	if (NULL != n->recs &&
	    n->recs->byhoursz &&
	    n->recs->byhour[0].entries) {
		vv = n->recs->byhour[0].discread /
			(double)n->recs->byhour[0].entries;
		draw_xfer(vv, 0);
		addch(':');
		vv = n->recs->byhour[0].discwrite /
			(double)n->recs->byhour[0].entries;
		draw_xfer(vv, 1);
	} else
		addstr("------:------");

	draw_sub_separator();

	if (NULL != n->recs &&
	    n->recs->bydaysz &&
	    n->recs->byday[0].entries) {
		vv = n->recs->byday[0].discread /
			(double)n->recs->byday[0].entries;
		draw_xfer(vv, 0);
		addch(':');
		vv = n->recs->byday[0].discwrite /
			(double)n->recs->byday[0].entries;
		draw_xfer(vv, 1);
	} else
		addstr("------:------");
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
		addch(':');
		vv = n->recs->byqmin[0].nettx /
			n->recs->byqmin[0].entries;
		attron(A_BOLD);
		draw_xfer(vv, 1);
		attroff(A_BOLD);
	} else
		addstr("------:------");

	draw_sub_separator();

	if (NULL != n->recs &&
	    n->recs->byhoursz &&
	    n->recs->byhour[0].entries) {
		vv = n->recs->byhour[0].netrx /
			n->recs->byhour[0].entries;
		draw_xfer(vv, 0);
		addch(':');
		vv = n->recs->byhour[0].nettx /
			n->recs->byhour[0].entries;
		draw_xfer(vv, 1);
	} else
		addstr("------:------");

	draw_sub_separator();

	if (NULL != n->recs &&
	    n->recs->bydaysz &&
	    n->recs->byday[0].entries) {
		vv = n->recs->byday[0].netrx /
			n->recs->byday[0].entries;
		draw_xfer(vv, 0);
		addch(':');
		vv = n->recs->byday[0].nettx /
			n->recs->byday[0].entries;
		draw_xfer(vv, 1);
	} else
		addstr("------:------");
}

static void
draw_mem(const struct node *n)
{
	double	 vv;

	if (NULL == n->recs) {
		printw("%10s", " ");
		addch(' ');
		printw("------%");
		draw_sub_separator();
		printw("------%");
		draw_sub_separator();
		printw("------%");
		return;
	}

	if (n->recs->byqminsz &&
	    n->recs->byqmin[0].entries) {
		vv = n->recs->byqmin[0].mem /
			n->recs->byqmin[0].entries;
		draw_bars(vv);
		addch(' ');
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
		draw_pct(vv);
	} else
		printw("%6s", " ");

	draw_sub_separator();

	if (n->recs->bydaysz &&
	    n->recs->byday[0].entries) {
		vv = n->recs->byday[0].mem /
			n->recs->byday[0].entries;
		draw_pct(vv);
	} else
		printw("%6s", " ");
}

static void
draw_cpu(const struct node *n)
{
	double	 vv;

	if (NULL == n->recs) {
		printw("%10s", " ");
		addch(' ');
		printw("------%");
		draw_sub_separator();
		printw("------%");
		draw_sub_separator();
		printw("------%");
		return;
	}

	if (n->recs->byqminsz &&
	    n->recs->byqmin[0].entries) {
		vv = n->recs->byqmin[0].cpu /
			n->recs->byqmin[0].entries;
		draw_bars(vv);
		addch(' ');
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
		draw_pct(vv);
	} else
		printw("%6s", " ");

	draw_sub_separator();

	if (n->recs->bydaysz &&
	    n->recs->byday[0].entries) {
		vv = n->recs->byday[0].cpu /
			n->recs->byday[0].entries;
		draw_pct(vv);
	} else
		printw("%6s", " ");
}

static void
draw_centre(const char *v, size_t sz)
{
	size_t	 vsz = strlen(v), left, i;

	assert(vsz <= sz);

	left = (sz - vsz) / 2;

	for (i = 0; i < left; i++)
		addch(' ');
	addstr(v);
	i += vsz;
	for ( ; i < sz; i++)
		addch(' ');

}

static void
draw_header(struct draw *d, size_t maxhostsz, size_t maxipsz)
{

	move(0, 1);
	clrtoeol();
	printw("%*s", (int)maxhostsz, "hostname");
	addch(' ');
	draw_main_separator();
	addch(' ');
	draw_centre("processor", 31);
	addch(' ');
	draw_main_separator();
	addch(' ');
	draw_centre("memory", 31);
	addch(' ');
	draw_main_separator();
	addch(' ');
	draw_centre("network rx:tx", 41);
	addch(' ');
	draw_main_separator();
	addch(' ');
	draw_centre("disc read:write", 41);
	addch(' ');
	draw_main_separator();
	addch(' ');
	printw("%*s", (int)maxipsz, "address");
	addch(' ');
	draw_main_separator();
	addch(' ');
	printw("%9s", "last");
	addch(' ');
	draw_main_separator();
	addch(' ');
	printw("%9s", "ping");
}

void
draw(WINDOW *win, struct draw *d, 
	const struct node *n, size_t nsz, time_t t)
{
	size_t	 i, sz, maxhostsz, maxipsz,
		 lastseenpos, intervalpos, chhead;
	int	 x, y;

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
	 * Our display is laid out as follows, here shown vertically but
	 * really shown in columns.
	 *
	 * hostname |                        (maxhostsz + 2)
	 * |||||||||| xxx.x%|xxx.x%|xxx.x% | ([10 6|6|6]=33)
	 * |||||||||| xxx.x%|xxx.x%|xxx.x% | ([10 6|6|6]=33)
	 * rx:tx|rx:tx|rx:tx                 (6 1 6|13|13=41)
	 * rd:wr|rd:wr|rd:rw                 (6 1 6|13|13=41)
	 * ip                                (maxipsz)
	 * hh:mm:ss                          (last entry=9)
	 * hh:mm:ss                          (last seen=9)
	 */

	for (i = 0; i < nsz; i++) {
		move(i + 1, 1);
		clrtoeol();
		attron(A_BOLD);
		printw("%*s", (int)maxhostsz, n[i].host);
		attroff(A_BOLD);
		addch(' ');
		draw_main_separator();
		addch(' ');
		draw_cpu(&n[i]);
		addch(' ');
		draw_main_separator();
		addch(' ');
		draw_mem(&n[i]);
		addch(' ');
		draw_main_separator();
		addch(' ');
		draw_inet(&n[i]);
		addch(' ');
		draw_main_separator();
		addch(' ');
		draw_disc(&n[i]);
		addch(' ');
		draw_main_separator();
		addch(' ');
		printw("%*s", (int)maxipsz,
			n[i].addrs.addrs[n[i].addrs.curaddr].ip);
		addch(' ');
		draw_main_separator();
		addch(' ');
		getyx(stdscr, y, x);
		intervalpos = x;
		draw_interval(get_last(&n[i]), t);
		addch(' ');
		draw_main_separator();
		addch(' ');
		getyx(stdscr, y, x);
		lastseenpos = x;
		draw_interval(n[i].lastseen, t);
	}

	/* Remember for updating times. */

	chhead = intervalpos != d->intervalpos ||
		lastseenpos != d->lastseenpos;

	d->intervalpos = intervalpos;
	d->lastseenpos = lastseenpos;

	if (chhead)
		draw_header(d, maxhostsz, maxipsz);
}

/*
 * If we have no new data but one second has elapsed, then redraw the
 * interval from last collection and last ping time.
 * We do this by overwriting only that data, which reduces screen update
 * and keeps our display running tight.
 */
void
drawtimes(WINDOW *win, const struct draw *d, 
	const struct node *n, size_t nsz, time_t t)
{
	size_t	 i;

	/* Hasn't collected data yet... */

	if (0 == d->intervalpos || 0 == d->lastseenpos)
		return;

	for (i = 0; i < nsz; i++) {
		move(i + 1, d->intervalpos);
		draw_interval(get_last(&n[i]), t);
		move(i + 1, d->lastseenpos);
		draw_interval(n[i].lastseen, t);
	}
}
