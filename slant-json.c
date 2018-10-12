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

/*
 * Parse the top-level objects of our JSON body.
 * Returns >1 on success, 0 on transient failure (malformatted), <0 on
 * system error (the system should halt).
 */
static int
json_parse_obj(struct out *out, const char *str, 
	const jsmntok_t *t, size_t pos, struct node *n, int toks)
{
	int	 rc = 0;

	if (jsmn_eq(str, &t[pos], "version")) {
		if (n->recs->has_version) {
			xwarnx(out, "JSON \"version\" "
				"duplicated: %s", n->host);
			return 0;
		} else if (JSMN_STRING != t[++pos].type) {
			xwarnx(out, "JSON \"version\" node "
				"not a string: %s", n->host);
			return 0;
		}
		n->recs->version = strndup
			(str + t[pos].start,
			 t[pos].end - t[pos].start);
		if (NULL == n->recs->version) {
			xwarn(out, NULL);
			return -1;
		}
		n->recs->has_version = 1;
		return 1;
	} else if (jsmn_eq(str, &t[pos], "system")) {
		if (n->recs->has_system) {
			xwarnx(out, "JSON \"system\" "
				"duplicated: %s", n->host);
			return 0;
		}
		pos++;
		rc = jsmn_system(&n->recs->system,
			 str, &t[pos], toks - pos);
		if (0 == rc) 
			xwarnx(out, "malformed JSON "
				"\"system\" node: %s", n->host);
		else if (rc < 0)
			xwarn(out, NULL);
		else
			n->recs->has_system = 1;
		return rc;
	}

	/* Now we do the qmin, min, hour, day, week, and year arrays. */

	if (jsmn_eq(str, &t[pos], "qmin")) {
		if (n->recs->byqminsz) {
			xwarnx(out, "JSON \"qmin\" "
				"duplicated: %s", n->host);
			return 0;
		}
		pos++;
		rc = jsmn_record_array
			(&n->recs->byqmin,
			 &n->recs->byqminsz,
			 str, &t[pos], toks - pos);
	} else if (jsmn_eq(str, &t[pos], "min")) {
		if (n->recs->byminsz) {
			xwarnx(out, "JSON \"min\" "
				"duplicated: %s", n->host);
			return 0;
		}
		pos++;
		rc = jsmn_record_array
			(&n->recs->bymin,
			 &n->recs->byminsz,
			 str, &t[pos], toks - pos);
	} else if (jsmn_eq(str, &t[pos], "hour")) {
		if (n->recs->byhoursz) {
			xwarnx(out, "JSON \"hour\" "
				"duplicated: %s", n->host);
			return 0;
		}
		pos++;
		rc = jsmn_record_array
			(&n->recs->byhour,
			 &n->recs->byhoursz,
			 str, &t[pos], toks - pos);
	} else if (jsmn_eq(str, &t[pos], "day")) {
		if (n->recs->bydaysz) {
			xwarnx(out, "JSON \"day\" "
				"duplicated: %s", n->host);
			return 0;
		}
		pos++;
		rc = jsmn_record_array
			(&n->recs->byday,
			 &n->recs->bydaysz,
			 str, &t[pos], toks - pos);
	} else if (jsmn_eq(str, &t[pos], "week")) {
		if (n->recs->byweeksz) {
			xwarnx(out, "JSON \"week\" "
				"duplicated: %s", n->host);
			return 0;
		}
		pos++;
		rc = jsmn_record_array
			(&n->recs->byweek,
			 &n->recs->byweeksz,
			 str, &t[pos], toks - pos);
	} else if (jsmn_eq(str, &t[pos], "year")) {
		if (n->recs->byyearsz) {
			xwarnx(out, "JSON \"year\" "
				"duplicated: %s", n->host);
			return 0;
		}
		pos++;
		rc = jsmn_record_array
			(&n->recs->byyear,
			 &n->recs->byyearsz,
			 str, &t[pos], toks - pos);
	} else {
		xwarnx(out, "unknown JSON node: %s", n->host);
		return 0;
	}

	if (0 == rc) 
		xwarnx(out, "JSON record array node failed: %s", n->host);
	else if (rc < 0)
		xwarn(out, NULL);

	return rc;
}

/*
 * Parse the full JSON response for a given node.
 * We use the JSMN interface produced by kwebapp.
 * This is a somewhat... abstruse interface, but simple and robust.
 * Returns >0 on success, 0 on transient failure (malformed JSON or
 * other recoverable error), <0 on fatal error (system should halt).
 */
int
json_parse(struct out *out, struct node *n, const char *str, size_t sz)
{
	int	 	 i, toks, ntoks, rc;
	size_t		 j;
	jsmn_parser	 jp;
	jsmntok_t	*t = NULL;

	/* Allocate results, if necessary, and free existing. */

	if (NULL == n->recs &&
	    NULL == (n->recs = calloc(1, sizeof(struct recset))))
		goto syserr;

	recset_free(n->recs);
	memset(n->recs, 0, sizeof(struct recset));

	/* Parse to get token length. */

	jsmn_init(&jp);
	if (-1 == (toks = jsmn_parse(&jp, str, sz, NULL, 0)))
		goto syserr;
	else if (toks <= 0)
		goto err;

	/* Allocate tokens and re-parse. */

	if (NULL == (t = calloc(toks, sizeof(jsmntok_t))))
		goto syserr;

	jsmn_init(&jp);
	ntoks = jsmn_parse(&jp, str, sz, t, toks);

	/* 
	 * I'm not sure why this happens, but jsmn_parse() can return a
	 * different result if "toks" is set.
	 * Catch that here.
	 */

	if (ntoks != toks) {
		xwarnx(out, "token count: %d != %d: %s", 
			ntoks, toks, n->host);
		goto err;
	} else if (t[0].type != JSMN_OBJECT) {
		xwarnx(out, "top-level JSON "
			"node not object: %s", n->host);
		goto err;
	}

	/* Parse each of our top-level objects. */

	for (i = 0, j = 1; i < t[0].size; i++) {
		rc = json_parse_obj
			(out, str, &t[j], 0, n, toks - j);
		if (rc < 0)
			goto syserr;
		else if (0 == rc)
			goto err;
		j += 1 + rc;
	}

	free(t);
	return 1;
syserr:
	xwarn(out, NULL);
	free(t);
	return -1;
err:
	xwarnx(out, "JSON parse: %s", n->host);
	fprintf(out->errs, "------>------\n");
	fprintf(out->errs, "%.*s\n", (int)sz, str);
	fprintf(out->errs, "------<------\n");
	fflush(out->errs);
	free(t);
	return 0;
}

