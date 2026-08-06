#define _GNU_SOURCE

#include <stdio.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <pwd.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <dirent.h>
#include <sys/statvfs.h>
#include <ctype.h>

#include "ansi.h"

static int colors_enabled(void) {
    return isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL;
}

void get_pretty_name(char *buffer, size_t size) {
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) {
        f = fopen("/usr/lib/os-release", "r");
        if (!f) {
            snprintf(buffer, size, "Unknown Linux");
            return;
        }
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
            char *start = line + 12;
            // Remove trailing newline or carriage return
            line[strcspn(line, "\r\n")] = 0;
            
            // Remove surrounding quotes if they exist
            if (*start == '"' || *start == '\'') {
                start++;
                size_t len = strlen(start);
                if (len > 0 && (start[len - 1] == '"' || start[len - 1] == '\'')) {
                    start[len - 1] = 0;
                }
            }
            
            snprintf(buffer, size, "%s", start);
            fclose(f);
            return;
        }
    }
    
    fclose(f);
    snprintf(buffer, size, "Unknown Linux");
}

void get_cpu_model(char *buffer, size_t size) {
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) {
        snprintf(buffer, size, "Unknown CPU");
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "model name", 10) == 0) {
            char *colon = strchr(line, ':');
            if (colon) {
                char *start = colon + 2;
                line[strcspn(line, "\r\n")] = 0;
                snprintf(buffer, size, "%s", start);
            }
            fclose(f);
            return;
        }
    }

    fclose(f);
    snprintf(buffer, size, "Unknown CPU");
}

int get_cpu_cores(void) {
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return 0;

    int count = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "processor", 9) == 0) count++;
    }
    fclose(f);
    return count;
}

double get_cpu_mhz(void) {
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return 0.0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu MHz", 7) == 0) {
            char *colon = strchr(line, ':');
            if (colon) {
                double mhz = strtod(colon + 1, NULL);
                fclose(f);
                return mhz;
            }
        }
    }
    fclose(f);
    return 0.0;
}

void print_gpus(void) {
    DIR *dir = opendir("/sys/class/drm");
    if (!dir) return;

    struct dirent *entry;
    int gpu_idx = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "card", 4) != 0) continue;

        char vendor_path[512];
        char uevent_path[512];
        snprintf(vendor_path, sizeof(vendor_path), "/sys/class/drm/%s/device/vendor", entry->d_name);
        snprintf(uevent_path, sizeof(uevent_path), "/sys/class/drm/%s/device/uevent", entry->d_name);

        if (access(vendor_path, R_OK) != 0) continue;

        char vendor[16] = "";
        FILE *vf = fopen(vendor_path, "r");
        if (vf) {
            fgets(vendor, sizeof(vendor), vf);
            fclose(vf);
        }

        char driver[256] = "";
        FILE *uf = fopen(uevent_path, "r");
        if (uf) {
            char line[256];
            while (fgets(line, sizeof(line), uf)) {
                if (strncmp(line, "DRIVER=", 7) == 0) {
                    line[strcspn(line, "\r\n")] = 0;
                    snprintf(driver, sizeof(driver), "%s", line + 7);
                    break;
                }
            }
            fclose(uf);
        }

        const char *vendor_name = "Unknown";
        if (strstr(vendor, "0x1002")) vendor_name = "AMD";
        else if (strstr(vendor, "0x10de")) vendor_name = "NVIDIA";
        else if (strstr(vendor, "0x8086")) vendor_name = "Intel";

        const char *label_color = colors_enabled() ? ANSI_CYAN : "";
        const char *reset = colors_enabled() ? ANSI_RESET : "";
        if (driver[0] != '\0')
            printf("%sGPU%d%s    : %s (%s)\n", label_color, gpu_idx, reset, vendor_name, driver);
        else
            printf("%sGPU%d%s    : %s\n", label_color, gpu_idx, reset, vendor_name);

        gpu_idx++;
    }
    closedir(dir);
}

