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
#ifndef SLANT_H
#define SLANT_H

struct	source {
	int	 family; /* 4 (PF_INET) or 6 (PF_INET6) */
	char	 ip[INET6_ADDRSTRLEN]; /* IPV4 or IPV6 address */
};

#define MAX_SERVERS_DNS 8

struct	dns {
	size_t	 	 addrsz; /* num addrs (<= MAX_SERVERS_DNS) */
	struct source	 addrs[MAX_SERVERS_DNS]; /* ip addresses */
	short	 	 port; /* port */
	int		 https; /* non-zero if https, else zero */
	size_t		 curaddr; /* current working address */
};

enum	draword {
	DRAWORD_CMDLINE = 0,
	DRAWORD_CPU,
	DRAWORD_HOST,
	DRAWORD_MEM,
};

enum	drawcat {
	DRAWCAT_CPU,
	DRAWCAT_MEM,
	DRAWCAT_NET,
	DRAWCAT_DISC,
	DRAWCAT_LINK,
	DRAWCAT_HOST,
	DRAWCAT_PROCS,
	DRAWCAT_FILES,
	DRAWCAT_RPROCS
};

/*
 * Within a drawbox, a given line.
 * There can be up to six lines per drawing box.
 */
struct	drawboxln {
	size_t		 len; /* length */
	size_t		 lastseen; /* if >0, lastseen time */
	size_t		 lastrecord; /* if >0, lastrecord time */
	unsigned int	 line; /* line display bits */
#define	LINE_QMIN	 0x0001
#define	LINE_MIN	 0x0002
#define	LINE_HOUR 	 0x0004
#define	LINE_DAY	 0x0008
#define LINE_WEEK	 0x0010
#define LINE_YEAR	 0x0020
#define	LINE_QMIN_BARS	 0x0100
#define	LINE_MIN_BARS	 0x0200
#define	LINE_HOUR_BARS	 0x0400
#define	LINE_DAY_BARS	 0x0800
#define	LINE_WEEK_BARS	 0x1000
#define	LINE_YEAR_BARS	 0x2000
#define	LINK_IP		 0x0001
#define LINK_STATE	 0x0002
#define LINK_ACCESS	 0x0004
#define	HOST_RECORD	 0x0001
#define HOST_SLANT_VERSION 0x0002
#define HOST_UPTIME	 0x0004
};

/*
 * A box (column) to draw in the output.
 * This (currently) can have two lines of content.
 */
struct	drawbox {
	enum drawcat	 cat; /* the box category */
	size_t		 len; /* maximum length for contents */
	struct drawboxln lines[6]; /* our drawables */
};

/*
 * We use this structure to keep track of key parts of our UI.
 * It lets us optimise repainting the screen per second to keep track of
 * our last-seen intervals.
 */
struct	draw {
	struct drawbox	*box; /* all configured boxes */
	size_t		 boxsz; /* number of boxes */
	int		 header; /* boolean for header */
	size_t		 errlog; /* lines in errlog */
	enum draword	 order; /* order of drawn hosts */
	size_t		 maxipsz; /* of all IPs possible, length */
	size_t		 maxhostsz; /* of all hosts possible, length */
	size_t		 maxline; /* max boxes' nonempty lines */
};

/*
 * The full set of records of a particular host.
 * This can be totally empty: we have no constraints.
 */
struct	recset {
	char		*version;
	struct system	 system;
	struct record	*byqmin;
	size_t		 byqminsz;
	struct record	*bymin;
	size_t		 byminsz;
	struct record	*byhour;
	size_t		 byhoursz;
	struct record	*byday;
	size_t		 bydaysz;
	struct record	*byweek;
	size_t		 byweeksz;
	struct record	*byyear;
	size_t		 byyearsz;
	int		 has_system;
	int		 has_version;
};

enum	state {
	STATE_STARTUP = 0,
	STATE_RESOLVING,
	STATE_CONNECT_WAITING,
	STATE_CONNECT_READY,
	STATE_CONNECT,
	STATE_CLOSE_DONE,
	STATE_CLOSE_ERR,
	STATE_WRITE,
	STATE_READ
};

/*
 * Data on a current transfer (read/write).
 */
struct	xfer {
	char		*wbuf; /* write buffer for http */
	size_t		 wbufsz; /* amount left to write */
	size_t		 wbufpos; /* write position in wbuf */
	char		*rbuf; /* read buffer for http */
	size_t		 rbufsz; /* amount read over http */
	struct sockaddr_storage ss; /* socket */
	struct pollfd	*pfd; /* pollfd descriptor */
	struct tls	*tls; /* tls context, if needed */
	time_t		 start; /* connection start time */
	time_t		 lastio; /* last read/write/connect */
};

/*
 * The full status of a single node (URL).
 */
struct	node {
	enum state	 state; /* state of node */
	const char	*url; /* full URL for connect */
	time_t		 waittime; /* idle time */
	time_t		 timeout; /* timeout */
	char		*httpauth; /* HTTP basic authenticator or NULL */
	char		*host; /* just hostname of connect */
	char		*path; /* path of connect */
	struct xfer	 xfer; /* transfer information */
	struct dns	 addrs; /* all possible IP addresses */
	time_t		 waitstart; /* wait period start */
	time_t		 lastseen; /* last sample data received */
	struct recset	*recs; /* results */
	int		 dirty; /* new results */
};

/*
 * Per-node configuration.
 */
struct	nconfig {
	char		*url; /* URL */
	time_t		 waittime; /* idle time (or zero) */
	time_t		 timeout; /* timeout (or zero) */
};

/*
 * A parsed configuration file (~/.slantrc or similar).
 */
struct	config {
	struct draw	 *draw; /* draw config or NULL */
	struct nconfig	 *urls; /* nodes (URLs) */
	size_t		  urlsz; /* number of urls */
	size_t		  waittime; /* global idle time */
	size_t		  timeout; /* global timeout */
};

/*
 * Output information (window, etc.).
 */
struct	out {
	WINDOW		*errwin; /* error panel */
	WINDOW		*mainwin; /* main panel */
	FILE		*errs; /* output error file */
	int		 debug; /* print debugging if non-zero */
};

__BEGIN_DECLS

void	 xdbg(struct out *, const char *, ...)
		__attribute__((format(printf, 2, 3)));
void	 xwarnx(struct out *, const char *, ...)
		__attribute__((format(printf, 2, 3)));
void	 xwarn(struct out *, const char *, ...)
		__attribute__((format(printf, 2, 3)));

size_t	 compute_width(const struct node *, size_t,
		const struct draw *);

void	 dns_parse_url(struct out *, struct node *);
int	 dns_resolve(struct out *, const char *, struct dns *);

int	 http_init_connect(struct out *, struct node *, time_t);
int	 http_close_done(struct out *, struct node *, time_t);
int	 http_close_err(struct out *, struct node *, time_t);
int	 http_connect(struct out *, struct node *, time_t);
int	 http_write(struct out *, struct node *, time_t);
int	 http_read(struct out *, struct node *, time_t);

void	 draw(struct out *, struct draw *, int,
		const struct node *, size_t, time_t);
void	 drawtimes(struct out *, const struct draw *, 
		const struct node *, size_t, time_t);

int 	 json_parse(struct out *, struct node *n, const char *, size_t);

void	 recset_free(struct recset *);

int 	 config_parse(const char *, struct config *, int, char *[]);
void	 config_free(struct config *);

__END_DECLS

#endif /* SLANT_H */
