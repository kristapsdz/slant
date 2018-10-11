#ifndef SLANT_COLLECT_H
#define SLANT_COLLECT_H

/*
 * Configuration of things we're going to look for.
 */
struct	syscfg {
	char	**discs; /* disc devices (e.g., sd0) */
	size_t	  discsz;
	char	**cmds; /* commands (e.g., httpd) */
	size_t	  cmdsz;
};

__BEGIN_DECLS

struct sysinfo	*sysinfo_alloc(void);
int 		 sysinfo_update(const struct syscfg *, struct sysinfo *);
void 		 sysinfo_free(struct sysinfo *);

double		 sysinfo_get_cpu_avg(const struct sysinfo *);
double		 sysinfo_get_mem_avg(const struct sysinfo *);
int64_t		 sysinfo_get_nettx_avg(const struct sysinfo *);
int64_t		 sysinfo_get_netrx_avg(const struct sysinfo *);
int64_t		 sysinfo_get_discread_avg(const struct sysinfo *);
int64_t		 sysinfo_get_discwrite_avg(const struct sysinfo *);
double		 sysinfo_get_nfiles(const struct sysinfo *);
double		 sysinfo_get_nprocs(const struct sysinfo *);
double		 sysinfo_get_rprocs(const struct sysinfo *);
time_t		 sysinfo_get_boottime(const struct sysinfo *);

__END_DECLS

#endif /* ! SLANT_COLLECT_H */