static void trim_newline(char *value) {
    value[strcspn(value, "\r\n")] = '\0';
}

static void trim_spaces(char *value) {
    char *start = value;
    while (isspace((unsigned char)*start)) start++;
    if (start != value) memmove(value, start, strlen(start) + 1);

    size_t length = strlen(value);
    while (length > 0 && isspace((unsigned char)value[length - 1]))
        value[--length] = '\0';
}

static int is_disk_device(const char *name) {
    /* Keep physical disks, while ignoring partitions and virtual devices. */
    if (strncmp(name, "loop", 4) == 0 || strncmp(name, "ram", 3) == 0 ||
        strncmp(name, "zram", 4) == 0 || strncmp(name, "dm-", 3) == 0 ||
        strncmp(name, "md", 2) == 0 || strncmp(name, "sr", 2) == 0)
        return 0;

    size_t length = strlen(name);
    if (length == 0) return 0;
    if (isdigit((unsigned char)name[length - 1])) {
        /* nvme0n1 is a disk; nvme0n1p1 is a partition. */
        if ((length >= 2 && name[length - 2] == 'n') ||
            strncmp(name, "mmcblk", 6) == 0) return 1;
        return 0;
    }
    return strncmp(name, "sd", 2) == 0 || strncmp(name, "hd", 2) == 0 ||
           strncmp(name, "vd", 2) == 0 || strncmp(name, "xvd", 3) == 0 ||
           strncmp(name, "mmcblk", 6) == 0;
}

static int get_disk_usage(const char *disk_name,
                          unsigned long long *used_bytes,
                          unsigned long long *total_bytes) {
    FILE *mounts = fopen("/proc/mounts", "r");
    if (!mounts) return 0;

    char device[PATH_MAX], mountpoint[PATH_MAX], filesystem[64];
    int found = 0;
    while (fscanf(mounts, "%511s %511s %63s %*s %*d %*d\n",
                  device, mountpoint, filesystem) == 3) {
        char expected[PATH_MAX];
        snprintf(expected, sizeof(expected), "/dev/%s", disk_name);
        size_t expected_length = strlen(expected);
        if (strncmp(device, expected, expected_length) != 0) continue;

        char next = device[expected_length];
        if (next != '\0' && !isdigit((unsigned char)next) && next != 'p') continue;

        struct statvfs stats;
        if (statvfs(mountpoint, &stats) != 0 || stats.f_blocks == 0) continue;
        unsigned long long block_size = stats.f_frsize ? stats.f_frsize : stats.f_bsize;
        *total_bytes = (unsigned long long)stats.f_blocks * block_size;
        *used_bytes = (unsigned long long)(stats.f_blocks - stats.f_bfree) * block_size;
        found = 1;
        break;
    }
    fclose(mounts);
    return found;
}

