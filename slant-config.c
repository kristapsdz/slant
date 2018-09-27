#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"
#include "slant.h"

static int
config_parse_servers(const char *fn, struct config *cfg, 
	const char **toks, size_t toksz, size_t *pos)
{
	void	*pp;

	if (*pos >= toksz) {
		warnx("%s: empty server list", fn);
		return 0;
	} else if (cfg->urlsz) {
		warnx("%s: servers already defined", fn);
		return 0;
	}

	while (*pos < toksz) {
		if (0 == strcmp(toks[*pos], ";"))
			break;
		pp = reallocarray
			(cfg->urls, cfg->urlsz + 1,
			 sizeof(char *));
		if (NULL == pp) {
			warn(NULL);
			return 0;
		}
		cfg->urls = pp;
		cfg->urls[cfg->urlsz] = 
			strdup(toks[*pos]);
		if (NULL == cfg->urls[cfg->urlsz]) {
			warn(NULL);
			return 0;
		}
		cfg->urlsz++;
		(*pos)++;
	}

	if (*pos == toksz)
		return 0;

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
		free(cfg->urls[i]);

	free(cfg->urls);
}
