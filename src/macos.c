#include "platform.h"

#if defined(__APPLE__)
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/statvfs.h>
#include <mach/mach.h>
#include <unistd.h>

static int read_sysctl_u64(const char *name, unsigned long long *value) {
    size_t size = sizeof(*value);
    return sysctlbyname(name, value, &size, NULL, 0) == 0 && size == sizeof(*value);
}

static int read_sysctl_int(const char *name, int *value) {
    size_t size = sizeof(*value);
    return sysctlbyname(name, value, &size, NULL, 0) == 0 && size == sizeof(*value);
}

static int read_sysctl_string(const char *name, char *buffer, size_t size) {
    size_t length = size;
    return sysctlbyname(name, buffer, &length, NULL, 0) == 0 && length > 0;
}

static unsigned long count_command_lines(const char *command) {
    FILE *file = popen(command, "r");
    char line[256];
    unsigned long count = 0;

    if (!file) return 0;
    while (fgets(line, sizeof(line), file)) ++count;
    (void)pclose(file);
    return count;
}

void sfetch_macos_get_pretty_name(char *buffer, size_t size) {
    char version[128] = "";
    size_t version_size = sizeof(version);

    if (sysctlbyname("kern.osproductversion", version, &version_size, NULL, 0) == 0)
        snprintf(buffer, size, "macOS %s", version);
    else
        snprintf(buffer, size, "macOS");
}

void sfetch_macos_get_os_id(char *buffer, size_t size) {
    snprintf(buffer, size, "macos");
}

void sfetch_macos_get_cpu_model(char *buffer, size_t size) {
    if (!read_sysctl_string("machdep.cpu.brand_string", buffer, size))
        snprintf(buffer, size, "Apple Silicon");
}

int sfetch_macos_get_cpu_cores(void) {
    int cores = 0;
    return read_sysctl_int("hw.ncpu", &cores) ? cores : 0;
}

double sfetch_macos_get_cpu_mhz(void) {
    unsigned long long frequency = 0;
    if (!read_sysctl_u64("hw.cpufrequency", &frequency)) return 0.0;
    return (double)frequency / 1000000.0;
}

void sfetch_macos_get_memory(SfetchPlatformMemory *memory) {
    unsigned long long total = 0;
    struct timeval boot_time;
    size_t boot_size = sizeof(boot_time);
    time_t now = time(NULL);
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vm_stats;
    host_basic_info_data_t host_info_data;
    mach_msg_type_number_t host_count = HOST_BASIC_INFO_COUNT;
    vm_size_t page_size = 0;

    memset(memory, 0, sizeof(*memory));
    (void)read_sysctl_u64("hw.memsize", &total);
    memory->total_ram = (unsigned long)total;

    if (sysctlbyname("kern.boottime", &boot_time, &boot_size, NULL, 0) == 0 &&
        now >= boot_time.tv_sec)
        memory->uptime = (unsigned long)(now - boot_time.tv_sec);

    if (host_page_size(mach_host_self(), &page_size) == KERN_SUCCESS &&
        host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
        uint64_t available_pages = (uint64_t)vm_stats.free_count +
                                   (uint64_t)vm_stats.inactive_count +
                                   (uint64_t)vm_stats.speculative_count;
        memory->free_ram = (unsigned long)(available_pages * page_size);
    }

    if (host_info(mach_host_self(), HOST_BASIC_INFO, (host_info_t)&host_info_data,
                  &host_count) == KERN_SUCCESS && memory->total_ram == 0)
        memory->total_ram = (unsigned long)host_info_data.max_mem;

    memory->process_count = (int)count_command_lines("ps -ax -o pid= 2>/dev/null");
}

