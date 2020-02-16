#ifdef __linux__
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

#include <net/if.h>

#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/*
 * Sector size is always 512
 * https://lkml.org/lkml/2015/8/17/269
 */
#define SECTOR_SHIFT 9

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
	uint64_t	 cpu_states[CPUSTATES]; /* used for cpu compute */
	double		 cpu_avg; /* average cpu */
	uint64_t	 cp_time[CPUSTATES]; /* used for cpu compute */
	uint64_t         cp_old[CPUSTATES]; /* used for cpu compute */
	uint64_t         cp_diff[CPUSTATES]; /* used for cpu compute */
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

static char buf[8192];

static ssize_t
proc_read_buf(const char *file)
{
	int fd;
	ssize_t rd;
	if (-1 == (fd = open(file, O_RDONLY))) {
		warn("open: %s", file);
		return -1;
	}
	if (-1 == (rd = read(fd, buf, sizeof buf - 1))) {
		warn("read: %s", file);
		close(fd);
		return -1;
	}
	close(fd);
	buf[rd] = '\0';
#ifdef DEBUG
	warnx("%s: read %ld bytes", file, rd);
#endif
	return rd;
}

static int
sysinfo_update_mem(struct sysinfo *p)
{
	ssize_t rd;
	uint64_t memtotal, memfree;
	char *ptr;

	rd = proc_read_buf("/proc/meminfo");
	if (-1 == rd)
		return 0;

	if (NULL == (ptr = memmem(buf, rd, "MemTotal:", 9)))
		goto errparse;
	if (1 != sscanf(ptr+9, "%" SCNu64, &memtotal))
		goto errparse;

	if (NULL == (ptr = memmem(buf, rd, "MemFree:", 8)))
		goto errparse;
	if (1 != sscanf(ptr+8, "%" SCNu64, &memfree))
		goto errparse;

	p->mem_avg = 100.0 * (memtotal - memfree) / memtotal;

#ifdef DEBUG
	warnx("memtotal=%ld memfree=%ld mem_avg=%lf", memtotal, memfree, p->mem_avg);
#endif

	return 1;
errparse:
	warnx("error while parsing /proc/meminfo");
	return 0;
}

static int
sysinfo_update_nfiles(const struct syscfg *cfg, struct sysinfo *p)
{
	uint64_t	allocfiles, unusedfiles, maxfiles;
	ssize_t		rd;

	rd = proc_read_buf("/proc/sys/fs/file-nr");
	if (-1 == rd)
		return 0;

	if (3 != sscanf(buf, "%" SCNu64 " %" SCNu64 " %" SCNu64,
	    &allocfiles, &unusedfiles, &maxfiles)) {
		warnx("failed to parse /proc/sys/fs/file-nr");
		return 0;
	}

#ifdef DEBUG
	warnx("allocfiles=%ld maxfiles=%ld", allocfiles, maxfiles);
#endif

	p->nfile_pct = 100.0 * allocfiles / (double)maxfiles;
	return 1;
}

static int
sysinfo_update_nprocs(const struct syscfg *cfg, struct sysinfo *p)
{
	struct dirent	*dent;
	DIR 		*dir;
	uint64_t	 maxproc, nprocs = 0;
	ssize_t		 rd;

	rd = proc_read_buf("/proc/sys/kernel/pid_max");
	if (-1 == rd)
		return 0;

	if (1 != sscanf(buf, "%" SCNu64, &maxproc)) {
		warnx("error while parsing /proc/sys/kernel/pid_max");
		return 0;
	}

	dir = opendir("/proc");
	if (NULL == dir) {
		warn("opendir: /proc");
		return 0;
	}

	while (NULL != (dent = readdir(dir))) {
		if (isdigit(*dent->d_name))
			nprocs++;
	}
	closedir(dir);

#ifdef DEBUG
	warnx("procs: nprocs=%" PRIu64 " maxproc=%" PRIu64, nprocs, maxproc);
#endif

	p->nproc_pct = 100.0 * nprocs / (double)maxproc;

	/*
	 * FIXME: add rproc support
	 */
	p->rproc_pct = .0;

	return 1;
}

