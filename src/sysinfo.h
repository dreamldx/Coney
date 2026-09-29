#ifndef SYSINFO_H
#define SYSINFO_H

#include <stdbool.h>
#include <stddef.h>

/* CPU load in percent since the previous call. The first call returns 0. */
int sysinfo_cpu_percent(void);

/* Used memory in percent (total minus MemAvailable), or -1 on error. */
int sysinfo_mem_percent(void);

/* SoC temperature in degrees Celsius. Returns false if it cannot be read. */
bool sysinfo_temp_c(float *out);

/* First non-loopback, non-link-local IPv4 address as text. Returns false if there is none. */
bool sysinfo_ip(char *buf, size_t size);

#endif /* SYSINFO_H */
