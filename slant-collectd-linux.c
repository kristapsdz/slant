/*	$Id$ */
/*
 * A lot of this file is a restatement of OpenBSD's top(1) machine.c.
 * Its copyright and license file is retained below.
 */
/*-
 * Copyright (c) 1994 Thorsten Lockert <tholo@sigmasoft.com>
 * Copyright (c) 2018 Thorsten Lockert <tholo@sigmasoft.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Other parts are from OpenBSD's systat(1) if.c.
 * Its copyright and license file is retained below.
 */
/*
 * Copyright (c) 2004 Markus Friedl <markus@openbsd.org>
 * Copyright (c) 2018 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2018 Duncan Overbruck <mail@duncano.de>
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
#include "config.h"

#include <linux/if.h>

#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <stdarg.h>
#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <ksql.h>

#include "slant-collectd.h"
#include "extern.h"
#include "db.h"

/*
 * /proc/stat
 */
#define CP_USER 0
#define CP_NICE 1
#define CP_SYS 2
#define CP_IDLE 3
#define CP_IOWAIT 4
#define CP_IRQ 5
#define CP_SOFTIRQ 6
#define CP_STEAL 7
#define CP_GUEST 8
#define CP_GUEST_NICE 9
#define CPUSTATES 10

struct 	ifcount {
	uint64_t	ifc_ib;			/* input bytes */
	uint64_t	ifc_ip;			/* input packets */
	uint64_t	ifc_ie;			/* input errors */
	uint64_t	ifc_ob;			/* output bytes */
	uint64_t	ifc_op;			/* output packets */
	uint64_t	ifc_oe;			/* output errors */
	uint64_t	ifc_co;			/* collisions */
};

struct	ifstat {
	struct ifcount	ifs_cur;
	struct ifcount	ifs_old;
	struct ifcount	ifs_now;
};

struct	sysinfo {
	size_t		 sample; /* sample number */
	double		 mem_avg; /* average memory */
	double		 nproc_pct; /* nprocs percent */
	double		 nfile_pct; /* nfiles percent */
	uint64_t      cpu_states[CPUSTATES]; /* used for cpu compute */
	double		 cpu_avg; /* average cpu */
	uint64_t        cp_time[CPUSTATES]; /* used for cpu compute */
	uint64_t        cp_old[CPUSTATES]; /* used for cpu compute */
	uint64_t        cp_diff[CPUSTATES]; /* used for cpu compute */
	double		 rproc_pct; /* pct command (by name) found */
	struct ifstat	*ifstats; /* used for inet compute */
	size_t		 ifstatsz; /* used for inet compute */
	struct ifcount	 ifsum; /* average inet */
	u_int64_t	 disc_rbytes; /* last disc total read */
	u_int64_t	 disc_wbytes; /* last disc total write */
	int64_t	 	 disc_ravg; /* average reads/sec */
	int64_t	 	 disc_wavg; /* average reads/sec */
	time_t		 boottime; /* time booted */
};

static void
percentages(int cnt, uint64_t *out, 
	uint64_t *new, uint64_t *old, uint64_t *diffs)
{
	uint64_t 	change, tot, *dp, half_total;
	int 	 i;

	/* initialization */

	tot = 0;
	dp = diffs;

	/* calculate changes for each state and the overall change */

	for (i = 0; i < cnt; i++) {
		if ((int64_t)(change = *new - *old) < 0) {
			/* this only happens when the counter wraps */
			change = INT64_MAX - *old + *new;
		}
		tot += (*dp++ = change);
		*old++ = *new++;
	}

	/* avoid divide by zero potential */

	if (tot == 0)
		tot = 1;

	/* calculate percentages based on overall change, rounding up */

	half_total = tot / 2l;
	for (i = 0; i < cnt; i++)
		*out++ = ((*diffs++ * 1000 + half_total) / tot);
}

void
sysinfo_free(struct sysinfo *p)
{
	if (NULL == p)
		return;

	free(p->ifstats);
	free(p);
}

static int
sysinfo_init_boottime(struct sysinfo *p)
{
	FILE *fp;
	char *line = NULL;
	size_t n = 0;
	uint64_t btime = 0;

	if ((fp = fopen("/proc/stat", "r")) == NULL) {
		warn("open: /proc/stat");
		return 0;
	}

	while (-1 != getline(&line, &n, fp)) {
		if (0 == strcmp("btime ", line)) {
			if (1 != scanf(line+6, "%" SCNu64, &btime)) {
				warnx("failed to get boot time");
				fclose(fp);
				free(line);
				return 0;
			}
			break;
		}
	}

	fclose(fp);
	free(line);

	p->boottime = btime;

	return 1;
}

