#ifndef SFETCH_LARP_EMBEDDED_H
#define SFETCH_LARP_EMBEDDED_H

#include "../src/platform.h"

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
    SfetchDisk disks[16];
    int disk_count;
    SfetchSwap swap;
    SfetchPackages packages;
    SfetchDisplay display;
    char gpus[8][512];
    int gpu_count;
    SfetchBattery batteries[8];
    int battery_count;
    SfetchChassis chassis;
    char terminal[128];
    char local_ip[512];
    double larp_fps;
    int larp_infinite;
    int larp_frames;
} SfetchLarpInfo;

int sfetch_run_larp(int argc, char **argv, const SfetchLarpInfo *info);

#endif
