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

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <kcgi.h>
#include <kcgijson.h>
#include <ksql.h>

#include "config.h"
#include "extern.h"
#include "db.h"
#include "json.h"

enum	page {
	PAGE_INDEX,
	PAGE__MAX
};

static const char *const pages[PAGE__MAX] = {
	"index", /* PAGE_INDEX */
};

/*
 * Fill out generic headers then start the HTTP document body (no more
 * headers after this point!)
 */
static void
http_open(struct kreq *r, enum khttp code)
{

	khttp_head(r, kresps[KRESP_STATUS], 
		"%s", khttps[code]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE], 
		"%s", kmimetypes[r->mime]);
	khttp_body(r);
}

static void
sendindex(struct kreq *r, 
	const struct system *sys, const struct record_q *q)
{
	struct kjsonreq	 req;
	const struct record *rr;

	http_open(r, KHTTP_200);
	kjson_open(&req, r);
	kjson_obj_open(&req);

	kjson_putstringp(&req, "version", VERSION);
	json_system_obj(&req, sys);

	kjson_arrayp_open(&req, "qmin");
	TAILQ_FOREACH(rr, q, _entries)
		if (INTERVAL_byqmin == rr->interval) {
			kjson_obj_open(&req);
			json_record_data(&req, rr);
			kjson_obj_close(&req);
		}
	kjson_array_close(&req);
	kjson_arrayp_open(&req, "min");
	TAILQ_FOREACH(rr, q, _entries)
		if (INTERVAL_bymin == rr->interval) {
			kjson_obj_open(&req);
			json_record_data(&req, rr);
			kjson_obj_close(&req);
		}
	kjson_array_close(&req);
	kjson_arrayp_open(&req, "hour");
	TAILQ_FOREACH(rr, q, _entries)
		if (INTERVAL_byhour == rr->interval) {
			kjson_obj_open(&req);
			json_record_data(&req, rr);
			kjson_obj_close(&req);
		}
	kjson_array_close(&req);
	kjson_arrayp_open(&req, "day");
	TAILQ_FOREACH(rr, q, _entries)
		if (INTERVAL_byday == rr->interval) {
			kjson_obj_open(&req);
			json_record_data(&req, rr);
			kjson_obj_close(&req);
		}
	kjson_array_close(&req);
	kjson_arrayp_open(&req, "week");
	TAILQ_FOREACH(rr, q, _entries)
		if (INTERVAL_byweek == rr->interval) {
			kjson_obj_open(&req);
			json_record_data(&req, rr);
			kjson_obj_close(&req);
		}
	kjson_array_close(&req);
	kjson_arrayp_open(&req, "year");
	TAILQ_FOREACH(rr, q, _entries)
		if (INTERVAL_byyear == rr->interval) {
			kjson_obj_open(&req);
			json_record_data(&req, rr);
			kjson_obj_close(&req);
		}
	kjson_array_close(&req);

	kjson_obj_close(&req);
	kjson_close(&req);
}

int
main(void)
{
	struct kreq	 r;
	enum kcgi_err	 er;
	struct record_q	*rq;
	struct system	*sys;

	if (-1 == pledge("stdio rpath "
	    "cpath wpath flock fattr proc", NULL)) {
		kutil_warn(NULL, NULL, "pledge");
		return EXIT_FAILURE;
	}

	er = khttp_parsex(&r, ksuffixmap,
             kmimetypes, KMIME__MAX, NULL, 0,
             pages, PAGE__MAX, KMIME_APP_JSON,
             PAGE_INDEX, NULL, NULL, 0, NULL);

	if (KCGI_OK != er) {
		kutil_warnx(NULL, NULL, "%s", kcgi_strerror(er));
		return EXIT_FAILURE;
	}

	/*
	 * Front line of defence: make sure we're a proper method, make
	 * sure we're a page, make sure we're a JSON file.
	 */

	if (KMETHOD_GET != r.method) {
		http_open(&r, KHTTP_405);
		khttp_free(&r);
		return EXIT_SUCCESS;
	} else if (PAGE__MAX == r.page || 
	           KMIME_APP_JSON != r.mime) {
		http_open(&r, KHTTP_404);
		khttp_free(&r);
		return EXIT_SUCCESS;
	}

	if (NULL == (r.arg = db_open(DBFILE))) {
		khttp_free(&r);
		return EXIT_SUCCESS;
	}

	if (-1 == pledge("stdio", NULL)) {
		kutil_warn(NULL, NULL, "pledge");
		db_close(r.arg);
		khttp_free(&r);
		return EXIT_FAILURE;
	}

	db_role(r.arg, ROLE_consume);

	rq = db_record_list_lister(r.arg);
	sys = db_system_get_id(r.arg, 1);

	sendindex(&r, sys, rq);

	db_system_free(sys);
	db_record_freeq(rq);

	db_close(r.arg);
	khttp_free(&r);

	return EXIT_SUCCESS;
}