struct sysinfo *
sysinfo_alloc(void)
{
	struct sysinfo	*p;

	p = calloc(1, sizeof(struct sysinfo));
	if (NULL == p) {
		warn(NULL);
		return NULL;
	}

	if ( ! sysinfo_init_boottime(p)) {
		sysinfo_free(p);
		return NULL;
	}

	return p;
}

static int
sysinfo_update_mem(struct sysinfo *p)
{
	FILE *fp;
	char *line;
	size_t n;
	size_t total, memfree, values;

	if ((fp = fopen("/proc/meminfo", "r")) == NULL) {
		warn("open: /proc/meminfo");
		return 0;
	}

	line = NULL;
	n = 0;
	total = memfree = values = 0;

	while (-1 != getline(&line, &n, fp)) {
		if (0 == strncmp(line, "MemTotal:", 9)) {
			if (1 != sscanf(line+9, "%" SCNu64, &total)) {
				warnx("failed to read MemTotal from /proc/meminfo");
				fclose(fp);
				free(line);
				return 0;
			}
			values++;
		} else if (0 == strncmp(line, "MemFree:", 8)) {
			if (1 != sscanf(line+8, "%" SCNu64, &memfree)) {
				warnx("failed to read MemFree from /proc/meminfo");
				fclose(fp);
				free(line);
				return 0;
			}
			values++;
		}
		if (values == 2)
			break;
	}
	fclose(fp);
	free(line);

	if (values != 2) {
		warnx("missing value /proc/meminfo");
		return 0;
	}

	p->mem_avg = 100.0 * memfree / total;

	return 1;
}

static int
sysinfo_update_nfiles(const struct syscfg *cfg, struct sysinfo *p)
{
	FILE *fp;
	uint64_t allocfiles, unusedfiles, maxfiles;

	if (NULL == (fp = fopen("/proc/sys/fs/file-nr", "r"))) {
		warn("fopen: /proc/sys/fs/file-nr");
		return 0;
	}
	if (3 != fscanf(fp, "%" SCNu64 " %" SCNu64 " %" SCNu64 "\n",
	    &allocfiles, &unusedfiles, &maxfiles)) {
		warnx("failed to parse /proc/sys/fs/file-nr");
		fclose(fp);
		return 0;
	}
	fclose(fp);
	p->nfile_pct = 100.0 * allocfiles / (double)maxfiles;
	return 1;
}

static int
sysinfo_update_nprocs(const struct syscfg *cfg, struct sysinfo *p)
{
	return 1;
}

static int
sysinfo_update_cpu(struct sysinfo *p)
{
	int64_t	 val;

	FILE *fp;

	if ((fp = fopen("/proc/stat", "r")) == NULL) {
		warn("open: /proc/stat");
		return 0;
	}

	int r = 0;
	r = fscanf(fp, "cpu %" SCNu64
	    " %" SCNu64
		" %" SCNu64
		" %" SCNu64
		" %" SCNu64
		" %" SCNu64
		" %" SCNu64
		" %" SCNu64
		" %" SCNu64
		" %" SCNu64
		"\n",
		&p->cp_time[0],
		&p->cp_time[1],
		&p->cp_time[2],
		&p->cp_time[3],
		&p->cp_time[4],
		&p->cp_time[5],
		&p->cp_time[6],
		&p->cp_time[7],
		&p->cp_time[8],
		&p->cp_time[9]);

	fclose(fp);

	if (r != 10) {
		warnx("failed to parse /proc/stat");
		return 0;
	}

	percentages(CPUSTATES, p->cpu_states, &p->cp_time[0],
		&p->cp_old[0], &p->cp_diff[0]);

	/* Update our averages. */

	val = 1000 - p->cpu_states[CP_IDLE];
	if (val > 1000) {
		warnx("CPU state out of bound: %" PRId64, val);
		val = 1000;
	} else if (val < 0) {
		warnx("CPU state out of bound: %" PRId64, val);
		val = 0;
	}

	p->cpu_avg = val / 10.;

	return 1;
}

static int
sysfs_scan_at(int dirfd, const char *file, const char *format, ...)
{
	va_list ap;
	int fd;
	FILE *fp;
	fd = openat(dirfd, file, O_RDONLY);
	if (-1 == fd) {
		warn("openat: %s", file);
		return 0;
	}
	fp = fdopen(fd, "r");
	if (NULL == fp) {
		warn("fdopen: %s", file);
		close(fd);
		return 0;
	}
	va_start(ap, format);
	int ret = vfscanf(fp, format, ap);
	va_end(ap);
	fclose(fp);
	close(fd);
	return ret;
}

#define SCAN(file, fmt, ...) \
	do { \
		if (1 != sysfs_scan_at(ifdirfd, file, fmt, __VA_ARGS__)) { \
			warnx("scan: %s", file); \
		} \
	} while(0)

