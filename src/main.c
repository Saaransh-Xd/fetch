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
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>

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

static int read_temperature_file(const char *path, double *temperature) {
    FILE *file = fopen(path, "r");
    long millidegrees;
    int result;

    if (!file) return 0;
    result = fscanf(file, "%ld", &millidegrees);
    fclose(file);
    if (result != 1) return 0;

    *temperature = (double)millidegrees / 1000.0;
    return 1;
}

static double get_cpu_temperature(void) {
    DIR *directory = opendir("/sys/class/thermal");
    struct dirent *entry;
    double fallback = 0.0;
    double temperature;

    if (!directory) return 0.0;
    while ((entry = readdir(directory)) != NULL) {
        char type_path[PATH_MAX];
        char temperature_path[PATH_MAX];
        char type[64] = "";
        FILE *type_file;

        if (strncmp(entry->d_name, "thermal_zone", 12) != 0) continue;
        snprintf(type_path, sizeof(type_path), "/sys/class/thermal/%s/type", entry->d_name);
        snprintf(temperature_path, sizeof(temperature_path),
                 "/sys/class/thermal/%s/temp", entry->d_name);
        type_file = fopen(type_path, "r");
        if (type_file) {
            (void)fgets(type, sizeof(type), type_file);
            fclose(type_file);
            type[strcspn(type, "\r\n")] = '\0';
        }
        if (!read_temperature_file(temperature_path, &temperature)) continue;
        if (fallback == 0.0) fallback = temperature;
        if (strstr(type, "cpu") || strstr(type, "x86_pkg") ||
            strstr(type, "coretemp") || strstr(type, "k10temp")) {
            closedir(directory);
            return temperature;
        }
    }
    closedir(directory);
    return fallback;
}

static double get_gpu_temperature(const char *card_name) {
    char hwmon_path[PATH_MAX];
    DIR *directory;
    struct dirent *entry;
    double temperature;

    snprintf(hwmon_path, sizeof(hwmon_path), "/sys/class/drm/%s/device/hwmon", card_name);
    directory = opendir(hwmon_path);
    if (!directory) return 0.0;
    while ((entry = readdir(directory)) != NULL) {
        char temperature_path[PATH_MAX];
        if (strncmp(entry->d_name, "hwmon", 5) != 0) continue;
        if (snprintf(temperature_path, sizeof(temperature_path), "%s/%s/temp1_input",
                     hwmon_path, entry->d_name) >= (int)sizeof(temperature_path))
            continue;
        if (read_temperature_file(temperature_path, &temperature)) {
            closedir(directory);
            return temperature;
        }
    }
    closedir(directory);
    return 0.0;
}

static void print_local_ip(const char *cyan, const char *reset) {
    struct ifaddrs *interfaces;
    struct ifaddrs *interface;
    char address[INET_ADDRSTRLEN];
    int printed = 0;

    printf("%sLocal IP%s: ", cyan, reset);
    if (getifaddrs(&interfaces) != 0) {
        printf("Unknown\n");
        return;
    }

    for (interface = interfaces; interface != NULL; interface = interface->ifa_next) {
        struct sockaddr_in *ipv4;
        if (!interface->ifa_addr || interface->ifa_addr->sa_family != AF_INET ||
            (interface->ifa_flags & IFF_LOOPBACK) != 0)
            continue;

        ipv4 = (struct sockaddr_in *)interface->ifa_addr;
        if (!inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address))) continue;
        printf("%s%s", printed ? ", " : "", address);
        printed = 1;
    }
    freeifaddrs(interfaces);
    if (printed) putchar('\n');
    else printf("Unknown\n");
}

static void print_display_info(const char *cyan, const char *reset) {
    FILE *display_command = popen("xrandr --current 2>/dev/null", "r");
    char line[512];
    int width = 0, height = 0;
    double refresh = 0.0;

    if (display_command) {
        while (fgets(line, sizeof(line), display_command)) {
            char *connected = strstr(line, " connected");
            if (!connected) continue;
            for (char *position = connected; *position != '\0'; ++position) {
                int candidate_width, candidate_height;
                if (sscanf(position, "%dx%d", &candidate_width, &candidate_height) == 2) {
                    width = candidate_width;
                    height = candidate_height;
                    char *rate_start = strchr(position, ' ');
                    if (rate_start) refresh = strtod(rate_start, NULL);
                    break;
                }
            }
            if (width > 0) break;
        }
        (void)pclose(display_command);
    }

    if (width > 0 && height > 0 && refresh > 0.0)
        printf("%sDisplay%s  : %dx%d @ %.0f Hz\n", cyan, reset, width, height, refresh);
    else
        printf("%sDisplay%s  : Unknown\n", cyan, reset);
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
        double temperature = get_gpu_temperature(entry->d_name);
        if (driver[0] != '\0' && temperature > 0.0)
            printf("%sGPU%d%s    : %s (%s) | %.1f C\n", label_color, gpu_idx,
                   reset, vendor_name, driver, temperature);
        else if (driver[0] != '\0')
            printf("%sGPU%d%s    : %s (%s) | %s\n", label_color, gpu_idx, reset,
                   vendor_name, driver, "temperature unavailable");
        else if (temperature > 0.0)
            printf("%sGPU%d%s    : %s | %.1f C\n", label_color, gpu_idx, reset,
                   vendor_name, temperature);
        else
            printf("%sGPU%d%s    : %s | temperature unavailable\n", label_color,
                   gpu_idx, reset, vendor_name);

        gpu_idx++;
    }
    closedir(dir);
}

