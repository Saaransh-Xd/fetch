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

        if (driver[0] != '\0')
            printf("GPU%d    : %s (%s)\n", gpu_idx, vendor_name, driver);
        else
            printf("GPU%d    : %s\n", gpu_idx, vendor_name);

        gpu_idx++;
    }
    closedir(dir);
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

            printf("%s@%s\n", pw->pw_name, hostname);

            size_t len = strlen(pw->pw_name) + strlen(hostname) + 1;

            for (size_t i = 0; i < len; i++)
            {
                putchar('-');
            }

            putchar('\n');

            printf("OS      : %s\n", os_name);
            printf("Kernel  : %s\n", sys.release);
            printf("Uptime  : %ldd %02ldh %02ldm\n", days, hours, minutes);
            if (cpu_mhz > 0)
                printf("CPU     : %s (%d) @ %.2f GHz\n", cpu_model, cpu_cores, cpu_mhz / 1000.0);
            else
                printf("CPU     : %s (%d)\n", cpu_model, cpu_cores);
            print_gpus();
            printf("Memory  : %lu MB / %lu MB\n", (total_ram - free_ram) / 1024 / 1024, total_ram / 1024 / 1024);
            printf("Processes: %d\n", s_info.procs);
            printf("Arch    : %s\n", sys.machine);
        }

    return 0;
}