void sfetch_macos_print_display(const char *cyan, const char *reset) {
    FILE *file = popen("system_profiler SPDisplaysDataType 2>/dev/null", "r");
    char line[512];
    int width = 0, height = 0;
    double refresh = 0.0;
    int display_idx = 0;

    #define PRINT_MAC_DISPLAY() do { \
        if (width > 0 && height > 0) { \
            if (refresh > 0.0) \
                printf("%sDisplay%d%s : %dx%d @ %.0f Hz\\n", cyan, display_idx++, reset, width, height, refresh); \
            else \
                printf("%sDisplay%d%s : %dx%d @ N/A\\n", cyan, display_idx++, reset, width, height); \
        } \
    } while (0)

    if (file) {
        while (fgets(line, sizeof(line), file)) {
            char *value = strstr(line, "Resolution:");
            if (value) {
                PRINT_MAC_DISPLAY();
                width = height = 0;
                refresh = 0.0;
                value += strlen("Resolution:");
                while (*value == ' ' || *value == '\t') ++value;
                if (sscanf(value, "%d x %d", &width, &height) != 2)
                    (void)sscanf(value, "%dx%d", &width, &height);
            }
            value = strstr(line, "Refresh Rate:");
            if (value) {
                value += strlen("Refresh Rate:");
                refresh = strtod(value, NULL);
            }
        }
        (void)pclose(file);
    }
    PRINT_MAC_DISPLAY();
    if (display_idx == 0) printf("%sDisplay%s  : Unknown\n", cyan, reset);
    #undef PRINT_MAC_DISPLAY
}

int sfetch_macos_battery_exists(void) {
    FILE *file = popen("pmset -g batt 2>/dev/null", "r");
    char line[256];
    int found = 0;
    if (!file) return 0;
    while (fgets(line, sizeof(line), file))
        if (strstr(line, "InternalBattery")) found = 1;
    (void)pclose(file);
    return found;
}

int sfetch_macos_print_battery(const char *cyan, const char *reset) {
    FILE *file = popen("pmset -g batt 2>/dev/null", "r");
    char line[256];
    int found = 0;
    if (!file) return 0;
    while (fgets(line, sizeof(line), file)) {
        char *percent = strchr(line, '%');
        if (!strstr(line, "InternalBattery") || !percent) continue;
        char *start = percent;
        while (start > line && start[-1] >= '0' && start[-1] <= '9') --start;
        printf("%sBattery%s : %.*s%%\n", cyan, reset, (int)(percent - start + 1), start);
        found = 1;
    }
    (void)pclose(file);
    return found;
}

void sfetch_macos_print_chassis(const char *cyan, const char *reset, int has_battery) {
    printf("%sChassis%s : %s\n", cyan, reset, has_battery ? "Laptop" : "Desktop");
}

void sfetch_macos_print_gpus(void) {
    FILE *file = popen("system_profiler SPDisplaysDataType 2>/dev/null", "r");
    char line[512];
    int gpu_idx = 0;
    if (!file) return;
    while (fgets(line, sizeof(line), file)) {
        char *value = strstr(line, "Chipset Model:");
        if (value) {
            value += strlen("Chipset Model:");
            while (*value == ' ' || *value == '\t') ++value;
            printf("GPU%d      : %s", gpu_idx++, value);
        }
    }
    (void)pclose(file);
}

void sfetch_macos_print_disks(const char *cyan, const char *reset) {
    struct statvfs stats;
    if (statvfs("/", &stats) != 0 || stats.f_blocks == 0) return;
    unsigned long long block_size = stats.f_frsize ? stats.f_frsize : stats.f_bsize;
    unsigned long long total = (unsigned long long)stats.f_blocks * block_size;
    unsigned long long used = total - (unsigned long long)stats.f_bfree * block_size;
    printf("%sDisk (/)%s: %.2f GiB / %.2f GiB\n", cyan, reset,
           (double)used / (1024.0 * 1024.0 * 1024.0),
           (double)total / (1024.0 * 1024.0 * 1024.0));
}

void sfetch_macos_print_swap(const char *cyan, const char *reset) {
    FILE *file = popen("sysctl -n vm.swapusage 2>/dev/null", "r");
    char line[256];
    if (file && fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';
        printf("%sSwap%s    : %s\n", cyan, reset, line);
    }
    if (file) (void)pclose(file);
}

void sfetch_macos_print_packages(const char *cyan, const char *reset) {
    if (access("/opt/homebrew/bin/brew", X_OK) == 0 ||
        access("/usr/local/bin/brew", X_OK) == 0) {
        unsigned long formulae = count_command_lines("brew list --formula 2>/dev/null");
        unsigned long casks = count_command_lines("brew list --cask 2>/dev/null");
        printf("%sPackages%s: %lu (Homebrew)\n", cyan, reset, formulae + casks);
    } else {
        printf("%sPackages%s: Unknown\n", cyan, reset);
    }
}

