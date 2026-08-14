#ifndef SFETCH_LARP_EMBEDDED_H
#define SFETCH_LARP_EMBEDDED_H

typedef struct {
    const char *user;
    const char *hostname;
    const char *os;
    const char *os_id;
    const char *kernel;
    const char *arch;
    const char *shell;
    const char *cpu_model;
    unsigned long uptime;
    unsigned long total_ram;
    unsigned long free_ram;
    int process_count;
    int cpu_cores;
    double cpu_mhz;
    double cpu_temperature;
} SfetchLarpInfo;

int sfetch_run_larp(int argc, char **argv, const SfetchLarpInfo *info);

#endif
