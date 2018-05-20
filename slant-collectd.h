#ifndef SLANT_COLLECT_H
#define SLANT_COLLECT_H

__BEGIN_DECLS

struct sysinfo	*sysinfo_alloc(void);
void 		 sysinfo_update(struct sysinfo *);
void 		 sysinfo_free(struct sysinfo *);

double		 sysinfo_get_cpu_avg(const struct sysinfo *);
double		 sysinfo_get_mem_avg(const struct sysinfo *);

__END_DECLS

#endif /* ! SLANT_COLLECT_H */