void sfetch_macos_get_display(SfetchDisplay *display) {
    FILE *file = popen("system_profiler SPDisplaysDataType 2>/dev/null", "r");
    char line[512];

    display->width = 0;
    display->height = 0;
    display->refresh_hz = 0.0;
    if (file) {
        while (fgets(line, sizeof(line), file)) {
            char *value = strstr(line, "Resolution:");
            if (value) {
                value += strlen("Resolution:");
                while (*value == ' ' || *value == '\t') ++value;
                if (sscanf(value, "%d x %d", &display->width, &display->height) != 2)
                    (void)sscanf(value, "%dx%d", &display->width, &display->height);
            }
            value = strstr(line, "Refresh Rate:");
            if (value) {
                value += strlen("Refresh Rate:");
                display->refresh_hz = strtod(value, NULL);
            }
            if (display->width > 0 && display->height > 0 && display->refresh_hz > 0.0) break;
        }
        (void)pclose(file);
    }
}

void sfetch_macos_get_disks(SfetchDisk *disks, int *count, int max_disks) {
    struct statvfs stats;
    *count = 0;
    if (max_disks < 1) return;
    if (statvfs("/", &stats) != 0 || stats.f_blocks == 0) return;

    unsigned long long block_size = stats.f_frsize ? stats.f_frsize : stats.f_bsize;
    unsigned long long total = (unsigned long long)stats.f_blocks * block_size;
    unsigned long long used = total - (unsigned long long)stats.f_bfree * block_size;

    snprintf(disks[0].mountpoint, sizeof(disks[0].mountpoint), "/");
    snprintf(disks[0].filesystem, sizeof(disks[0].filesystem), "apfs");
    disks[0].total_gib = (double)total / (1024.0 * 1024.0 * 1024.0);
    disks[0].used_gib = (double)used / (1024.0 * 1024.0 * 1024.0);
    disks[0].percent = total ? (unsigned long)((double)used * 100.0 / total + 0.5) : 0;
    *count = 1;
}

void sfetch_macos_get_swap(SfetchSwap *swap) {
    FILE *file = popen("sysctl -n vm.swapusage 2>/dev/null", "r");
    char line[256];

    swap->used_kb = 0;
    swap->total_kb = 0;
    if (file && fgets(line, sizeof(line), file)) {
        double total_m = 0.0, used_m = 0.0;
        if (sscanf(line, "total = %lfM", &total_m) == 1) {
            char *used_pos = strstr(line, "used =");
            if (used_pos) {
                (void)sscanf(used_pos + 6, "%lf", &used_m);
                swap->total_kb = (unsigned long long)(total_m * 1024.0);
                swap->used_kb = (unsigned long long)(used_m * 1024.0);
            }
        }
    }
    if (file) (void)pclose(file);
}

void sfetch_macos_get_packages(SfetchPackages *packages) {
    packages->count = 0;
    packages->available = 0;
    packages->manager[0] = '\0';
    if (access("/opt/homebrew/bin/brew", X_OK) == 0 ||
        access("/usr/local/bin/brew", X_OK) == 0) {
        unsigned long formulae = count_command_lines("brew list --formula 2>/dev/null");
        unsigned long casks = count_command_lines("brew list --cask 2>/dev/null");
        packages->count = formulae + casks;
        packages->available = 1;
        snprintf(packages->manager, sizeof(packages->manager), "Homebrew");
    }
}

int sfetch_macos_get_battery(SfetchBattery *batteries, int max) {
    FILE *file = popen("pmset -g batt 2>/dev/null", "r");
    char line[256];
    int count = 0;

    if (!file) return 0;
    while (fgets(line, sizeof(line), file) && count < max) {
        char *percent = strchr(line, '%');
        if (!strstr(line, "InternalBattery") || !percent) continue;
        char *start = percent;
        while (start > line && start[-1] >= '0' && start[-1] <= '9') --start;
        batteries[count].capacity = atoi(start);
        snprintf(batteries[count].name, sizeof(batteries[count].name), "InternalBattery");
        snprintf(batteries[count].status, sizeof(batteries[count].status), "Unknown");
        count++;
    }
    (void)pclose(file);
    return count;
}

void sfetch_macos_get_chassis(SfetchChassis *chassis, int has_battery) {
    snprintf(chassis->type, sizeof(chassis->type), "%s", has_battery ? "Laptop" : "Desktop");
}
#endif