void print_disks(void) {
    DIR *dir = opendir("/sys/block");
    if (!dir) return;

    struct dirent *entry;
    int disk_index = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (!is_disk_device(entry->d_name)) continue;

        char size_path[PATH_MAX];
        snprintf(size_path, sizeof(size_path), "/sys/block/%s/size", entry->d_name);
        FILE *size_file = fopen(size_path, "r");
        unsigned long long sectors = 0;
        if (size_file) {
            (void)fscanf(size_file, "%llu", &sectors);
            fclose(size_file);
        }

        char model_path[PATH_MAX];
        snprintf(model_path, sizeof(model_path), "/sys/block/%s/device/model", entry->d_name);
        char model[128] = "";
        FILE *model_file = fopen(model_path, "r");
        if (model_file) {
            if (fgets(model, sizeof(model), model_file)) trim_newline(model);
            fclose(model_file);
        }
        trim_spaces(model);

        double size_gb = (double)sectors * 512.0 / 1000000000.0;
        unsigned long long used_bytes = 0, total_bytes = 0;
        int has_usage = get_disk_usage(entry->d_name, &used_bytes, &total_bytes);
        const char *cyan = colors_enabled() ? ANSI_CYAN : "";
        const char *reset = colors_enabled() ? ANSI_RESET : "";
        const char *model_color = colors_enabled() ? ANSI_DIM : "";
        if (has_usage) {
            double used_gb = (double)used_bytes / 1000000000.0;
            double total_gb = (double)total_bytes / 1000000000.0;
            double percentage = total_bytes ? (double)used_bytes * 100.0 / total_bytes : 0.0;
            const char *usage_color = percentage >= 90.0 ? ANSI_RED :
                                      percentage >= 75.0 ? ANSI_YELLOW : ANSI_GREEN;
            if (!colors_enabled()) usage_color = "";
            printf("%sDisk%-2d%s  : %s | %.1f / %.1f GB (%s%.1f%%%s)",
                   cyan, disk_index, reset, entry->d_name, used_gb, total_gb,
                   usage_color, percentage, reset);
        } else {
            const char *unknown_color = colors_enabled() ? ANSI_YELLOW : "";
            printf("%sDisk%-2d%s  : %s | N/A / %.1f GB (%sN/A%s)",
                   cyan, disk_index, reset, entry->d_name, size_gb,
                   unknown_color, reset);
        }
        if (model[0] != '\0') printf(" | %s%s%s", model_color, model, reset);
        putchar('\n');
        disk_index++;
    }
    closedir(dir);
}

void print_swap(void) {
    FILE *f = fopen("/proc/swaps", "r");
    if (!f) return;

    char line[512];
    unsigned long long total_kb = 0, used_kb = 0;
    (void)fgets(line, sizeof(line), f); /* header */
    while (fgets(line, sizeof(line), f)) {
        char filename[256], type[32];
        unsigned long long size_kb = 0, swap_used_kb = 0;
        if (sscanf(line, "%255s %31s %llu %llu", filename, type,
                   &size_kb, &swap_used_kb) == 4) {
            total_kb += size_kb;
            used_kb += swap_used_kb;
        }
    }
    fclose(f);

    const char *label_color = colors_enabled() ? ANSI_CYAN : "";
    const char *reset = colors_enabled() ? ANSI_RESET : "";
    printf("%sSwap%s    : %llu MB / %llu MB\n", label_color, reset,
           used_kb / 1024, total_kb / 1024);
}

static unsigned long count_command_lines(const char *command) {
    FILE *f = popen(command, "r");
    if (!f) return 0;
    unsigned long count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) count++;
    (void)pclose(f);
    return count;
}

static unsigned long count_dpkg_packages(void) {
    FILE *f = fopen("/var/lib/dpkg/status", "r");
    if (!f) return 0;

    unsigned long count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Status: install ok installed", 28) == 0)
            count++;
    }
    fclose(f);
    return count;
}

void print_packages(void) {
    unsigned long count = 0;
    const char *manager = NULL;

    if (access("/usr/bin/dpkg-query", X_OK) == 0 || access("/bin/dpkg-query", X_OK) == 0) {
        count = count_dpkg_packages();
        manager = "dpkg";
    } else if (access("/usr/bin/rpm", X_OK) == 0 || access("/bin/rpm", X_OK) == 0) {
        count = count_command_lines("rpm -qa 2>/dev/null");
        manager = "rpm";
    } else if (access("/usr/bin/pacman", X_OK) == 0 || access("/bin/pacman", X_OK) == 0) {
        count = count_command_lines("pacman -Qq 2>/dev/null");
        manager = "pacman";
    }

    const char *label_color = colors_enabled() ? ANSI_CYAN : "";
    const char *reset = colors_enabled() ? ANSI_RESET : "";
    if (manager)
        printf("%sPackages%s: %lu (%s)\n", label_color, reset, count, manager);
    else
        printf("%sPackages%s: Unknown\n", label_color, reset);
}