static void unescape_mount_field(char *value) {
    char *source = value;
    char *destination = value;

    while (*source != '\0') {
        if (strncmp(source, "\\040", 4) == 0) {
            *destination++ = ' ';
            source += 4;
        } else if (strncmp(source, "\\011", 4) == 0) {
            *destination++ = '\t';
            source += 4;
        } else if (strncmp(source, "\\134", 4) == 0) {
            *destination++ = '\\';
            source += 4;
        } else {
            *destination++ = *source++;
        }
    }
    *destination = '\0';
}

static int is_pseudo_filesystem(const char *filesystem) {
    static const char *const pseudo_filesystems[] = {
        "autofs", "cgroup", "cgroup2", "debugfs", "devpts", "devtmpfs",
        "fusectl", "hugetlbfs", "mqueue", "proc", "pstore", "rootfs",
        "securityfs", "squashfs", "sysfs", "tmpfs", "tracefs"
    };

    for (size_t i = 0; i < sizeof(pseudo_filesystems) / sizeof(pseudo_filesystems[0]); ++i)
        if (strcmp(filesystem, pseudo_filesystems[i]) == 0) return 1;
    return 0;
}

void print_disks(void) {
    FILE *mounts = fopen("/proc/mounts", "r");
    if (!mounts) return;

    char device[PATH_MAX], mountpoint[PATH_MAX], filesystem[64];
    const char *cyan = colors_enabled() ? ANSI_CYAN : "";
    const char *reset = colors_enabled() ? ANSI_RESET : "";
    char root_device[PATH_MAX] = "";

    /* Find the root device first so bind mounts of it are not reported as disks. */
    while (fscanf(mounts, "%511s %511s %63s %*s %*d %*d\n",
                  device, mountpoint, filesystem) == 3) {
        if (strcmp(mountpoint, "/") == 0) {
            snprintf(root_device, sizeof(root_device), "%s", device);
            break;
        }
    }
    rewind(mounts);

    while (fscanf(mounts, "%511s %511s %63s %*s %*d %*d\n",
                  device, mountpoint, filesystem) == 3) {
        if (is_pseudo_filesystem(filesystem)) continue;
        if (strcmp(mountpoint, "/") != 0 && strcmp(device, root_device) == 0)
            continue;
        if (strcmp(mountpoint, "/") != 0 &&
            strncmp(device, "/dev/", 5) != 0 &&
            !(strcmp(filesystem, "9p") == 0 && strncmp(mountpoint, "/mnt/", 5) == 0))
            continue;

        unescape_mount_field(mountpoint);
        struct statvfs stats;
        if (statvfs(mountpoint, &stats) != 0 || stats.f_blocks == 0) continue;

        unsigned long long block_size = stats.f_frsize ? stats.f_frsize : stats.f_bsize;
        unsigned long long total_bytes = (unsigned long long)stats.f_blocks * block_size;
        unsigned long long used_bytes = total_bytes -
                                        (unsigned long long)stats.f_bfree * block_size;
        double used_gib = (double)used_bytes / (1024.0 * 1024.0 * 1024.0);
        double total_gib = (double)total_bytes / (1024.0 * 1024.0 * 1024.0);
        unsigned long percentage = total_bytes
            ? (unsigned long)((double)used_bytes * 100.0 / total_bytes + 0.5)
            : 0;
        const char *usage_color = percentage >= 90 ? ANSI_RED :
                                  percentage >= 75 ? ANSI_YELLOW : ANSI_GREEN;
        if (!colors_enabled()) usage_color = "";

        printf("%sDisk (%s)%s: %.2f GiB / %.2f GiB (%s%lu%%%s) - %s\n",
               cyan, mountpoint, reset, used_gib, total_gib,
               usage_color, percentage, reset, filesystem);
    }
    fclose(mounts);
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
    unsigned long percentage = total_kb
        ? (unsigned long)((double)used_kb * 100.0 / total_kb + 0.5)
        : 0;
    const char *usage_color = percentage >= 90 ? ANSI_RED :
                              percentage >= 75 ? ANSI_YELLOW : ANSI_GREEN;
    if (!colors_enabled()) usage_color = "";
    printf("%sSwap%s    : %llu MB / %llu MB (%s%lu%%%s)\n",
           label_color, reset, used_kb / 1024, total_kb / 1024,
           usage_color, percentage, reset);
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
            double cpu_temperature = get_cpu_temperature();

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
                printf("%sCPU%s     : %s (%d) @ %.2f GHz", cyan, reset, cpu_model, cpu_cores, cpu_mhz / 1000.0);
            else
                printf("%sCPU%s     : %s (%d)", cyan, reset, cpu_model, cpu_cores);
            if (cpu_temperature > 0.0) printf(" | %.1f C", cpu_temperature);
            else printf(" | temperature unavailable");
            putchar('\n');
            print_gpus();
            unsigned long ram_used = total_ram - free_ram;
            unsigned long ram_percentage = total_ram
                ? (unsigned long)((double)ram_used * 100.0 / total_ram + 0.5)
                : 0;
            const char *ram_color = ram_percentage >= 90 ? ANSI_RED :
                                    ram_percentage >= 75 ? ANSI_YELLOW : ANSI_GREEN;
            if (!colors_enabled()) ram_color = "";
            printf("%sMemory%s  : %lu MB / %lu MB (%s%lu%%%s)\n", cyan, reset,
                   ram_used / 1024 / 1024, total_ram / 1024 / 1024,
                   ram_color, ram_percentage, reset);
            print_disks();
            print_swap();
            print_packages();
            print_terminal();
            print_local_ip(cyan, reset);
            print_display_info(cyan, reset);
            printf("%sProcesses%s: %d\n", cyan, reset, s_info.procs);
            printf("%sArch%s    : %s\n", cyan, reset, sys.machine);
            printf("%sShell%s   : %s\n", cyan, reset, shell_name);
            printColorPalette();
        }

    return 0;
}
