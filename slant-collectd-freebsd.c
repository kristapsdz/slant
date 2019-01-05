#ifdef __FreeBSD__
/*	$Id$ */
#include "config.h"

#include <sys/resource.h>
#include <sys/sysctl.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "slant-collectd.h"

#define GETSYSCTL(name, var) \
	getsysctl(name, &(var), sizeof(var))

struct	sysinfo {
	size_t		 ncpu; /* total number of cpus */
	long		*pcpu_cp_time; /* used for cpu compute */
	long		*pcpu_cp_old; /* used for cpu compute */
	long		*pcpu_cp_diff; /* used for cpu compute */
	int 		*pcpu_cpu_states; /* used for cpu compute */
	time_t		 boottime; /* time booted */

	double		 mem_avg; /* average memory */
	double		 cpu_avg; /* average cpu */
	double		 nproc_pct; /* nprocs percent */
	double		 nfile_pct; /* nfiles percent */
	double		 rproc_pct; /* pct command (by name) found */
	u_int64_t	 disc_rbytes; /* last disc total read */
	u_int64_t	 disc_wbytes; /* last disc total write */
	int64_t	 	 disc_ravg; /* average reads/sec */
	int64_t	 	 disc_wavg; /* average reads/sec */
};

/*
 * Get the number of configured CPUs.
 * This might be greater than the number of working CPUs.
 * Return zero on failure, non-zero on success.
 * Fills in "p" on success.
 */
static int
sysinfo_getncpu(size_t *p)
{
	int 	 numcpu;

	if (-1 == GETSYSCTL("kern.smp.maxcpus", maxcpu)) {
		warn("sysctl: kern.smp.maxcpus");
		return 0;
	}

	assert(numcpu > 0);
	*p = numcpu;
	return 1;
}

static int
sysinfo_init_boottime(struct sysinfo *p)
{
	struct timeval 	tv;

	if (-1 == GETSYSCTL("kern.boottime", tv)) {
		warn("sysctl: kern.boottime");
		return 0;
	}

	p->boottime = tv.tv_sec;
	return 1;
}

struct sysinfo *
sysinfo_alloc(void)
{
	struct sysinfo	*p;
	size_t		 size;

	p = calloc(1, sizeof(struct sysinfo));
	if (NULL == p) {
		warn(NULL);
		return NULL;
	} else if ( ! sysinfo_getncpu(&p->ncpu)) {
		warn(NULL);
		sysinfo_free(p);
		return NULL;
	}

	size = sizeof(long) * p->ncpu * CPUSTATES;

	p->pcpu_cp_time = calloc(1, size);
	p->pcpu_cp_old = calloc(p->ncpu * CPUSTATES, sizeof(long));
	p->pcpu_cp_diff = calloc(p->ncpu * CPUSTATES, sizeof(long));
	p->pcpu_cpu_states = calloc(p->ncpu * CPUSTATES, sizeof(int));

	if (NULL == p->pcpu_cp_time ||
  	    NULL == p->pcpu_cp_old ||
	    NULL == p->pcpu_cp_diff ||
	    NULL == p->pcpu_cpu_states) {
		warn(NULL);
		sysinfo_free(p);
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
}

static int
sysinfo_update_nfiles(const struct syscfg *cfg, struct sysinfo *p)
{
}

static int
sysinfo_update_nprocs(const struct syscfg *cfg, struct sysinfo *p)
{
}

static int
sysinfo_update_cpu(struct sysinfo *p)
{
}

static int
sysinfo_update_if(struct sysinfo *p)
{
}

static int
sysinfo_update_disc(const struct syscfg *cfg, struct sysinfo *p)
{
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

void
sysinfo_free(struct sysinfo *p)
{
	size_t	 i;

	if (NULL == p)
		return;

	free(p->pcpu_cp_time);
	free(p->pcpu_cp_old);
	free(p->pcpu_cp_diff);
	free(p->pcpu_cpu_states);
	free(p);
}

#endif

