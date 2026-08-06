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

            printf("%s@%s\n", pw->pw_name, hostname);

            size_t len = strlen(pw->pw_name) + strlen(hostname) + 1;

            for (size_t i = 0; i < len; i++)
            {
                putchar('-');
            }

            putchar('\n');

            printf("OS      : %s (%s)\n", os_name, sys.machine);
            printf("Kernel  : %s\n", sys.release);
            printf("Uptime  : %ldd %02ldh %02ldm\n", days, hours, minutes);
            printf("Memory  : %lu MB / %lu MB\n", (total_ram - free_ram) / 1024 / 1024, total_ram / 1024 / 1024);
            printf("Processes: %d\n", s_info.procs);
        }

    return 0;
}