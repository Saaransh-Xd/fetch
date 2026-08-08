#ifndef SFETCH_PLATFORM_H
#define SFETCH_PLATFORM_H

#include <stddef.h>

typedef struct {
    unsigned long uptime;
    unsigned long total_ram;
    unsigned long free_ram;
    int process_count;
} SfetchPlatformMemory;

#if defined(__APPLE__)
void sfetch_macos_get_pretty_name(char *buffer, size_t size);
void sfetch_macos_get_os_id(char *buffer, size_t size);
void sfetch_macos_get_cpu_model(char *buffer, size_t size);
int sfetch_macos_get_cpu_cores(void);
double sfetch_macos_get_cpu_mhz(void);
void sfetch_macos_get_memory(SfetchPlatformMemory *memory);
void sfetch_macos_print_display(const char *cyan, const char *reset);
int sfetch_macos_battery_exists(void);
int sfetch_macos_print_battery(const char *cyan, const char *reset);
void sfetch_macos_print_chassis(const char *cyan, const char *reset, int has_battery);
void sfetch_macos_print_gpus(void);
void sfetch_macos_print_disks(const char *cyan, const char *reset);
void sfetch_macos_print_swap(const char *cyan, const char *reset);
void sfetch_macos_print_packages(const char *cyan, const char *reset);
#endif

#endif