#define UPDATE(x, y) \
	do { \
		ifs->ifs_now.x = 0; \
		if (1 == sysfs_scan_at(ifdirfd, y, "%" SCNu64 "\n", &ifs->ifs_now.x)) { \
			ifs->ifs_cur.x = ifs->ifs_now.x - ifs->ifs_old.x; \
			ifs->ifs_old.x = ifs->ifs_now.x; \
			ifs->ifs_cur.x /= 15; \
			if (up) \
				p->ifsum.x += ifs->ifs_cur.x; \
		} else { \
			warnx("scan: %s", y); \
		}\
	} while(0)

static int
sysinfo_update_if(struct sysinfo *p)
{
	DIR *dir;
	struct ifstat 	*newstats, *ifs;
	struct dirent	*dirent;
	uint64_t	ifindex;
	int		up;
	size_t		i = 0;
	uint32_t	flags;
	int		dirfd, ifdirfd;

	if (-1 == (dirfd = open("/sys/class/net", O_DIRECTORY))) {
		warn("open: /sys/class/net");
		return 0;
	}
	if (NULL == (dir = fdopendir(dirfd))) {
		warn("opendir: /sys/class/net");
		return 0;
	}

	memset(&p->ifsum, 0, sizeof(p->ifsum));

	while (NULL != (dirent = readdir(dir))) {
		if (dirent->d_name[0] == '.')
			continue;
		ifdirfd = openat(dirfd, dirent->d_name, O_DIRECTORY);
		if (-1 == ifdirfd) {
			closedir(dir);
			close(dirfd);
			return 0;
		}

		if (i >= p->ifstatsz) {
			newstats = reallocarray
				(p->ifstats, i + 4,
				 sizeof(struct ifstat));
			if (NULL == newstats) {
				warn(NULL);
				closedir(dir);
				close(dirfd);
				close(ifdirfd);
				return 0;
			}
			p->ifstats = newstats;
			while (p->ifstatsz < i + 4) {
				memset(&p->ifstats[p->ifstatsz], 
					0, sizeof(*p->ifstats));
				p->ifstatsz++;
			}
		}


		flags = ifindex = 0;

		SCAN("ifindex", "%" SCNu64 "\n", &ifindex);
		ifindex--;

		ifs = &p->ifstats[ifindex];

		SCAN("flags", "%" SCNx32 "\n", &flags);
		up = (flags & IFF_UP) && ! (flags & IFF_LOOPBACK);

		UPDATE(ifc_ip, "statistics/rx_packets");
		UPDATE(ifc_ib, "statistics/rx_bytes");
		UPDATE(ifc_ie, "statistics/rx_errors");
		UPDATE(ifc_op, "statistics/tx_packets");
		UPDATE(ifc_ob, "statistics/tx_bytes");
		UPDATE(ifc_oe, "statistics/tx_errors");
		UPDATE(ifc_co, "statistics/collisions");

		close(ifdirfd);
	}
	closedir(dir);
	close(dirfd);

	return 1;
}

static int
sysinfo_update_disc(const struct syscfg *cfg, struct sysinfo *p)
{
	return 1;
}

int
sysinfo_update(const struct syscfg *cfg, struct sysinfo *p)
{
	if ( ! sysinfo_update_nprocs(cfg, p))
		return 0;
	if ( ! sysinfo_update_nfiles(cfg, p))
		return 0;
	if ( ! sysinfo_update_cpu(p))
		return 0;
	if ( ! sysinfo_update_mem(p))
		return 0;
	if ( ! sysinfo_update_if(p))
		return 0;
	if ( ! sysinfo_update_disc(cfg, p))
		return 0;

	p->sample++;
	return 1;
}

double
sysinfo_get_cpu_avg(const struct sysinfo *p)
{

	return p->cpu_avg;
}

double
sysinfo_get_mem_avg(const struct sysinfo *p)
{

	return p->mem_avg;
}

int64_t
sysinfo_get_nettx_avg(const struct sysinfo *p)
{

	if (1 == p->sample)
		return 0;
	return p->ifsum.ifc_ob;
}

int64_t
sysinfo_get_netrx_avg(const struct sysinfo *p)
{

	if (1 == p->sample)
		return 0;
	return p->ifsum.ifc_ib;
}

int64_t
sysinfo_get_discread_avg(const struct sysinfo *p)
{

	if (1 == p->sample)
		return 0;
	return p->disc_ravg;
}

int64_t
sysinfo_get_discwrite_avg(const struct sysinfo *p)
{

	if (1 == p->sample)
		return 0;
	return p->disc_wavg;
}

double
sysinfo_get_rprocs(const struct sysinfo *p)
{

	return p->rproc_pct;
}

double
sysinfo_get_nfiles(const struct sysinfo *p)
{

	return p->nfile_pct;
}

double
sysinfo_get_nprocs(const struct sysinfo *p)
{

	return p->nproc_pct;
}

time_t
sysinfo_get_boottime(const struct sysinfo *p)
{

	return p->boottime;
}
