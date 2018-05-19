#include <sys/queue.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "extern.h"
#include "slant.h"

static int
jsonobj_parse_int(const struct node *n,
	int64_t *res, const struct json_number_s *num)
{
	const char	*er;

	*res = strtonum(num->number, -LLONG_MAX, LLONG_MAX, &er);
	if (er == NULL)
		return 1;
	warnx("%s: bad integer: %s: %s", n->host, num->number, er);
	return 0;
}

static int
jsonobj_parse_real(const struct node *n,
	double *res, const struct json_number_s *num)
{
	char	*der;

	errno = 0;
	*res = strtod(num->number, &der);
	if (der != num->number && 0 == errno)
		return 1;
	warnx("%s: bad double: %s", n->host, num->number);
	return 0;
}

static int
jsonobj_parse_recs(const struct node *n, const char *name,
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
			 has_id;

	if (json_type_array != e->value->type) {
		warnx("%s: non-array child of %s", n->host, name);
		return 0;
	}

	ar = e->value->payload;
	*recsz = ar->length;
	*recs = calloc(*recsz, sizeof(struct record));
	if (NULL == *recs) {
		warn(NULL);
		return 0;
	}

	for (i = 0, are = ar->start; 
	     NULL != are; are = are->next, i++) {
		has_ctime = has_entries = has_cpu = 
			has_interval = has_id = 0;
		val = are->value;
		if (json_type_object != val->type) {
			warnx("%s: non-object array "
				"child of %s", n->host, name);
			goto err;
		}
		obj = val->payload;
		for (obje = obj->start; 
		     NULL != obje; obje = obje->next) {
			cp = obje->name->string;
			if (json_type_number !=
			    obje->value->type) {
				warnx("%s: expected number "
					"for array object", n->host);
				goto err;
			}
			num = obje->value->payload;
			if (0 == strcasecmp(cp, "ctime")) {
				if ( ! jsonobj_parse_int
				    (n, &(*recs)[i].ctime, num)) 
					goto err;
				has_ctime = 1;
			} else if (0 == strcasecmp(cp, "entries")) {
				if ( ! jsonobj_parse_int
				    (n, &(*recs)[i].entries, num)) 
					goto err;
				has_entries = 1;
			} else if (0 == strcasecmp(cp, "cpu")) {
				if ( ! jsonobj_parse_real
				    (n, &(*recs)[i].cpu, num)) 
					goto err;
				has_cpu = 1;
			} else if (0 == strcasecmp(cp, "interval")) {
				if ( ! jsonobj_parse_int(n, &ival, num)) 
					goto err;
				(*recs)[i].interval = ival;		
				has_interval = 1;
			} else if (0 == strcasecmp(cp, "id")) {
				if ( ! jsonobj_parse_int
				    (n, &(*recs)[i].id, num)) 
					goto err;
				has_id = 1;
			}
		}
		if (0 == has_ctime ||
		    0 == has_entries ||
		    0 == has_cpu ||
		    0 == has_interval ||
		    0 == has_id) {
			warnx("%s: missing fields", n->host);
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
 * Parse the full response 
 */
int
jsonobj_parse(struct node *n, const char *str, size_t sz)
{
	struct json_value_s *s;
	struct json_object_s *obj;
	struct json_object_element_s *e;
	int	 rc;

	if (NULL == (s = json_parse(str, sz))) {
		warnx("%s: json_parse", n->host);
		return 1;
	}

	n->dirty = 1;

	/* Allocate, if necessary, and free existing. */

	if (NULL == n->recs) {
		n->recs = calloc(1, sizeof(struct recset));
		if (NULL == n->recs) {
			warn(NULL);
			free(s);
			return 0;
		}
	} 
	free(n->recs->byqmin);
	free(n->recs->bymin);
	free(n->recs->byhour);
	n->recs->byqminsz =
		n->recs->byminsz =
		n->recs->byhoursz = 0;

	obj = s->payload;
	for (e = obj->start; NULL != e; e = e->next) {
		if (0 == strcasecmp(e->name->string, "qmin")) {
			rc = jsonobj_parse_recs(n, "qmin", e,
				&n->recs->byqmin,
				&n->recs->byqminsz);
		} else if (0 == strcasecmp(e->name->string, "min")) {
			rc = jsonobj_parse_recs(n, "min", e,
				&n->recs->bymin,
				&n->recs->byminsz);
		} else if (0 == strcasecmp(e->name->string, "hour")) {
			rc = jsonobj_parse_recs(n, "hour", e,
				&n->recs->byhour,
				&n->recs->byhoursz);
		} else
			continue;

		if (0 == rc) {
			free(s);
			return 0;
		}
	}

	free(s);
	return 1;
}

