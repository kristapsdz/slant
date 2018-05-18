#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <ksql.h>

#include "slant-collectd.h"
#include "db.h"

static	sig_atomic_t	doexit = 0;

static void
sig(int sig)
{

	doexit = 1;
}

/*
 * Update the database "db" given the current record "p" and all
 * existing database records "rq".
 */
static void
update(struct kwbp *db, const struct sysinfo *p, 
	const struct record_q *rq)
{
	size_t	 	 bymin = 0, byhour = 0, byqmin = 0;
	time_t		 t = time(NULL);
	const struct record *r, 
	      		*first_bymin = NULL, *last_bymin = NULL,
			*first_byqmin = NULL, *last_byqmin = NULL,
			*first_byhour = NULL, *last_byhour = NULL;

	/* 
	 * First count what we have.
	 * We need this when determining how many "spare" entries to
	 * keep in any given interval, e.g., we keep 5 minutes of
	 * quarter-minute interval data, but only really need the last
	 * single minute for accumulation.
	 */

	TAILQ_FOREACH(r, rq, _entries)
		switch (r->interval) {
		case INTERVAL_byqmin:
			if (NULL == first_byqmin)
				first_byqmin = r;
			last_byqmin = r;
			byqmin++;
			break;
		case INTERVAL_bymin:
			if (NULL == first_bymin)
				first_bymin = r;
			last_bymin = r;
			bymin++;
			break;
		case INTERVAL_byhour:
			if (NULL == first_byhour)
				first_byhour = r;
			last_byhour = r;
			byhour++;
			break;
		}

	/* 
	 * Start by pushing the current quarter-minute.
	 * We keep the last five minutes (in quarter-minute intervals)
	 * simply for a finer-look at the recent time.
	 */

	if (byqmin > (4 * 5)) {
		assert(NULL != last_byqmin);
		db_record_update_tail(db, t, 1, 
			sysinfo_get_proc_avg(p), last_byqmin->id);
	} else
		db_record_insert(db, t, 1,
			sysinfo_get_proc_avg(p), INTERVAL_byqmin);

	/* 
	 * Augment our first by-minute entry.
	 * Make sure that we don't augment a record when we're past the
	 * time it'd matter.
	 */

	if (NULL == first_bymin || 
	    first_bymin->ctime + 60 <= t) {
		if (bymin > (60 * 5)) {
			assert(NULL != first_bymin);
			assert(NULL != last_bymin);
			db_record_update_tail(db, t, 1, 
				sysinfo_get_proc_avg(p),
				last_bymin->id);
		} else
			db_record_insert(db, t, 1, 
				sysinfo_get_proc_avg(p),
				INTERVAL_bymin);
	} else
		db_record_update_current(db, 
			first_bymin->entries + 1,
			first_bymin->cpu + 
			 sysinfo_get_proc_avg(p),
			first_bymin->id);

	/* 
	 * Now augment our first by-hour entry.
	 * There are sixty minute entries in the newest bucket.
	 * We keep the last five days.
	 */

	if (NULL == first_byhour || 
	   first_byhour->ctime + 60 * 60 <= t) {
		if (byhour > (24 * 5)) {
			assert(NULL != first_byhour);
			assert(NULL != last_byhour);
			db_record_update_tail(db, t, 1, 
				sysinfo_get_proc_avg(p),
				last_byhour->id);
		} else
			db_record_insert(db, t, 1, 
				sysinfo_get_proc_avg(p),
				INTERVAL_byhour);
	} else
		db_record_update_current(db, 
			first_byhour->entries + 1,
			first_byhour->cpu + 
			 sysinfo_get_proc_avg(p),
			first_byhour->id);
}

int
main(int argc, char *argv[])
{
	struct kwbp	*db;
	struct record_q	*rq;
	struct sysinfo	*info;
	int		 c;
	const char	*dbfile = "/var/www/data/slant.db";

	/*
	 * Pre-pledge, establishing a reasonable baseline.
	 * Then open our database in a protected process.
	 * After that, drop us to minimum privilege/role.
	 */

	if (-1 == pledge
	    ("ps stdio rpath cpath wpath flock proc fattr", NULL))
		err(EXIT_FAILURE, "pledge");

	while (-1 != (c = getopt(argc, argv, "f:")))
		switch (c) {
		case 'f':
			dbfile = optarg;
			break;
		default:
			goto usage;
		}

	/* XXX: hack around ksql(3) exit when receives signal. */

	if (SIG_ERR == signal(SIGINT, SIG_IGN))
		err(EXIT_FAILURE, "signal");
	if (SIG_ERR == signal(SIGTERM, SIG_IGN))
		err(EXIT_FAILURE, "signal");

	db = db_open(dbfile);
	if (NULL == db)
		errx(EXIT_FAILURE, "%s", dbfile);

	if (-1 == pledge("ps stdio", NULL))
		err(EXIT_FAILURE, "pledge");

	db_role(db, ROLE_produce);

	/*
	 * From here on our, use the "out" label for bailing on errors,
	 * which will clean up behind us.
	 * Let SIGINT and SIGTERM trigger us into exiting safely.
	 */

	if (NULL == (info = sysinfo_alloc()))
		goto out;

	if (SIG_ERR == signal(SIGINT, sig) ||
	    SIG_ERR == signal(SIGTERM, sig)) {
		warn("signal");
		goto out;
	}

	/*
	 * Now enter our main loop.
	 * The body will run every 15 seconds.
	 * Start each iteration by grabbing the current system state
	 * using sysctl(3).
	 * Then grab what we have in the database.
	 * Lastly, modify the database state given our current.
	 */

	while ( ! doexit) {
		if (sleep(15 - ((time(NULL) + 1) % 15)))
			break;
		sysinfo_update(info);
		rq = db_record_list_lister(db);
		update(db, info, rq);
		db_record_freeq(rq);
	}

out:
	sysinfo_free(info);
	db_close(db);
	return EXIT_SUCCESS;
usage:
	fprintf(stderr, "usage: %s [-f dbfile]\n", getprogname());
	return EXIT_FAILURE;
}
