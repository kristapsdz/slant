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
#include <sys/param.h>
#include <sys/sched.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/route.h>
#include <sys/ioctl.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "slant-collectd.h"

struct 	ifcount {
	u_int64_t	ifc_ib;			/* input bytes */
	u_int64_t	ifc_ip;			/* input packets */
	u_int64_t	ifc_ie;			/* input errors */
	u_int64_t	ifc_ob;			/* output bytes */
	u_int64_t	ifc_op;			/* output packets */
	u_int64_t	ifc_oe;			/* output errors */
	u_int64_t	ifc_co;			/* collisions */
	int		ifc_flags;		/* up / down */
	int		ifc_state;		/* link state */
};

struct	ifstat {
	struct ifcount	ifs_cur;
	struct ifcount	ifs_old;
	struct ifcount	ifs_now;
	char		ifs_flag;
};

/* 
 * Define pagetok in terms of pageshift.
 */
#define PAGETOK(size, pageshift) ((size) << (pageshift))

struct	sysinfo {
	size_t		 sample; /* sample number */
	int		 pageshift; /* used for memory pages */
	double		 mem_avg; /* average memory */
	int64_t         *cpu_states; /* used for cpu compute */
	double		 cpu_avg; /* average cpu */
	int64_t        **cp_time; /* used for cpu compute */
	int64_t        **cp_old; /* used for cpu compute */
	int64_t        **cp_diff; /* used for cpu compute */
	size_t		 ncpu; /* number cpus */
	struct ifstat	*ifstats; /* used for inet compute */
	size_t		 ifstatsz; /* used for inet compute */
	struct ifcount	 ifsum; /* average inet */
};

static int
getncpu(size_t *p)
{
	int 	 mib[] = { CTL_HW, HW_NCPU };
	int 	 numcpu;
	size_t 	 size = sizeof(numcpu);

	if (sysctl(mib, sizeof(mib) / sizeof(mib[0]),
	    &numcpu, &size, NULL, 0) == -1) {
		warn("sysctl: CTL_HW, HW_NCPU");
		return 0;
	}

	*p = numcpu;
	return 1;
}

