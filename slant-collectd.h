#ifndef SLANT_COLLECT_H
#define SLANT_COLLECT_H

struct	syscfg {
	char	**discs;
	size_t	  discsz;
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
double		 sysinfo_get_nprocs(const struct sysinfo *);

__END_DECLS

#endif /* ! SLANT_COLLECT_H */
