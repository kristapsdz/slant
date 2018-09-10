#include <sys/queue.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "extern.h"
#include "slant.h"

static const char *const json_errors[] =
{
	"no error",
	"expected comma or closing bracket",
	"expected colon",
	"expected opening quote",
	"invalid string escape sequence",
	"invalid number format",
	"invalid value",
	"premature end of buffer",
	"invalid string",
	"allocator failed",
	"unexpected trailing characters",
	"unknown"
};

static int
jsonobj_parse_int(WINDOW *errwin, const struct node *n,
	int64_t *res, const struct json_number_s *num)
{
	const char	*er;

	*res = strtonum(num->number, -LLONG_MAX, LLONG_MAX, &er);
	if (er == NULL)
		return 1;
	xwarnx(errwin, "JSON bad integer: %s: %s: %s", 
		num->number, n->host, er);
	return 0;
}

static int
jsonobj_parse_real(WINDOW *errwin, const struct node *n,
	double *res, const struct json_number_s *num)
{
	char	*der;

	errno = 0;
	*res = strtod(num->number, &der);
	if (der != num->number && 0 == errno)
		return 1;
	xwarnx(errwin, "JSON bad double: %s: %s", 
		num->number, n->host);
	return 0;
}

static int
jsonobj_parse_recs(WINDOW *errwin,
	const struct node *n, const char *name,
	const struct json_object_element_s *e, 
	struct record **recs, size_t *recsz)
{
	const struct json_array_s *ar;
	const struct json_array_element_s *are;
	const struct json_value_s *val;
	const struct json_object_s *obj;
	const struct json_object_element_s *obje;
	const struct json_number_s *num;
	int64_t		 ival;
	const char	*cp;
	size_t		 i;
	int		 has_ctime, has_entries, 
			 has_cpu, has_interval,
			 has_id, has_mem, has_procs,
			 has_nettx, has_netrx,
			 has_discwrite, has_discread;

	if (json_type_array != e->value->type) {
		xwarnx(errwin, "JSON non-array child: %s: %s", 
			name, n->host);
		return 0;
	}

	ar = e->value->payload;
	*recsz = ar->length;
	*recs = calloc(*recsz, sizeof(struct record));
	if (NULL == *recs) {
		xwarn(errwin, NULL);
		return -1;
	}

	for (i = 0, are = ar->start; 
	     NULL != are; are = are->next, i++) {
		has_ctime = has_entries = has_cpu = 
			has_interval = has_id = 
			has_nettx = has_netrx = 0;

		val = are->value;
		if (json_type_object != val->type) {
			xwarnx(errwin, "JSON non-object child: %s: %s", 
				name, n->host);
			goto err;
		}
		obj = val->payload;
		for (obje = obj->start; 
		     NULL != obje; obje = obje->next) {
			cp = obje->name->string;
			if (json_type_number !=
			    obje->value->type) {
				xwarnx(errwin, "JSON expected number "
					"for array object: %s", 
					n->host);
				goto err;
			}
			num = obje->value->payload;
			if (0 == strcasecmp(cp, "ctime")) {
				if ( ! jsonobj_parse_int
				    (errwin, n, &(*recs)[i].ctime, num)) 
					goto err;
				has_ctime = 1;
			} else if (0 == strcasecmp(cp, "entries")) {
				if ( ! jsonobj_parse_int
				    (errwin, n, &(*recs)[i].entries, num)) 
					goto err;
				has_entries = 1;
			} else if (0 == strcasecmp(cp, "cpu")) {
				if ( ! jsonobj_parse_real
				    (errwin, n, &(*recs)[i].cpu, num)) 
					goto err;
				has_cpu = 1;
			} else if (0 == strcasecmp(cp, "mem")) {
				if ( ! jsonobj_parse_real
				    (errwin, n, &(*recs)[i].mem, num)) 
					goto err;
				has_mem = 1;
			} else if (0 == strcasecmp(cp, "nprocs")) {
				if ( ! jsonobj_parse_real
				    (errwin, n, &(*recs)[i].nprocs, num)) 
					goto err;
				has_procs = 1;
			} else if (0 == strcasecmp(cp, "nettx")) {
				if ( ! jsonobj_parse_int
				    (errwin, n, &(*recs)[i].nettx, num)) 
					goto err;
				has_nettx = 1;
			} else if (0 == strcasecmp(cp, "netrx")) {
				if ( ! jsonobj_parse_int
				    (errwin, n, &(*recs)[i].netrx, num)) 
					goto err;
				has_netrx = 1;
			} else if (0 == strcasecmp(cp, "discread")) {
				if ( ! jsonobj_parse_int
				    (errwin, n, &(*recs)[i].discread, num)) 
					goto err;
				has_discread = 1;
			} else if (0 == strcasecmp(cp, "discwrite")) {
				if ( ! jsonobj_parse_int
				    (errwin, n, &(*recs)[i].discwrite, num)) 
					goto err;
				has_discwrite = 1;
			} else if (0 == strcasecmp(cp, "interval")) {
				if ( ! jsonobj_parse_int(errwin, n, &ival, num)) 
					goto err;
				(*recs)[i].interval = ival;		
				has_interval = 1;
			} else if (0 == strcasecmp(cp, "id")) {
				if ( ! jsonobj_parse_int
				    (errwin, n, &(*recs)[i].id, num)) 
					goto err;
				has_id = 1;
			}
		}
		if (0 == has_ctime ||
		    0 == has_entries ||
		    0 == has_cpu ||
		    0 == has_mem ||
		    0 == has_procs ||
		    0 == has_nettx ||
		    0 == has_netrx ||
		    0 == has_discread ||
		    0 == has_discwrite ||
		    0 == has_interval ||
		    0 == has_id) {
			xwarnx(errwin, "JSON missing fields: %s", n->host);
			goto err;
		}
	}

