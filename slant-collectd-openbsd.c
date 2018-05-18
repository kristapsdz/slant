/*
 * Most of this file is a restatement of OpenBSD's top(1) machine.c.
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
#include <sys/param.h>
#include <sys/sched.h>
#include <sys/sysctl.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

#include "slant-collectd.h"

/*
 * Most of this file is from OpenBSD's top(1), specifically machine.c.
 * Its license is as follows.
 */


struct	sysinfo {
	int64_t         *cpu_states;
	double		 cpu_avg;
	int64_t        **cp_time;
	int64_t        **cp_old;
	int64_t        **cp_diff;
	size_t		 ncpu;
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
percentages(int cnt, int64_t *out, int64_t *new, int64_t *old, int64_t *diffs)
{
	int64_t	 change, total_change, *dp, half_total;
	int 	 i;

	/* initialization */

	total_change = 0;
	dp = diffs;

	/* calculate changes for each state and the overall change */

	for (i = 0; i < cnt; i++) {
		if ((change = *new - *old) < 0) {
			/* this only happens when the counter wraps */
			change = INT64_MAX - *old + *new;
		}
		total_change += (*dp++ = change);
		*old++ = *new++;
	}

	/* avoid divide by zero potential */

	if (total_change == 0)
		total_change = 1;

	/* calculate percentages based on overall change, rounding up */

	half_total = total_change / 2l;
	for (i = 0; i < cnt; i++)
		*out++ = ((*diffs++ * 1000 + half_total) / total_change);
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
	free(p);
}

struct sysinfo *
sysinfo_alloc(void)
{
	struct sysinfo	*p;
	size_t		 i;

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

	return p;
}

void
sysinfo_update(struct sysinfo *p)
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
			    p->cp_time[i], &size, NULL, 0) < 0)
				warn("sysctl: CTL_KERN, KERN_CPTIME2");
			percentages(CPUSTATES, tmpstate, 
				p->cp_time[i], p->cp_old[i], p->cp_diff[i]);
		}
	} else {
		int cp_time_mib[] = { CTL_KERN, KERN_CPTIME };
		size = sizeof(cp_time_tmp);

		if (sysctl(cp_time_mib, 2, 
		    cp_time_tmp, &size, NULL, 0) < 0)
			warn("sysctl: CTL_KERN, KERN_CPTIME");
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
}

double
sysinfo_get_proc_avg(const struct sysinfo *p)
{

	return p->cpu_avg;
}
