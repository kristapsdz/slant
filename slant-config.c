#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"
#include "slant.h"

/*
 * "waittime" num ";"
 */
static int
config_parse_waittime(const char *fn, struct config *cfg, 
	const char **toks, size_t toksz, size_t *pos)
{
	const char	*er;

	if (*pos >= toksz) {
		warnx("%s: unexpected eof", fn);
		return 0;
	}

	cfg->waittime = strtonum(toks[*pos], 15, INT_MAX, &er);
	if (NULL != er) {
		warnx("%s: bad waittime: %s", fn, er);
		return 0;
	} else if (++(*pos) >= toksz) {
		warnx("%s: unexpected eof", fn);
		return 0;
	} else if (strcmp(toks[*pos], ";")) {
		warnx("%s: expected semicolon", fn);
		return 0;
	}

	(*pos)++;
	return 1;
}

/*
 * ["waittime" num] "}"
 */
static int
config_parse_server_args(const char *fn, struct config *cfg, 
	const char **toks, size_t toksz, size_t *pos, size_t count)
{
	const char	*er;
	time_t		 waittime = 0;
	size_t		 i;

	while (*pos < toksz && strcmp(toks[*pos], "}")) {
		warnx("%s", toks[*pos]);
		if (0 == strcmp(toks[*pos], "waittime")) {
			if (++(*pos) >= toksz) {
				warnx("%s: unexpected eof", fn);
				return 0;
			}
			waittime = strtonum
				(toks[*pos], 15, INT_MAX, &er);
			if (NULL != er) {
				warnx("%s: bad waittime: %s", fn, er);
				return 0;
			}
			if (++(*pos) >= toksz) {
				warnx("%s: unexpected eof", fn);
				return 0;
			}
			if (0 == strcmp(toks[*pos], ";"))
				(*pos)++;
		} else {
			warnx("%s: unknown token: %s", fn, toks[*pos]);
			return 0;
		}
	}

	if (*pos >= toksz) {
		warnx("%s: unexpected eof", fn);
		return 0;
	}

	(*pos)++;

	assert(cfg->urlsz >= count);
	if (waittime)
		for (i = 0; i < count; i++)
			cfg->urls[cfg->urlsz - 1 - i].waittime = waittime;

	return 1;
}

/*
 * "servers" s1 [s2...] ["{" args] ";"
 */
static int
config_parse_servers(const char *fn, struct config *cfg, 
	const char **toks, size_t toksz, size_t *pos)
{
	void	*pp;
	size_t	 count = 0;

	while (*pos < toksz) {
		if (0 == strcmp(toks[*pos], ";") ||
	 	    0 == strcmp(toks[*pos], "{"))
			break;
		pp = reallocarray
			(cfg->urls, cfg->urlsz + 1,
			 sizeof(struct nconfig));
		if (NULL == pp) {
			warn(NULL);
			return 0;
		}
		cfg->urls = pp;
		memset(&cfg->urls[cfg->urlsz], 
			0, sizeof(struct nconfig));
		cfg->urlsz++;
		cfg->urls[cfg->urlsz - 1].url = 
			strdup(toks[*pos]);
		if (NULL == cfg->urls[cfg->urlsz - 1].url) {
			warn(NULL);
			return 0;
		}
		(*pos)++;
		count++;
	}

	if (0 == count) {
		warnx("%s: no servers in statement", fn);
		return 0;
	} else if (*pos >= toksz) {
		warnx("%s: unexpected eof", fn);
		return 0;
	}

	/* Now the arguments. */

	if (0 == strcmp(toks[*pos], "{")) {
		(*pos)++;
		if ( ! config_parse_server_args
		    (fn, cfg, toks, toksz, pos, count))
			return 0;
		if (strcmp(toks[*pos], ";")) {
			warnx("%s: expected semicolon", fn);
			return 0;
		}
	}

	(*pos)++;
	return 1;
}

int
config_parse(const char *fn, struct config *cfg)
{
	int		  fd;
	void		 *map, *pp;
	char		 *buf, *bufsv, *cp;
	char		**toks = NULL;
	const char	**tokp;
	size_t		  mapsz, toksz = 0, pos = 0;
	struct stat	  st;

	memset(cfg, 0, sizeof(struct config));

	/* Set some defaults. */

	cfg->waittime = 60;

	/* Open file, map it, create a NUL-terminated string. */

	if (-1 == (fd = open(fn, O_RDONLY, 0))) {
		warn("%s", fn);
		return 0;
	} else if (-1 == fstat(fd, &st)) {
		warn("%s", fn);
		close(fd);
		return 0;
	}
	
	mapsz = (size_t)st.st_size;
	if (NULL == (buf = bufsv = malloc(mapsz + 1))) {
		warn(NULL);
		close(fd);
		return 0;
	}
	map = mmap(NULL, mapsz, PROT_READ, MAP_SHARED, fd, 0);
	if (MAP_FAILED == map) {
		warn("%s", fn);
		free(buf);
		close(fd);
		return 0;
	}

	memcpy(buf, map, mapsz);
	buf[mapsz] = '\0';
	munmap(map, mapsz);
	close(fd);

	/* 
	 * Step through all space-separated tokens.
	 * TODO: make this into a proper parsing sequence at some point,
	 * but for now this will do.
	 */

	while (NULL != (cp = strsep(&buf, " \t\r\n"))) {
		if ('\0' == *cp)
			continue;
		pp = reallocarray
			(toks, toksz + 1,
			 sizeof(char *));
		if (NULL == pp) {
			free(bufsv);
			free(toks);
			warn(NULL);
			return 0;
		}
		toks = pp;
		toks[toksz++] = cp;
	}

	tokp = (const char **)toks;
	while (pos < toksz) {
		if (0 == strcmp(tokp[pos], "servers")) {
			pos++;
			if ( ! config_parse_servers
			    (fn, cfg, tokp, toksz, &pos))
				break;
		} else if (0 == strcmp(tokp[pos], "waittime")) {
			pos++;
			if ( ! config_parse_waittime
			    (fn, cfg, tokp, toksz, &pos))
				break;
		} else {
			warnx("%s: unknown token: %s", fn, tokp[pos]);
			break;
		}
	}

	free(toks);
	free(bufsv);
	return pos == toksz;
}

void
config_free(struct config *cfg)
{
	size_t	 i;

	if (NULL == cfg)
		return;

	for (i = 0; i < cfg->urlsz; i++)
		free(cfg->urls[i].url);

	free(cfg->urls);
}