	return 1;
err:
	free(*recs);
	*recs = NULL;
	*recsz = 0;
	return 0;
}

/*
 * Parse the full response.
 */
int
jsonobj_parse(WINDOW *errwin, 
	struct node *n, const char *str, size_t sz)
{
	struct json_value_s *s;
	struct json_object_s *obj;
	struct json_object_element_s *e;
	struct json_parse_result_s res;
	int	 rc;

	/* Consider this a recoverable error. */

	memset(&res, 0, sizeof(struct json_parse_result_s));
	s = json_parse_ex(str, sz, 0, NULL, NULL, &res);
	if (NULL == s) {
		xwarnx(errwin, "json_parse_ex: %s:%zu:%zu: %s", 
			n->host,
			res.error_line_no,
			res.error_row_no,
			json_errors[res.error]);
		fprintf(stderr, "------>------\n");
		fprintf(stderr, "%.*s\n", (int)sz, str);
		fprintf(stderr, "------<------\n");
		fflush(stderr);
		return 0;
	}

	/* Allocate, if necessary, and free existing. */

	if (NULL == n->recs) {
		n->recs = calloc(1, sizeof(struct recset));
		if (NULL == n->recs) {
			xwarn(errwin, NULL);
			free(s);
			return -1;
		}
	} 

	free(n->recs->byqmin);
	free(n->recs->bymin);
	free(n->recs->byhour);
	free(n->recs->byday);
	free(n->recs->byweek);
	free(n->recs->byyear);

	n->recs->byqminsz =
		n->recs->byminsz =
		n->recs->byhoursz = 
		n->recs->bydaysz = 
		n->recs->byweeksz = 
		n->recs->byyearsz = 0;

	obj = s->payload;
	for (e = obj->start; NULL != e; e = e->next) {
		if (0 == strcasecmp(e->name->string, "qmin")) 
			rc = jsonobj_parse_recs(errwin, n, "qmin", e,
				&n->recs->byqmin,
				&n->recs->byqminsz);
		else if (0 == strcasecmp(e->name->string, "min"))
			rc = jsonobj_parse_recs(errwin, n, "min", e,
				&n->recs->bymin,
				&n->recs->byminsz);
		else if (0 == strcasecmp(e->name->string, "hour"))
			rc = jsonobj_parse_recs(errwin, n, "hour", e,
				&n->recs->byhour,
				&n->recs->byhoursz);
		else if (0 == strcasecmp(e->name->string, "day"))
			rc = jsonobj_parse_recs(errwin, n, "day", e,
				&n->recs->byday,
				&n->recs->bydaysz);
		else if (0 == strcasecmp(e->name->string, "week"))
			rc = jsonobj_parse_recs(errwin, n, "week", e,
				&n->recs->byweek,
				&n->recs->byweeksz);
		else if (0 == strcasecmp(e->name->string, "year"))
			rc = jsonobj_parse_recs(errwin, n, "year", e,
				&n->recs->byyear,
				&n->recs->byyearsz);
		else
			continue;

		if (rc <= 0) {
			free(s);
			return rc;
		}
	}

	free(s);
	return 1;
}

