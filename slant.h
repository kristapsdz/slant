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

/*
 * We use this structure to keep track of key parts of our UI.
 * It lets us optimise repainting the screen per second to keep track of
 * our last-seen intervals.
 */
struct	draw {
	size_t		 lastseenpos; /* location of last seen stamp */
	size_t		 intervalpos; /* location of interval stamp */
	enum draword	 order;
	int		 box_cpu;
#define	CPU_QMIN	 0x0001
#define	CPU_MIN	 	 0x0002
#define	CPU_HOUR 	 0x0004
#define	CPU_DAY	 	 0x0008
#define	CPU_QMIN_BARS	 0x0010
	int		 box_mem;
#define	MEM_QMIN	 0x0001
#define	MEM_MIN	 	 0x0002
#define	MEM_HOUR 	 0x0004
#define	MEM_DAY	 	 0x0008
#define	MEM_QMIN_BARS	 0x0010
	int		 box_net;
#define	NET_QMIN	 0x0001
#define	NET_MIN	 	 0x0002
#define	NET_HOUR 	 0x0004
#define	NET_DAY	 	 0x0008
	int		 box_disc;
#define	DISC_QMIN	 0x0001
#define	DISC_MIN	 0x0002
#define	DISC_HOUR 	 0x0004
#define	DISC_DAY	 0x0008
	int		 box_link;
#define	LINK_IP		 0x0001
#define LINK_STATE	 0x0002
#define LINK_ACCESS	 0x0004
};

/*
 * The full set of records of a particular host.
 * This can be totally empty: we have no constraints.
 */
struct	recset {
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
};

struct	node {
	enum state	 state; /* state of node */
	char		*url; /* full URL for connect */
	char		*host; /* just hostname of connect */
	char		*path; /* path of connect */
	struct xfer	 xfer; /* transfer information */
	struct dns	 addrs; /* all possible IP addresses */
	time_t		 waitstart; /* wait period start */
	time_t		 lastseen; /* last data received */
	struct recset	*recs; /* results */
	int		 dirty; /* new results */
};

/* All of this is from json.h. */

enum	json_type_e {
	json_type_string,
	json_type_number,
	json_type_object,
	json_type_array,
	json_type_true,
	json_type_false,
	json_type_null
};

struct 	json_value_s {
	void 		*payload;
	enum json_type_e type;
};

struct	json_number_s {
	const char *number;
	size_t 	number_size;
};

struct 	json_array_element_s {
	struct json_value_s *value;
	struct json_array_element_s *next;
};

struct	json_array_s {
	struct json_array_element_s *start;
	size_t 	 length;
};

struct 	json_string_s {
	const char *string;
	size_t	 string_size;
};

struct 	json_object_element_s {
	struct json_string_s *name;
	struct json_value_s *value;
	struct json_object_element_s *next;
};

struct	json_object_s {
	struct json_object_element_s *start;
	size_t 	 length;
};

enum json_parse_error_e {
	json_parse_error_none = 0,
	json_parse_error_expected_comma_or_closing_bracket,
	json_parse_error_expected_colon,
	json_parse_error_expected_opening_quote,
	json_parse_error_invalid_string_escape_sequence,
	json_parse_error_invalid_number_format,
	json_parse_error_invalid_value,
	json_parse_error_premature_end_of_buffer,
	json_parse_error_invalid_string,
	json_parse_error_allocator_failed,
	json_parse_error_unexpected_trailing_characters,
	json_parse_error_unknown
};

struct json_parse_result_s {
	size_t error;
	size_t error_offset;
	size_t error_line_no;
	size_t error_row_no;
};


__BEGIN_DECLS

void	 xdbg(WINDOW *, const char *, ...)
		__attribute__((format(printf, 2, 3)));
void	 xwarnx(WINDOW *, const char *, ...)
		__attribute__((format(printf, 2, 3)));
void	 xwarn(WINDOW *, const char *, ...)
		__attribute__((format(printf, 2, 3)));

int	 dns_parse_url(WINDOW *, struct node *);
int	 dns_resolve(WINDOW *, const char *, struct dns *);

int	 http_init_connect(WINDOW *, struct node *);
int	 http_close_done(WINDOW *, struct node *);
int	 http_close_err(WINDOW *, struct node *);
int	 http_connect(WINDOW *, struct node *);
int	 http_write(WINDOW *, struct node *n);
int	 http_read(WINDOW *, struct node *n);

void	 draw(WINDOW *, struct draw *, time_t,
		const struct node *, size_t, time_t);
void	 drawtimes(WINDOW *, const struct draw *, time_t,
		const struct node *, size_t, time_t);

struct json_value_s *
	 json_parse_ex(const void *, size_t, size_t, 
		void *(*)(void *, size_t), void *, 
		struct json_parse_result_s *);
int 	 jsonobj_parse(WINDOW *, struct node *n, const char *, size_t);

__END_DECLS

#endif /* SLANT_H */
