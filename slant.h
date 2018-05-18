#ifndef SLANT_H
#define SLANT_H

struct	source {
	int	 family; /* 4 (PF_INET) or 6 (PF_INET6) */
	char	 ip[INET6_ADDRSTRLEN]; /* IPV4 or IPV6 address */
	short	 port;
};

#define MAX_SERVERS_DNS 8

struct	addrset {
	size_t	 	 addrsz;
	struct source	 addrs[MAX_SERVERS_DNS];
};

struct	rec {
	time_t	 	ctime;
	int64_t	 	entries;
	double	 	value;
	int64_t		interval;
	int64_t	 	id;
};

struct	recset {
	struct rec	*byqmin;
	size_t		 byqminsz;
	struct rec	*bymin;
	size_t		 byminsz;
	struct rec	*byhour;
	size_t		 byhoursz;
};

enum	state {
	STATE_STARTUP = 0,
	STATE_RESOLVING,
	STATE_CONNECT_WAITING,
	STATE_CONNECT_READY,
	STATE_CONNECT,
	STATE_WRITE,
	STATE_READ,
	STATE_WAITING,
	STATE_DONE,
};

struct	node {
	enum state	 state; /* state of node */
	char		*url; /* full URL for connect */
	char		*host; /* just hostname of connect */
	char		*path; /* path of connect */
	struct addrset	 addrs; /* all possible IP addresses */
	size_t		 curaddr; /* current working address */
	struct pollfd	*pfd; /* pollfd descriptor */
	struct sockaddr_storage ss; /* socket */
	socklen_t	 sslen; /* socket length */
	char		*wbuf; /* write buffer for http */
	size_t		 wbufsz; /* amount left to write */
	size_t		 wbufpos; /* write position in wbuf */
	char		*rbuf; /* read buffer for http */
	size_t		 rbufsz; /* amount read over http */
	time_t		 waitstart;
	struct recset	*recs;
};

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

__BEGIN_DECLS

int	 dns_parse_url(struct node *);
int	 dns_resolve(const char *, struct addrset *);

int	 http_init_connect(struct node *);
int	 http_connect(struct node *);
int	 http_write(struct node *n);
int	 http_read(struct node *n);

struct json_value_s
	*json_parse(const void *, size_t);

int	 jsonobj_parse(struct node *n, const char *, size_t);

__END_DECLS

#endif /* SLANT_H */
