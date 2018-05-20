#include <sys/queue.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "extern.h"
#include "slant.h"

struct	buf {
	char	*buf;
	size_t	 bufsz;
};

static void
buf_appendv(struct buf *, const char *, ...)
	__attribute__((format(printf, 2, 3)));

static void
buf_appendc(struct buf *b, char c)
{

	b->buf = realloc(b->buf, b->bufsz + 2);
	if (NULL == b->buf) 
		err(EXIT_FAILURE, "realloc");

	b->buf[b->bufsz] = c;
	b->buf[b->bufsz + 1] = '\0';
	b->bufsz++;
}

static void
buf_appendv(struct buf *b, const char *fmt, ...)
{
	int	 len;
	va_list	 ap;
	
	va_start(ap, fmt);

	/* Get length of variable string. */

	if ((len = vsnprintf(NULL, 0, fmt, ap)) < 0)
		err(EXIT_FAILURE, "vsnprintf");

	va_end(ap);
	va_start(ap, fmt);

	/* Reallocate and fill to new buffer + NUL. */

	b->buf = realloc(b->buf, b->bufsz + len + 1);
	if (NULL == b->buf) 
		err(EXIT_FAILURE, "realloc");
	vsnprintf(b->buf + b->bufsz, len + 1, fmt, ap);
	b->bufsz += len;
	va_end(ap);

}

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
draw_mem(struct buf *b, const struct node *n)
{
	size_t	 j;
	double	 v, vv;

	if (NULL == n->recs) {
		buf_appendv(b, "[%34s", "]");
		return;
	}

	buf_appendc(b, '[');

	if (n->recs->byqminsz &&
	    n->recs->byqmin[0].entries) {
		vv = n->recs->byqmin[0].mem /
			n->recs->byqmin[0].entries;
		for (j = 1; j <= 10; j++) {
			v = j * 10.0;
			if (v > vv)
				break;
			buf_appendc(b, '|');
		}
		for ( ; j <= 10; j++)
			buf_appendc(b, ' ');
		buf_appendv(b, " %5.1f%%|", vv);
	} else
		buf_appendv(b, "%18s", "|");

	if (n->recs->byhoursz &&
	    n->recs->byhour[0].entries) {
		vv = n->recs->byhour[0].mem /
			n->recs->byhour[0].entries;
		buf_appendv(b, " %5.1f%%|", vv);
	} else
		buf_appendv(b, "%8s", "|");

	if (n->recs->bydaysz &&
	    n->recs->byday[0].entries) {
		vv = n->recs->byday[0].mem /
			n->recs->byday[0].entries;
		buf_appendv(b, " %5.1f%%]", vv);
	} else
		buf_appendv(b, "%8s", "]");
}

static void
draw_cpu(struct buf *b, const struct node *n)
{
	size_t	 j;
	double	 v, vv;

	if (NULL == n->recs) {
		buf_appendv(b, "[%34s", "]");
		return;
	}

	buf_appendc(b, '[');

	if (n->recs->byqminsz &&
	    n->recs->byqmin[0].entries) {
		vv = n->recs->byqmin[0].cpu /
			n->recs->byqmin[0].entries;
		for (j = 1; j <= 10; j++) {
			v = j * 10.0;
			if (v > vv)
				break;
			buf_appendc(b, '|');
		}
		for ( ; j <= 10; j++)
			buf_appendc(b, ' ');
		buf_appendv(b, " %5.1f%%|", vv);
	} else
		buf_appendv(b, "%18s", "|");

	if (n->recs->byhoursz &&
	    n->recs->byhour[0].entries) {
		vv = n->recs->byhour[0].cpu /
			n->recs->byhour[0].entries;
		buf_appendv(b, " %5.1f%%|", vv);
	} else
		buf_appendv(b, "%8s", "|");

	if (n->recs->bydaysz &&
	    n->recs->byday[0].entries) {
		vv = n->recs->byday[0].cpu /
			n->recs->byday[0].entries;
		buf_appendv(b, " %5.1f%%]", vv);
	} else
		buf_appendv(b, "%8s", "]");
}

void
draw(const struct node *n, size_t nsz)
{
	size_t	 	 i, sz, maxhostsz, maxipsz;
	time_t		 last, span, t = time(NULL), hr, min;
	struct buf	 b;

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

	memset(&b, 0, sizeof(struct buf));

	/*
	 * hostname                          (maxhostsz)
	 * [|||||||||| xxx.x%|xxx.x%|xxx.x%] ([10 6|6|6]=31)
	 * [|||||||||| xxx.x%|xxx.x%|xxx.x%] ([10 6|6|6]=31)
	 * hh:mm:ss o
	 */

	for (i = 0; i < nsz; i++) {
		buf_appendv(&b, "%*s ", 
			(int)maxhostsz, n[i].host);
		draw_cpu(&b, &n[i]);
		buf_appendc(&b, ' ');
		draw_mem(&b, &n[i]);
		buf_appendv(&b, " %*s", (int)maxipsz, 
			n[i].addrs.addrs[n[i].addrs.curaddr].ip);
		if ((last = get_last(&n[i]))) {
			if ((span = t - last) < 0)
				span = 0;
			hr = span / (60 * 60);
			span -= hr * 60 * 60;
			min = span / 60;
			span -= min * 60;
			buf_appendv(&b, " %3lld:%.2lld:%.2lld", 
				(long long)hr, (long long)min, 
				(long long)span);
		} else
			buf_appendv(&b, " %s", "---:--:--");

		buf_appendc(&b, '\n');
	}

	printf("%s", b.buf);
	free(b.buf);
}