static int
sysinfo_update_cpu(struct sysinfo *p)
{
	int64_t	val;
	ssize_t	rd;

	rd = proc_read_buf("/proc/stat");
	if (-1 == rd)
		return 0;

	if (10 != sscanf(buf, "cpu"
	    " %" SCNu64
	    " %" SCNu64
	    " %" SCNu64
	    " %" SCNu64
	    " %" SCNu64
	    " %" SCNu64
	    " %" SCNu64
	    " %" SCNu64
	    " %" SCNu64
	    " %" SCNu64,
	    &p->cp_time[0],
	    &p->cp_time[1],
	    &p->cp_time[2],
	    &p->cp_time[3],
	    &p->cp_time[4],
	    &p->cp_time[5],
	    &p->cp_time[6],
	    &p->cp_time[7],
	    &p->cp_time[8],
	    &p->cp_time[9])) {
		warnx("error while parsing /proc/stat");
		return 0;
	}

	percentages(CPUSTATES, p->cpu_states, &p->cp_time[0],
		&p->cp_old[0], &p->cp_diff[0]);

#ifdef DEBUG
	for (int i = 0; i < CPUSTATES; i++)
		warnx("%d: cp_time=%ld cp_old=%ld cp_diff=%ld",
			i, p->cp_time[i], p->cp_old[i], p->cp_diff[i]);
#endif

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
get_ifflags(int *fd, const char *ifname, short *ifflags)
{
	static struct	ifreq ifr;

	if (-1 == *fd &&
	    -1 == (*fd = socket(AF_UNIX, SOCK_DGRAM, 0))) {
		warn("socket");
		return 0;
	}

	if (strlcpy(ifr.ifr_name, ifname, sizeof (ifr.ifr_name)) >=
	    sizeof (ifr.ifr_name)) {
		warnx("ifname too long");
		return 0;
	}

	if (-1 == ioctl(*fd, SIOCGIFFLAGS, &ifr)) {
		warn("ioctl: SIOCGIFFLAGS");
		return 0;
	}

	*ifflags = ifr.ifr_flags;

	return 1;
}

static int
get_ifindex(struct if_nameindex *idx, const char *ifname, int *ifindex)
{
	struct if_nameindex	*next;
	for (next = idx; (next->if_index && next->if_name); next++) {
		if (0 == strcmp(ifname, next->if_name)) {
			*ifindex = next->if_index;
			return 1;
		}
	}
	return 0;
}

#define UPDATE(x, y, up) \
	do { \
		ifs->ifs_now.x = y; \
		ifs->ifs_cur.x = ifs->ifs_now.x - ifs->ifs_old.x; \
		ifs->ifs_old.x = ifs->ifs_now.x; \
		ifs->ifs_cur.x /= 15; \
		if ((up)) \
			p->ifsum.x += ifs->ifs_cur.x; \
	} while(0)

static int
sysinfo_update_if(struct sysinfo *p)
{
	struct ifcount		 ifctmp;
	struct ifstat 		*newstats, *ifs;
	struct if_nameindex	*idx;
	char			*ptr, *ifname;
	ssize_t			 rd;
	int			 ifindex, up, sockfd = -1;
	short			 flags;

	idx = if_nameindex();
	if (NULL == idx)
		return 0;

	rd = proc_read_buf("/proc/net/dev");
	if (-1 == rd)
		goto err;

	/*
	 * Skip two header lines
	 */
	if (NULL == (ptr = strchr(buf, '\n')) ||
	    NULL == (ptr = strchr(ptr+1, '\n')) ||
	    '\0' == *++ptr)
		goto errparse;


	memset(&p->ifsum, 0, sizeof(p->ifsum));

	for (; ptr < buf+rd; ptr++) {
		/*
		 * Get interface name, skip leading spaces
		 */
		for (;*ptr == ' '; ptr++) ;
		ifname = ptr;
		if (NULL == (ptr = strchr(ptr, ':')))
			goto errparse;
		*ptr++ = '\0';

		if ( ! get_ifindex(idx, ifname, &ifindex)) {
			warnx("couldn't find ifindex for '%s'", ifname);
			goto err;
		}

		sscanf(ptr,
		    " %" SCNu64  // rx-bytes
		    " %" SCNu64  // rx-packets
		    " %" SCNu64  // rx-errors
		    " %*u"       // rx-drop
		    " %*u"       // rx-fifo
		    " %*u"       // rx-frame
		    " %*u"       // rx-compressed
		    " %*u"       // rx-multicast
		    " %" SCNu64  // tx-bytes
		    " %" SCNu64  // tx-packates
		    " %" SCNu64  // tx-errors
		    " %*u"       // tx-drop
		    " %*u"       // tx-fifo
		    " %" SCNu64  // tx-collisions
		    " %*u"       // tx-carrier
		    " %*u" ,     // tx-compressed
		    &ifctmp.ifc_ib,
		    &ifctmp.ifc_ip,
		    &ifctmp.ifc_ie,
		    &ifctmp.ifc_ob,
		    &ifctmp.ifc_op,
		    &ifctmp.ifc_oe,
		    &ifctmp.ifc_co);

		if ( ! get_ifflags(&sockfd, ifname, &flags))
			goto err;

		if ((unsigned int)ifindex >= p->ifstatsz) {
			newstats = reallocarray
				(p->ifstats, ifindex + 4,
				 sizeof(struct ifstat));
			if (NULL == newstats) {
				warn(NULL);
				goto err;
			}
			p->ifstats = newstats;
			while (p->ifstatsz < (unsigned int)ifindex + 4) {
				memset(&p->ifstats[p->ifstatsz], 
					0, sizeof(*p->ifstats));
				p->ifstatsz++;
			}
		}
		ifs = &p->ifstats[ifindex];

		/* Only consider non-loopback up addresses. */

		up = (flags & IFF_UP) && ! (flags & IFF_LOOPBACK);

#ifdef DEBUG
		warnx("%s: ifindex=%d up=%d", ifname, ifindex, up);
#endif

		UPDATE(ifc_ip, ifctmp.ifc_ip, up);
		UPDATE(ifc_ib, ifctmp.ifc_ib, up);
		UPDATE(ifc_ie, ifctmp.ifc_ie, up);
		UPDATE(ifc_op, ifctmp.ifc_op, up);
		UPDATE(ifc_ob, ifctmp.ifc_ob, up);
		UPDATE(ifc_oe, ifctmp.ifc_oe, up);
		UPDATE(ifc_co, ifctmp.ifc_co, up);

		if (NULL == (ptr = strchr(ptr, '\n')))
			goto errparse;
	}

	close(sockfd);
	if_freenameindex(idx);
	return 1;
errparse:
	warnx("error while parsing /proc/net/dev");
err:
	close(sockfd);
	if_freenameindex(idx);
	return 0;
}

static int
is_real_block_device(char *name)
{
	char	 path[PATH_MAX];
	char	*p;

	/*
	 * iostat replaces slashes in device names with `!`
	 */
	p = name;
	while (NULL != (p = strchr(p, '/')))
		*p = '!';

	/*
	 * real devices have the `device` symlink in their
	 * `/sys/block/<disk>` directory.
	 */
	snprintf(path, sizeof path, "/sys/block/%s/device", name);
	return 0 == access(path, F_OK);
}

static int
sysinfo_update_disc(const struct syscfg *cfg, struct sysinfo *p)
{
	ssize_t		 rd;
	char		 name[128];
	unsigned long	 secrd, secwr;
	char		*ptr;
	uint64_t	 rs = 0, ws = 0;
	uint64_t	 rb = 0, wb = 0;

	rd = proc_read_buf("/proc/diskstats");
	if (-1 == rd)
		return 0;

	for (ptr = buf; ptr < buf+rd; ptr++) {
		/*
		 * field 3  = device name
		 * field 6  = sectors read
		 * field 10 = sectors written
		 */
		if (3 != sscanf(ptr, "%*u %*u %127s "
		    "%*u %*u %lu %*u %*u %*u %lu %*u %*u %*u %*u",
		    name, &secrd, &secwr))
			goto errparse;
		if (NULL == (ptr = strchr(ptr, '\n')))
			goto errparse;
		if ( ! is_real_block_device(name))
			continue;
		rs += secrd;
		ws += secwr;
	}

	rb = rs << SECTOR_SHIFT;
	wb = ws << SECTOR_SHIFT;

	if (rb > p->disc_rbytes) {
		p->disc_ravg = (rb - p->disc_rbytes) / 15;
		p->disc_rbytes = rb;
	} else {
		p->disc_ravg = 0;
		p->disc_rbytes = rb;
	}

	if (wb > p->disc_wbytes) {
		p->disc_wavg = (wb - p->disc_wbytes) / 15;
		p->disc_wbytes = wb;
	} else {
		p->disc_wavg = 0;
		p->disc_wbytes = wb;
	}

#ifdef DEBUG
	warnx("disc: rbytes=%lu wbytes=%lu", p->disc_rbytes, p->disc_rbytes);
#endif

	return 1;
errparse:
	warnx("error while parsing /proc/diskstats");
	return 0;
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
#endif
