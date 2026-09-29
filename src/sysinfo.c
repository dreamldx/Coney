#include "sysinfo.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

int sysinfo_cpu_percent(void)
{
    static unsigned long long prev_total, prev_idle;
    static bool have_prev;

    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;
    unsigned long long user, nice, sys, idle, iowait, irq, softirq, steal;
    int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal);
    fclose(f);
    if (n != 8) return 0;

    unsigned long long idle_all = idle + iowait;
    unsigned long long total = user + nice + sys + idle_all + irq + softirq + steal;
    int pct = 0;
    if (have_prev && total > prev_total) {
        unsigned long long dt = total - prev_total;
        unsigned long long di = idle_all - prev_idle;
        pct = (int)((dt - di) * 100 / dt);
    }
    prev_total = total;
    prev_idle = idle_all;
    have_prev = true;
    return pct;
}

int sysinfo_mem_percent(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    unsigned long long total = 0, avail = 0;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        sscanf(line, "MemTotal: %llu kB", &total);
        sscanf(line, "MemAvailable: %llu kB", &avail);
    }
    fclose(f);
    if (!total || avail > total) return -1;
    return (int)((total - avail) * 100 / total);
}

bool sysinfo_temp_c(float *out)
{
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) return false;
    int milli;
    int n = fscanf(f, "%d", &milli);
    fclose(f);
    if (n != 1) return false;
    *out = milli / 1000.0f;
    return true;
}

bool sysinfo_ip(char *buf, size_t size)
{
    struct ifaddrs *list;
    if (getifaddrs(&list) < 0) return false;

    bool found = false;
    for (struct ifaddrs *a = list; a && !found; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if ((a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP)) continue;
        struct in_addr ip = ((struct sockaddr_in *)a->ifa_addr)->sin_addr;
        if ((ntohl(ip.s_addr) & 0xFFFF0000u) == 0xA9FE0000u) continue;   /* 169.254.0.0/16 */
        found = inet_ntop(AF_INET, &ip, buf, (socklen_t)size) != NULL;
    }
    freeifaddrs(list);
    return found;
}
