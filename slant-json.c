#include <sys/queue.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include <kcgi.h>
#include <kcgijson.h>

#include "extern.h"
#include "slant.h"
#include "json.h"

static int
json_parse_obj(WINDOW *errwin, const char *str, 
	const jsmntok_t *t, size_t pos, struct node *n, int toks)
{
	int	 rc = 0;

	if (jsmn_eq(str, &t[pos], "version")) {
		if (JSMN_STRING != t[++pos].type) {
			xwarnx(errwin, "JSON version node "
				"not a string: %s", n->host);
			return 0;
		}
		n->recs->version = strndup
			(str + t[pos].start,
			 t[pos].end - t[pos].start);
		xwarnx(errwin, "JSON version: %s", n->recs->version);
		return NULL == n->recs->version ? 0 : 1;
	}

	/* Now we do the qmin, min, hour, day, week, and year arrays. */

	if (jsmn_eq(str, &t[pos], "qmin")) {
		if (JSMN_ARRAY != t[++pos].type) {
			xwarnx(errwin, "JSON qmin node "
				"not an array: %s", n->host);
			return 0;
		}
		rc = jsmn_record_array
			(&n->recs->byqmin,
			 &n->recs->byqminsz,
			 str, &t[pos], toks - pos);
	} else if (jsmn_eq(str, &t[pos], "min")) {
		if (JSMN_ARRAY != t[++pos].type) {
			xwarnx(errwin, "JSON min node "
				"not an array: %s", n->host);
			return 0;
		}
		rc = jsmn_record_array
			(&n->recs->bymin,
			 &n->recs->byminsz,
			 str, &t[pos], toks - pos);
	} else if (jsmn_eq(str, &t[pos], "hour")) {
		if (JSMN_ARRAY != t[++pos].type) {
			xwarnx(errwin, "JSON hour node "
				"not an array: %s", n->host);
			return 0;
		}
		rc = jsmn_record_array
			(&n->recs->byhour,
			 &n->recs->byhoursz,
			 str, &t[pos], toks - pos);
	} else if (jsmn_eq(str, &t[pos], "day")) {
		if (JSMN_ARRAY != t[++pos].type) {
			xwarnx(errwin, "JSON day node "
				"not an array: %s", n->host);
			return 0;
		}
		rc = jsmn_record_array
			(&n->recs->byday,
			 &n->recs->bydaysz,
			 str, &t[pos], toks - pos);
	} else if (jsmn_eq(str, &t[pos], "week")) {
		if (JSMN_ARRAY != t[++pos].type) {
			xwarnx(errwin, "JSON week node "
				"not an array: %s", n->host);
			return 0;
		}
		rc = jsmn_record_array
			(&n->recs->byweek,
			 &n->recs->byweeksz,
			 str, &t[pos], toks - pos);
	} else if (jsmn_eq(str, &t[pos], "year")) {
		if (JSMN_ARRAY != t[++pos].type) {
			xwarnx(errwin, "JSON year node "
				"not an array: %s", n->host);
			return 0;
		}
		rc = jsmn_record_array
			(&n->recs->byyear,
			 &n->recs->byyearsz,
			 str, &t[pos], toks - pos);
	} else {
		xwarnx(errwin, "JSON node "
			"unknown name: %d, %s", t[pos].type, n->host);
		return 0;
	}

	return rc;
}

/*
 * Parse the full response.
 */
int
json_parse(WINDOW *errwin, struct node *n, const char *str, size_t sz)
{
	int	 	 i, toks, ntoks, rc;
	size_t		 j;
	jsmn_parser	 jp;
	jsmntok_t	*t = NULL;

	/* Allocate results, if necessary, and free existing. */

	if (NULL == n->recs &&
	    NULL == (n->recs = calloc(1, sizeof(struct recset))))
		goto syserr;

	free(n->recs->version);
	free(n->recs->byqmin);
	free(n->recs->bymin);
	free(n->recs->byhour);
	free(n->recs->byday);
	free(n->recs->byweek);
	free(n->recs->byyear);

	memset(n->recs, 0, sizeof(struct recset));

	jsmn_init(&jp);
	if (-1 == (toks = jsmn_parse(&jp, str, sz, NULL, 0)))
		goto syserr;
	else if (toks <= 0)
		goto err;

	if (NULL == (t = calloc(toks, sizeof(jsmntok_t))))
		goto syserr;

	jsmn_init(&jp);
	ntoks = jsmn_parse(&jp, str, sz, t, toks);

	if (ntoks != toks) {
		xwarnx(errwin, "token count: %d != %d: %s", 
			ntoks, toks, n->host);
		goto err;
	} else if (t[0].type != JSMN_OBJECT) {
		xwarnx(errwin, "top-level JSON "
			"node not object: %s", n->host);
		goto err;
	}

	for (i = 0, j = 1; i < t[0].size; i++) {
		rc = json_parse_obj
			(errwin, str, &t[j], 0, n, toks - j);
		if (rc < 0)
			goto syserr;
		else if (0 == rc)
			goto err;
		j += 1 + rc;
	}

	return 1;
syserr:
	xwarn(errwin, NULL);
	free(t);
	return -1;
err:
	xwarnx(errwin, "JSON parse: %s", n->host);
	fprintf(stderr, "------>------\n");
	fprintf(stderr, "%.*s\n", (int)sz, str);
	fprintf(stderr, "------<------\n");
	fflush(stderr);
	free(t);
	return 0;
}