static void
percentages(int cnt, int64_t *out, 
	int64_t *new, int64_t *old, int64_t *diffs)
{
	int64_t	 change, tot, *dp, half_total;
	int 	 i;

	/* initialization */

	tot = 0;
	dp = diffs;

	/* calculate changes for each state and the overall change */

	for (i = 0; i < cnt; i++) {
		if ((change = *new - *old) < 0) {
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
	size_t	 i;

	if (NULL == p)
		return;

	for (i = 0; i < p->ncpu; i++) {
		free(p->cp_time[i]);
		free(p->cp_old[i]);
		free(p->cp_diff[i]);
	}

	free(p->cp_time);
	free(p->cp_old);
	free(p->cp_diff);
	free(p->cpu_states);
	free(p->ifstats);
	free(p);
}

struct sysinfo *
sysinfo_alloc(void)
{
	struct sysinfo	*p;
	size_t		 i;
	int		 pagesize;

	p = calloc(1, sizeof(struct sysinfo));
	if (NULL == p) {
		warn(NULL);
		return NULL;
	}

	if ( ! getncpu(&p->ncpu)) {
		warn(NULL);
		sysinfo_free(p);
		return NULL;
	}

	assert(p->ncpu > 0);

	p->cpu_states = calloc
		(p->ncpu, CPUSTATES * sizeof(int64_t));
	p->cp_time = calloc(p->ncpu, sizeof(int64_t *));
	p->cp_old = calloc(p->ncpu, sizeof(int64_t *));
	p->cp_diff = calloc(p->ncpu, sizeof(int64_t *));

	if (NULL == p->cpu_states ||
	    NULL == p->cp_time ||
	    NULL == p->cp_old ||
	    NULL == p->cp_diff) {
		warn(NULL);
		sysinfo_free(p);
		return NULL;
	}

	for (i = 0; i < p->ncpu; i++) {
		p->cp_time[i] = calloc(CPUSTATES, sizeof(int64_t));
		p->cp_old[i] = calloc(CPUSTATES, sizeof(int64_t));
		p->cp_diff[i] = calloc(CPUSTATES, sizeof(int64_t));
		if (NULL == p->cp_time[i] ||
		    NULL == p->cp_old[i] ||
		    NULL == p->cp_diff[i]) {
			warn(NULL);
			sysinfo_free(p);
			return NULL;
		}
	}

	/*
	 * get the page size with "getpagesize" and calculate pageshift
	 * from it
	 */

	pagesize = getpagesize();
	p->pageshift = 0;
	while (pagesize > 1) {
		p->pageshift++;
		pagesize >>= 1;
	}

	/* we only need the amount of log(2)1024 for our conversion */

	p->pageshift -= 10;

	return p;
}

static int
sysinfo_update_mem(struct sysinfo *p)
{
	int	 	uvmexp_mib[] = { CTL_VM, VM_UVMEXP };
	struct uvmexp 	uvmexp;
	size_t	 	size;

	size = sizeof(uvmexp);
	if (sysctl(uvmexp_mib, 2, &uvmexp, &size, NULL, 0) < 0) {
		warn("sysctl: CTL_VM, VM_UVMEXP");
		return 0;
	}

	p->mem_avg = 100.0 *
		PAGETOK(uvmexp.active, p->pageshift) /
		(double)PAGETOK(uvmexp.npages, p->pageshift);
	return 1;
}

static int
sysinfo_update_cpu(struct sysinfo *p)
{
	size_t	 i, pos, size;
	long 	 cp_time_tmp[CPUSTATES];
	int64_t	 val;
	double	 sum = 0.0;
	int64_t	*tmpstate;

	if (p->ncpu > 1) {
		int cp_time_mib[] = 
			{ CTL_KERN, KERN_CPTIME2, /*fillme*/0 };
		size = CPUSTATES * sizeof(int64_t);

		for (i = 0; i < p->ncpu; i++) {
			cp_time_mib[2] = i;
			tmpstate = p->cpu_states + (CPUSTATES * i);
			if (sysctl(cp_time_mib, 3, 
			    p->cp_time[i], &size, NULL, 0) < 0) {
				warn("sysctl: CTL_KERN, KERN_CPTIME2");
				return 0;
			}
			percentages(CPUSTATES, tmpstate, 
				p->cp_time[i], p->cp_old[i], 
				p->cp_diff[i]);
		}
	} else {
		int cp_time_mib[] = { CTL_KERN, KERN_CPTIME };
		size = sizeof(cp_time_tmp);

		if (sysctl(cp_time_mib, 2, 
		    cp_time_tmp, &size, NULL, 0) < 0) {
			warn("sysctl: CTL_KERN, KERN_CPTIME");
			return 0;
		}
		for (i = 0; i < CPUSTATES; i++)
			p->cp_time[0][i] = cp_time_tmp[i];
		percentages(CPUSTATES, p->cpu_states, p->cp_time[0],
			p->cp_old[0], p->cp_diff[0]);
	}

	/* Update our averages. */

	for (i = 0; i < p->ncpu; i++) {
		pos = i * CPUSTATES + CP_IDLE;
		val = 1000 - p->cpu_states[pos];
		if (val > 1000) {
			warnx("CPU state out of bound: %" PRId64, val);
			val = 1000;
		} else if (val < 0) {
			warnx("CPU state out of bound: %" PRId64, val);
			val = 0;
		}
		sum += val / 10.;
	}

	p->cpu_avg = sum / p->ncpu;
	return 1;
}

#define UPDATE(x, y, up) \
	do { \
		ifs->ifs_now.x = ifm.y; \
		ifs->ifs_cur.x = ifs->ifs_now.x - ifs->ifs_old.x; \
		ifs->ifs_old.x = ifs->ifs_now.x; \
		ifs->ifs_cur.x /= 15; \
		if ((up)) \
			p->ifsum.x += ifs->ifs_cur.x; \
	} while(0)

static int
sysinfo_update_if(struct sysinfo *p)
{
	struct ifstat 	*newstats, *ifs;
	struct if_msghdr ifm;
	char 		*buf, *next, *lim;
	int 		 mib[6];
	size_t 		 need, up;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = 0;
	mib[4] = NET_RT_IFLIST;
	mib[5] = 0;

	if (-1 == sysctl(mib, 6, NULL, &need, NULL, 0)) {
		warn("sysctl: CTL_NET, PF_ROUTE, NET_RT_IFLIST");
		return 0;
	} else if (NULL == (buf = malloc(need))) {
		warn(NULL);
		return 0;
	} else if (-1 == sysctl(mib, 6, buf, &need, NULL, 0)) {
		warn("sysctl: CTL_NET, PF_ROUTE, NET_RT_IFLIST");
		free(buf);
		return 0;
	}

	memset(&p->ifsum, 0, sizeof(p->ifsum));

	lim = buf + need;
	for (next = buf; next < lim; next += ifm.ifm_msglen) {
		memcpy(&ifm, next, sizeof(ifm));

		if (ifm.ifm_version != RTM_VERSION ||
		    ifm.ifm_type != RTM_IFINFO ||
		    ! (ifm.ifm_addrs & RTA_IFP))
			continue;

		if (ifm.ifm_index >= p->ifstatsz) {
			newstats = reallocarray
				(p->ifstats, ifm.ifm_index + 4,
				 sizeof(struct ifstat));
			if (NULL == newstats) {
				warn(NULL);
				free(buf);
				return 0;
			}
			p->ifstats = newstats;
			while (p->ifstatsz < ifm.ifm_index + 4) {
				memset(&p->ifstats[p->ifstatsz], 
					0, sizeof(*p->ifstats));
				p->ifstatsz++;
			}
		}

		ifs = &p->ifstats[ifm.ifm_index];

		/* Only consider non-loopback up addresses. */

		up = (ifs->ifs_cur.ifc_flags & IFF_UP) &&
			! (ifs->ifs_cur.ifc_flags & IFF_LOOPBACK);

		UPDATE(ifc_ip, ifm_data.ifi_ipackets, up);
		UPDATE(ifc_ib, ifm_data.ifi_ibytes, up);
		UPDATE(ifc_ie, ifm_data.ifi_ierrors, up);
		UPDATE(ifc_op, ifm_data.ifi_opackets, up);
		UPDATE(ifc_ob, ifm_data.ifi_obytes, up);
		UPDATE(ifc_oe, ifm_data.ifi_oerrors, up);
		UPDATE(ifc_co, ifm_data.ifi_collisions, up);

		ifs->ifs_cur.ifc_flags = ifm.ifm_flags;
		ifs->ifs_cur.ifc_state = ifm.ifm_data.ifi_link_state;
		ifs->ifs_flag++;
	}

	free(buf);
	return 1;
}

int
sysinfo_update(struct sysinfo *p)
{

	if ( ! sysinfo_update_cpu(p))
		return 0;
	if ( ! sysinfo_update_mem(p))
		return 0;
	if ( ! sysinfo_update_if(p))
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