void print_terminal(void) {
    const char *terminal = getenv("TERM_PROGRAM");
    if (!terminal || terminal[0] == '\0') {
        if (getenv("WT_SESSION")) terminal = "Windows Terminal";
        else terminal = getenv("TERM");
    }
    const char *label_color = colors_enabled() ? ANSI_CYAN : "";
    const char *reset = colors_enabled() ? ANSI_RESET : "";
    printf("%sTerminal%s: %s\n", label_color, reset,
           terminal && terminal[0] ? terminal : "Unknown");
}

int main(void)
{
    struct utsname sys;
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    // HOST_NAME_MAX is usually 64 on Linux, defined in <limits.h>
    char hostname[512];
    char os_name[128];
    get_pretty_name(os_name, sizeof(os_name));

    struct sysinfo s_info;

    if (gethostname(hostname, sizeof(hostname)) != 0) {
        perror("gethostname allocation or runtime error");
        return 1;
    }

    if (uname(&sys) != 0)
    {
        perror("uname");
        return 1;
    }

    if (sysinfo(&s_info) != 0)
    {
        perror("sysinfo");
        return 1;
    }    
    if (pw == NULL)
    {
        perror("getpwuid");
        return 1;
    }
        if (uname(&sys) == 0 && sysinfo(&s_info) == 0)
        {
            long days = s_info.uptime / 86400;
            long hours = (s_info.uptime % 86400) / 3600;
            long minutes = (s_info.uptime % 3600) / 60;

            char *shell_path = getenv("SHELL");

            double loads[3] = {0.0, 0.0, 0.0};
            if (getloadavg(loads, 3) < 0) {
                loads[0] = loads[1] = loads[2] = 0.0;
            }

            unsigned long total_ram = s_info.totalram * s_info.mem_unit;
            unsigned long free_ram = s_info.freeram * s_info.mem_unit;

            char cpu_model[128];
            get_cpu_model(cpu_model, sizeof(cpu_model));
            int cpu_cores = get_cpu_cores();
            double cpu_mhz = get_cpu_mhz();

            const char *green = colors_enabled() ? ANSI_GREEN : "";
            const char *cyan = colors_enabled() ? ANSI_CYAN : "";
            const char *dim = colors_enabled() ? ANSI_DIM : "";
            const char *reset = colors_enabled() ? ANSI_RESET : "";

            printf("%s%s%s%s@%s%s\n", green, pw->pw_name, reset,
                   cyan, hostname, reset);

            size_t len = strlen(pw->pw_name) + strlen(hostname) + 1;

            for (size_t i = 0; i < len; i++)
            {
                printf("%s-%s", dim, reset);
            }

            putchar('\n');

        if (shell_path == NULL) {
            printf("SHELL environment variable is not set.\n");
        }

            const char *shell_name = "Unknown";
            if (shell_path) {
                shell_name = strrchr(shell_path, '/');
                shell_name = shell_name ? shell_name + 1 : shell_path;
            }

            printf("%sOS%s      : %s\n", cyan, reset, os_name);
            printf("%sKernel%s  : %s\n", cyan, reset, sys.release);
            printf("%sUptime%s  : %ldd %02ldh %02ldm\n", cyan, reset, days, hours, minutes);
            if (cpu_mhz > 0)
                printf("%sCPU%s     : %s (%d) @ %.2f GHz\n", cyan, reset, cpu_model, cpu_cores, cpu_mhz / 1000.0);
            else
                printf("%sCPU%s     : %s (%d)\n", cyan, reset, cpu_model, cpu_cores);
            print_gpus();
            printf("%sMemory%s  : %lu MB / %lu MB\n", cyan, reset, (total_ram - free_ram) / 1024 / 1024, total_ram / 1024 / 1024);
            print_disks();
            print_swap();
            print_packages();
            print_terminal();
            printf("%sProcesses%s: %d\n", cyan, reset, s_info.procs);
            printf("%sArch%s    : %s\n", cyan, reset, sys.machine);
            printf("%sShell%s   : %s\n", cyan, reset, shell_name);
            printColorPalette();
        }

    return 0;
}
