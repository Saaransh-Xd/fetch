#define _GNU_SOURCE

#include <stdio.h>
#include <sys/utsname.h>
#include "platform.h"
#if defined(__APPLE__)
#define SFETCH_MACOS 1
#define SFETCH_BSD 0
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#define SFETCH_MACOS 0
#define SFETCH_BSD 1
#include <sys/sysctl.h>
#include <sys/time.h>
#else
#define SFETCH_MACOS 0
#define SFETCH_BSD 0
#include <sys/sysinfo.h>
#endif
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <pwd.h>
#include <string.h>
#include <dirent.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <ctype.h>
#include <math.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#if defined(__linux__)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif
#endif
#if !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__OpenBSD__) && \
    !defined(__NetBSD__) && !defined(__DragonFly__)
#include <dlfcn.h>
#include <wchar.h>
#endif

#include "ansi.h"
#include "../larp/embedded.h"

typedef struct {
    int logo;
    int header;
    int os;
    int kernel;
    int uptime;
    int cpu;
    int gpu;
    int memory;
    int disks;
    int swap;
    int packages;
    int terminal;
    int local_ip;
    int display;
    int battery;
    int chassis;
    int processes;
    int arch;
    int shell;
    int palette;
    double larp_fps;
    int larp_infinite;
    int larp_frames;
} SfetchConfig;

static char *trim_whitespace(char *value) {
    char *end;
    while (isspace((unsigned char)*value)) ++value;
    end = value + strlen(value);
    while (end > value && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return value;
}

static int parse_toggle(const char *value) {
    return strcmp(value, "0") != 0 && strcmp(value, "false") != 0 &&
           strcmp(value, "no") != 0 && strcmp(value, "off") != 0;
}

#if SFETCH_BSD
#if defined(__OpenBSD__)
static int openbsd_sysctl_mib(const char *name, int mib[2]) {
    mib[0] = CTL_HW;
    if (strcmp(name, "hw.model") == 0) mib[1] = HW_MODEL;
    else if (strcmp(name, "hw.ncpu") == 0) mib[1] = HW_NCPU;
    else if (strcmp(name, "hw.cpuspeed") == 0) mib[1] = HW_CPUSPEED;
    else if (strcmp(name, "hw.pagesize") == 0) mib[1] = HW_PAGESIZE;
    else if (strcmp(name, "hw.physmem64") == 0 || strcmp(name, "hw.physmem") == 0)
        mib[1] = HW_PHYSMEM64;
    else if (strcmp(name, "hw.usermem") == 0) mib[1] = HW_USERMEM64;
    else return 0;
    return 1;
}

static int read_sysctl_u64(const char *name, uint64_t *value) {
    int mib[2];
    unsigned long long raw = 0;
    size_t size = sizeof(raw);
    if (!openbsd_sysctl_mib(name, mib) || sysctl(mib, 2, &raw, &size, NULL, 0) != 0)
        return 0;
    if (size != sizeof(uint32_t) && size != sizeof(uint64_t)) return 0;
    *value = (uint64_t)raw;
    return 1;
}

static int read_sysctl_string(const char *name, char *buffer, size_t size) {
    int mib[2];
    size_t length = size;
    if (!openbsd_sysctl_mib(name, mib) || sysctl(mib, 2, buffer, &length, NULL, 0) != 0)
        return 0;
    return length > 0;
}
#else
static int read_sysctl_u64(const char *name, uint64_t *value) {
    size_t size = sizeof(*value);
    return sysctlbyname(name, value, &size, NULL, 0) == 0 &&
           (size == sizeof(uint32_t) || size == sizeof(uint64_t));
}

static int read_sysctl_string(const char *name, char *buffer, size_t size) {
    size_t length = size;
    return sysctlbyname(name, buffer, &length, NULL, 0) == 0 && length > 0;
}
#endif

static unsigned long get_bsd_uptime(void) {
    struct timeval boot_time;
    size_t size = sizeof(boot_time);
    int mib[] = { CTL_KERN, KERN_BOOTTIME };
    time_t now = time(NULL);

    if (sysctl(mib, sizeof(mib) / sizeof(mib[0]), &boot_time, &size, NULL, 0) != 0 ||
        size != sizeof(boot_time) || now < boot_time.tv_sec)
        return 0;
    return (unsigned long)(now - boot_time.tv_sec);
}

static void get_bsd_memory(uint64_t *total, uint64_t *available) {
    uint64_t pages, page_size;
    uint64_t user_memory;
    long sysconf_pages;
    long sysconf_available;
    long sysconf_page_size;

    *total = 0;
    *available = 0;
    (void)read_sysctl_u64("hw.physmem64", total);
    if (*total == 0) (void)read_sysctl_u64("hw.physmem", total);

    if (read_sysctl_u64("vm.stats.vm.v_free_count", &pages) &&
        read_sysctl_u64("hw.pagesize", &page_size))
        *available = pages * page_size;

    if (*total == 0) {
        sysconf_pages = sysconf(_SC_PHYS_PAGES);
        sysconf_page_size = sysconf(_SC_PAGESIZE);
        if (sysconf_pages > 0 && sysconf_page_size > 0)
            *total = (uint64_t)sysconf_pages * (uint64_t)sysconf_page_size;
    }
    if (*available == 0) {
#ifdef _SC_AVPHYS_PAGES
        sysconf_available = sysconf(_SC_AVPHYS_PAGES);
        sysconf_page_size = sysconf(_SC_PAGESIZE);
        if (sysconf_available > 0 && sysconf_page_size > 0)
            *available = (uint64_t)sysconf_available * (uint64_t)sysconf_page_size;
#endif
    }
    if (*available == 0 && read_sysctl_u64("hw.usermem", &user_memory))
        *available = user_memory;
    if (*total > 0 && *available > *total)
        *available = *total;
}
#endif

static void set_config_value(SfetchConfig *config, const char *key, const char *value) {
    int enabled = parse_toggle(value);
    char *end;
    double number;
    long frames;
    if (strcmp(key, "logo") == 0) config->logo = enabled;
    else if (strcmp(key, "header") == 0) config->header = enabled;
    else if (strcmp(key, "os") == 0) config->os = enabled;
    else if (strcmp(key, "kernel") == 0) config->kernel = enabled;
    else if (strcmp(key, "uptime") == 0) config->uptime = enabled;
    else if (strcmp(key, "cpu") == 0) config->cpu = enabled;
    else if (strcmp(key, "gpu") == 0) config->gpu = enabled;
    else if (strcmp(key, "memory") == 0) config->memory = enabled;
    else if (strcmp(key, "disks") == 0) config->disks = enabled;
    else if (strcmp(key, "swap") == 0) config->swap = enabled;
    else if (strcmp(key, "packages") == 0) config->packages = enabled;
    else if (strcmp(key, "terminal") == 0) config->terminal = enabled;
    else if (strcmp(key, "local_ip") == 0) config->local_ip = enabled;
    else if (strcmp(key, "display") == 0) config->display = enabled;
    else if (strcmp(key, "battery") == 0) config->battery = enabled;
    else if (strcmp(key, "chassis") == 0) config->chassis = enabled;
    else if (strcmp(key, "processes") == 0) config->processes = enabled;
    else if (strcmp(key, "arch") == 0) config->arch = enabled;
    else if (strcmp(key, "shell") == 0) config->shell = enabled;
    else if (strcmp(key, "palette") == 0) config->palette = enabled;
    else if (strcmp(key, "infinite") == 0) config->larp_infinite = enabled;
    else if (strcmp(key, "fps") == 0) {
        number = strtod(value, &end);
        if (*value != '\0' && *trim_whitespace(end) == '\0' && isfinite(number) && number > 0)
            config->larp_fps = number;
    } else if (strcmp(key, "frames") == 0) {
        frames = strtol(value, &end, 10);
        if (*value != '\0' && *trim_whitespace(end) == '\0' && frames > 0 && frames <= INT_MAX)
            config->larp_frames = (int)frames;
    }
}

static void load_config(SfetchConfig *config) {
    FILE *file;
    char line[256];

    *config = (SfetchConfig){
        .logo = 1, .header = 1, .os = 1, .kernel = 1, .uptime = 1,
        .cpu = 1, .gpu = 1, .memory = 1, .disks = 1, .swap = 1,
        .packages = 1, .terminal = 1, .local_ip = 1, .display = 1,
        .battery = 1, .chassis = 1, .processes = 1, .arch = 1,
        .shell = 1, .palette = 1, .larp_fps = 12.5, .larp_infinite = 1,
        .larp_frames = 48
    };

    (void)mkdir("/etc/sfetch", 0755);
    file = fopen("/etc/sfetch/config", "r");
    if (!file) {
        file = fopen("/etc/sfetch/config", "wx");
        if (file) {
            fputs("# sfetch configuration: use true/false to toggle sections.\n", file);
            fputs("logo=true\nheader=true\nos=true\nkernel=true\nuptime=true\n", file);
            fputs("cpu=true\ngpu=true\nmemory=true\ndisks=true\nswap=true\n", file);
            fputs("packages=true\nterminal=true\nlocal_ip=true\ndisplay=true\n", file);
            fputs("battery=true\nchassis=true\nprocesses=true\narch=true\nshell=true\n", file);
            fputs("palette=true\n", file);
            fputs("fps=12.5\ninfinite=true\nframes=48\n", file);
            fclose(file);
            file = fopen("/etc/sfetch/config", "r");
        }
    }
    if (!file) return;
    while (fgets(line, sizeof(line), file)) {
        char *entry = trim_whitespace(line);
        char *equals;
        if (*entry == '\0' || *entry == '#') continue;
        equals = strchr(entry, '=');
        if (!equals) continue;
        *equals = '\0';
        set_config_value(config, trim_whitespace(entry), trim_whitespace(equals + 1));
    }
    fclose(file);
}

void get_pretty_name(char *buffer, size_t size) {
#if SFETCH_MACOS
    sfetch_macos_get_pretty_name(buffer, size);
    return;
#endif
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) {
        f = fopen("/usr/lib/os-release", "r");
        if (!f) {
            struct utsname sys;
            if (uname(&sys) == 0)
                snprintf(buffer, size, "%s", sys.sysname);
            else
                snprintf(buffer, size, "Unknown OS");
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
    {
        struct utsname sys;
        if (uname(&sys) == 0)
            snprintf(buffer, size, "%s", sys.sysname);
        else
            snprintf(buffer, size, "Unknown OS");
    }
}

static void get_os_id(char *buffer, size_t size) {
#if SFETCH_MACOS
    sfetch_macos_get_os_id(buffer, size);
    return;
#endif
#if SFETCH_BSD
    struct utsname sys;
    if (uname(&sys) == 0) {
        size_t i;
        for (i = 0; i + 1 < size && sys.sysname[i] != '\0'; ++i)
            buffer[i] = (char)tolower((unsigned char)sys.sysname[i]);
        buffer[i] = '\0';
        return;
    }
#endif

    FILE *file = fopen("/etc/os-release", "r");
    char line[256];

    if (!file) {
        snprintf(buffer, size, "unknown");
        return;
    }
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "ID=", 3) != 0) continue;
        char *value = line + 3;
        value[strcspn(value, "\r\n")] = '\0';
        size_t value_length = strlen(value);
        if (value_length >= 2 && (*value == '\"' || *value == '\'') &&
            value[value_length - 1] == *value) {
            value[value_length - 1] = '\0';
            ++value;
        }
        size_t copy_length = strlen(value);
        if (copy_length >= size) copy_length = size - 1;
        memcpy(buffer, value, copy_length);
        buffer[copy_length] = '\0';
        fclose(file);
        return;
    }
    fclose(file);
    snprintf(buffer, size, "unknown");
}

void get_cpu_model(char *buffer, size_t size) {
#if SFETCH_MACOS
    sfetch_macos_get_cpu_model(buffer, size);
    return;
#endif
#if SFETCH_BSD
    if (read_sysctl_string("hw.model", buffer, size)) {
        buffer[strcspn(buffer, "\r\n")] = '\0';
        return;
    }
#endif
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
#if SFETCH_MACOS
    return sfetch_macos_get_cpu_cores();
#endif
#if SFETCH_BSD
    uint64_t cores;
    if (read_sysctl_u64("hw.ncpu", &cores)) return (int)cores;
#endif
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
#if SFETCH_MACOS
    return sfetch_macos_get_cpu_mhz();
#endif
#if SFETCH_BSD
    uint64_t frequency;
    if (read_sysctl_u64("hw.cpufrequency", &frequency))
        return (double)frequency / 1000000.0;
#endif
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

static int is_drm_card(const char *name) {
    const char *cursor;
    if (strncmp(name, "card", 4) != 0 || name[4] == '\0') return 0;
    for (cursor = name + 4; *cursor != '\0'; ++cursor)
        if (*cursor < '0' || *cursor > '9') return 0;
    return 1;
}

static int get_lspci_name(const char *card_name, char *name, size_t name_size) {
    char device_path[PATH_MAX];
    char device_real_path[PATH_MAX];
    char command[PATH_MAX + 64];
    char line[512];
    const char *device;
    char *description;
    FILE *file;

    if (snprintf(device_path, sizeof(device_path), "/sys/class/drm/%s/device", card_name) >=
        (int)sizeof(device_path)) return 0;
    if (!realpath(device_path, device_real_path)) return 0;
    device = strrchr(device_real_path, '/');
    if (!device || device[1] == '\0') return 0;
    ++device;

    if (snprintf(command, sizeof(command), "lspci -D -s %s -nn 2>/dev/null", device) >=
        (int)sizeof(command)) return 0;
    file = popen(command, "r");
    if (!file) return 0;
    if (!fgets(line, sizeof(line), file)) {
        (void)pclose(file);
        return 0;
    }
    (void)pclose(file);

    description = strstr(line, "]: ");
    if (!description) return 0;
    description += 3;
    description[strcspn(description, "\r\n")] = '\0';
    {
        char *pci_id = strrchr(description, '[');
        if (pci_id && pci_id > description && pci_id[-1] == ' ') pci_id[-1] = '\0';
    }
    if (*description == '\0') return 0;
    snprintf(name, name_size, "%s", description);
    return 1;
}

static int print_wsl_gpus(void) {
#if !SFETCH_MACOS && !SFETCH_BSD
    typedef struct { uint32_t data1; uint16_t data2, data3; unsigned char data4[8]; } WslGuid;
    typedef struct WslUnknown WslUnknown;
    typedef struct {
        WslUnknown *(*query_interface)(WslUnknown *, const WslGuid *, void **);
        unsigned long (*add_ref)(WslUnknown *);
        unsigned long (*release)(WslUnknown *);
    } WslUnknownVtbl;
    struct WslUnknown { WslUnknownVtbl *vtable; };
    typedef struct {
        WslUnknownVtbl unknown;
        int (*create_adapter_list)(void *, uint32_t, const WslGuid *, const WslGuid *, void **);
    } WslFactoryVtbl;
    typedef struct {
        WslUnknownVtbl unknown;
        int (*get_adapter)(void *, uint32_t, const WslGuid *, void **);
        uint32_t (*get_adapter_count)(void *);
    } WslListVtbl;
    typedef struct {
        WslUnknownVtbl unknown;
        unsigned char *reserved[3];
        int (*get_property)(void *, uint32_t, size_t, void *);
        int (*get_property_size)(void *, uint32_t, size_t *);
    } WslAdapterVtbl;
    typedef int (*create_factory_fn)(const WslGuid *, void **);
    static const WslGuid factory_iid = {0x78ee5945, 0xc36e, 0x4b13,
        {0xa6, 0x69, 0x00, 0x5d, 0xd1, 0x1c, 0x0f, 0x06}};
    static const WslGuid list_iid = {0x526c7776, 0x40e9, 0x459b,
        {0xb7, 0x11, 0xf3, 0x2a, 0xd7, 0x6d, 0xfc, 0x28}};
    static const WslGuid adapter_iid = {0xf0db4c7f, 0xfe5a, 0x42a2,
        {0xbd, 0x62, 0xf2, 0xa6, 0xcf, 0x6f, 0xc8, 0x3e}};
    static const WslGuid d3d12_graphics = {0x8c47866b, 0x7583, 0x450d,
        {0xf0, 0xf0, 0x6b, 0xad, 0xa8, 0x95, 0xaf, 0x4b}};
    {
    void *library = dlopen("/usr/lib/wsl/lib/libdxcore.so", RTLD_LAZY);
    create_factory_fn create_factory;
    union { void *object; create_factory_fn function; } create_factory_symbol;
    WslUnknown *factory = NULL;
    WslUnknown *list = NULL;
    uint32_t gpu_idx = 0;
    const char *label_color = colors_enabled() ? ANSI_CYAN : "";
    const char *reset = colors_enabled() ? ANSI_RESET : "";

    if (!library) return 0;
    create_factory_symbol.object = dlsym(library, "DXCoreCreateAdapterFactory");
    create_factory = create_factory_symbol.function;
    if (!create_factory || create_factory(&factory_iid, (void **)&factory) != 0) {
        dlclose(library);
        return 0;
    }
    if (((WslFactoryVtbl *)factory->vtable)->create_adapter_list(factory, 1,
            &d3d12_graphics, &list_iid, (void **)&list) != 0) {
        factory->vtable->release(factory);
        dlclose(library);
        return 0;
    }
    {
        uint32_t count = ((WslListVtbl *)list->vtable)->get_adapter_count(list);
        for (uint32_t i = 0; i < count; ++i) {
            WslUnknown *adapter = NULL;
            char description[512] = "";
            char name[512] = "";
            uint64_t dedicated = 0, shared = 0;
            unsigned char integrated = 0;
            unsigned char hardware = 0;
            size_t description_size = sizeof(description);
            if (((WslListVtbl *)list->vtable)->get_adapter(list, i, &adapter_iid,
                    (void **)&adapter) != 0 || !adapter) continue;
            WslAdapterVtbl *adapter_vtable = (WslAdapterVtbl *)adapter->vtable;
            (void)adapter_vtable->get_property(adapter, 2, description_size, description);
            (void)adapter_vtable->get_property(adapter, 7, sizeof(dedicated), &dedicated);
            (void)adapter_vtable->get_property(adapter, 9, sizeof(shared), &shared);
            (void)adapter_vtable->get_property(adapter, 12, sizeof(integrated), &integrated);
            (void)adapter_vtable->get_property(adapter, 11, sizeof(hardware), &hardware);
            snprintf(name, sizeof(name), "%s", description);
            if (name[0] != '\0') {
                const uint64_t memory = dedicated > 0 ? dedicated : shared;
                const char *kind = integrated ? "Integrated" : "Discrete";
                if (memory >= 1024ULL * 1024ULL * 1024ULL)
                    printf("%sGPU%u%s    : %s (%.2f GiB) [%s]\n", label_color,
                           gpu_idx++, reset, name, (double)memory / (1024.0 * 1024.0 * 1024.0), kind);
                else
                    printf("%sGPU%u%s    : %s (%.2f MiB) [%s]\n", label_color,
                           gpu_idx++, reset, name, (double)memory / (1024.0 * 1024.0), kind);
            }
            adapter->vtable->release(adapter);
        }
    }
    list->vtable->release(list);
    factory->vtable->release(factory);
    dlclose(library);
    if (gpu_idx > 0) return (int)gpu_idx;
    }

    FILE *gpu_file;
    char line[512];
    int gpu_idx = 0;
    const char *label_color = colors_enabled() ? ANSI_CYAN : "";
    const char *reset = colors_enabled() ? ANSI_RESET : "";

    gpu_file = popen("if [ -x /usr/lib/wsl/lib/nvidia-smi ]; then /usr/lib/wsl/lib/nvidia-smi --query-gpu=name,memory.total --format=csv,noheader,nounits; else nvidia-smi --query-gpu=name,memory.total --format=csv,noheader,nounits; fi 2>/dev/null", "r");
    if (!gpu_file) return 0;
    while (fgets(line, sizeof(line), gpu_file)) {
        char *name = trim_whitespace(line);
        char *separator = strchr(name, '|');
        if (!separator) separator = strchr(name, ',');
        unsigned long long vram_bytes = 0;
        const char *kind = "Discrete";
        char vram[64] = "Unknown";

        if (separator) {
            char *end;
            *separator++ = '\0';
            separator = trim_whitespace(separator);
            vram_bytes = strtoull(separator, &end, 10) * 1024ULL * 1024ULL;
            if (*separator == '\0' || *end != '\0') vram_bytes = 0;
        }
        if (*name == '\0') continue;
        if (vram_bytes >= 1024ULL * 1024ULL * 1024ULL)
            snprintf(vram, sizeof(vram), "%.2f GiB", (double)vram_bytes / (1024.0 * 1024.0 * 1024.0));
        else if (vram_bytes > 0)
            snprintf(vram, sizeof(vram), "%.2f MiB", (double)vram_bytes / (1024.0 * 1024.0));
        printf("%sGPU%d%s    : %s (%s) [%s]\n", label_color, gpu_idx++, reset,
               name, vram, kind);
    }
    (void)pclose(gpu_file);
    return gpu_idx;
#else
    return 0;
#endif
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
#if SFETCH_MACOS
    sfetch_macos_print_display(cyan, reset);
    return;
#endif
#if defined(__linux__)
    int printed = 0;
    for (int card = 0; card < 16; ++card) {
        char path[64];
        int fd;
        struct drm_mode_card_res resources = {0};
        uint32_t *connectors = NULL;
        snprintf(path, sizeof(path), "/dev/dri/card%d", card);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &resources) != 0 ||
            resources.count_connectors == 0) {
            close(fd);
            continue;
        }
        connectors = calloc(resources.count_connectors, sizeof(*connectors));
        if (!connectors) { close(fd); continue; }
        resources.connector_id_ptr = (uintptr_t)connectors;
        if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &resources) != 0) {
            free(connectors);
            close(fd);
            continue;
        }
        for (uint32_t i = 0; i < resources.count_connectors; ++i) {
            struct drm_mode_get_connector connector = {0};
            struct drm_mode_modeinfo *modes = NULL;
            struct drm_mode_get_encoder encoder = {0};
            struct drm_mode_crtc crtc = {0};
            uint32_t crtc_id = 0;
            int width = 0, height = 0;
            double refresh = 0.0;
            connector.connector_id = connectors[i];
            if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &connector) != 0 ||
                connector.connection != DRM_MODE_CONNECTED || connector.count_modes == 0)
                continue;
            modes = calloc(connector.count_modes, sizeof(*modes));
            if (!modes) continue;
            connector.modes_ptr = (uintptr_t)modes;
            if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &connector) != 0) {
                free(modes);
                continue;
            }
            if (connector.encoder_id != 0) {
                encoder.encoder_id = connector.encoder_id;
                if (ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &encoder) == 0)
                    crtc_id = encoder.crtc_id;
            }
            if (crtc_id != 0) {
                crtc.crtc_id = crtc_id;
                if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &crtc) == 0 && crtc.mode_valid) {
                    width = (int)crtc.mode.hdisplay;
                    height = (int)crtc.mode.vdisplay;
                    refresh = crtc.mode.vrefresh;
                }
            }
            if (width == 0) {
                for (uint32_t mode = 0; mode < connector.count_modes; ++mode) {
                    if ((modes[mode].type & DRM_MODE_TYPE_PREFERRED) != 0 || mode == 0) {
                        width = (int)modes[mode].hdisplay;
                        height = (int)modes[mode].vdisplay;
                        refresh = modes[mode].vrefresh;
                        if ((modes[mode].type & DRM_MODE_TYPE_PREFERRED) != 0) break;
                    }
                }
            }
            if (width > 0 && height > 0) {
                const char *connector_type = "External";
                if (connector.connector_type == DRM_MODE_CONNECTOR_eDP ||
                    connector.connector_type == DRM_MODE_CONNECTOR_LVDS ||
                    connector.connector_type == DRM_MODE_CONNECTOR_DSI)
                    connector_type = "Internal";
                printf("%sDisplay%s  : %dx%d @ %.0f Hz (%s)\n", cyan, reset,
                       width, height, refresh, connector_type);
                printed++;
            }
            free(modes);
        }
        free(connectors);
        close(fd);
    }
    if (printed > 0) return;
#endif
    FILE *display_command;
    char line[512];
    int width = 0, height = 0;
    double refresh = 0.0;
    int reading_display_modes = 0;

    display_command = popen("xrandr --current 2>/dev/null", "r");
    if (display_command) {
        while (fgets(line, sizeof(line), display_command)) {
            char *connected = strstr(line, " connected");
            if (connected) {
                for (char *position = connected; *position != '\0'; ++position) {
                    int candidate_width, candidate_height;
                    if (sscanf(position, " %dx%d+", &candidate_width, &candidate_height) == 2) {
                        width = candidate_width;
                        height = candidate_height;
                        reading_display_modes = 1;
                        break;
                    }
                }
                continue;
            }

            if (reading_display_modes && line[0] != ' ' && line[0] != '\t') break;
            if (reading_display_modes) {
                int mode_width, mode_height;
                char *mode = line;
                while (*mode != '\0' && !isdigit((unsigned char)*mode)) ++mode;
                if (sscanf(mode, "%dx%d", &mode_width, &mode_height) == 2 &&
                    mode_width == width && mode_height == height) {
                    char *active_marker = strchr(mode, '*');
                    if (active_marker) {
                        char *rate_start = active_marker;
                        while (rate_start > mode &&
                               (isdigit((unsigned char)rate_start[-1]) || rate_start[-1] == '.'))
                            --rate_start;
                        refresh = strtod(rate_start, NULL);
                        break;
                    }
                }
            }
        }
        (void)pclose(display_command);
    }

    if (width > 0 && height > 0 && refresh > 0.0)
        printf("%sDisplay%s  : %dx%d @ %.0f Hz\n", cyan, reset, width, height, refresh);
    else
        printf("%sDisplay%s  : Unknown\n", cyan, reset);
}

static int read_first_line(const char *path, char *buffer, size_t size) {
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    if (!fgets(buffer, (int)size, file)) {
        fclose(file);
        return 0;
    }
    fclose(file);
    buffer[strcspn(buffer, "\r\n")] = '\0';
    return 1;
}

static void print_logo_line(const char *line, const char *const logo_colors[9],
                            const char *reset) {
    for (size_t i = 0; line[i] != '\0'; ++i) {
        if (line[i] == '$' && line[i + 1] >= '1' && line[i + 1] <= '9') {
            fputs(logo_colors[line[i + 1] - '1'], stdout);
            ++i;
        } else {
            putchar(line[i]);
        }
    }
    fputs(reset, stdout);
    putchar('\n');
}

static void print_logo(const char *const logo_colors[9], const char *reset,
                       const char *requested_logo) {
    char os_id[64];
    char path[PATH_MAX];
    char line[512];
    FILE *logo;

    if (requested_logo && requested_logo[0] != '\0') {
        snprintf(os_id, sizeof(os_id), "%s", requested_logo);
        logo = NULL;
    } else {
        logo = fopen("/etc/sfetch/logo", "r");
        if (logo) {
            if (fseek(logo, 0, SEEK_END) != 0 || ftell(logo) == 0) {
                fclose(logo);
                logo = NULL;
            } else {
                rewind(logo);
            }
        }
        get_os_id(os_id, sizeof(os_id));
    }
    if (!logo) {
        if (os_id[0] < 'a' || os_id[0] > 'z') snprintf(os_id, sizeof(os_id), "unknown");
        snprintf(path, sizeof(path), "assets/ascii/%c/%s.txt", os_id[0], os_id);
        logo = fopen(path, "r");
        if (!logo) {
            snprintf(path, sizeof(path), "/usr/local/share/sfetch/assets/ascii/%c/%s.txt",
                     os_id[0], os_id);
            logo = fopen(path, "r");
        }
        if (!logo) logo = fopen("assets/ascii/_/unknown.txt", "r");
        if (!logo) logo = fopen("/usr/local/share/sfetch/assets/ascii/_/unknown.txt", "r");
    }
    if (!logo) return;

    while (fgets(line, sizeof(line), logo)) {
        line[strcspn(line, "\r\n")] = '\0';
        print_logo_line(line, logo_colors, reset);
    }
    fclose(logo);
    putchar('\n');
}

static int battery_exists(void) {
#if SFETCH_MACOS
    return sfetch_macos_battery_exists();
#endif
    DIR *directory = opendir("/sys/class/power_supply");
    struct dirent *entry;
    char type_path[PATH_MAX];
    char type[32];

    if (!directory) return 0;
    while ((entry = readdir(directory)) != NULL) {
        if (strncmp(entry->d_name, "BAT", 3) != 0) continue;
        snprintf(type_path, sizeof(type_path), "/sys/class/power_supply/%s/type", entry->d_name);
        if (read_first_line(type_path, type, sizeof(type)) && strcmp(type, "Battery") == 0) {
            closedir(directory);
            return 1;
        }
    }
    closedir(directory);
    return 0;
}

static size_t visible_width(const char *line) {
    size_t width = 0;
    for (size_t i = 0; line[i] != '\0'; ++i) {
        if (line[i] == '\033' && line[i + 1] == '[') {
            i += 2;
            while (line[i] != '\0' && line[i] != 'm') ++i;
        } else if (((unsigned char)line[i] & 0xC0) == 0x80) {
            /* UTF-8 continuation bytes are part of the previous cell. */
            continue;
        } else {
            ++width;
        }
    }
    return width;
}

static void print_side_by_side(FILE *logo, FILE *information) {
    char logo_line[1024];
    char info_line[2048];
    int has_logo;
    int has_information;
    size_t logo_width = 0;
    size_t info_column;

    /*
     * The old fixed column could overlap wide logos (for example nixos).
     * Find the widest rendered line first, then keep the historical minimum
     * column width for smaller logos.
     */
    rewind(logo);
    while (fgets(logo_line, sizeof(logo_line), logo) != NULL) {
        logo_line[strcspn(logo_line, "\r\n")] = '\0';
        size_t width = visible_width(logo_line);
        if (width > logo_width) logo_width = width;
    }
    info_column = logo_width + 4;
    if (info_column < 42) info_column = 42;

    rewind(logo);
    rewind(information);
    do {
        has_logo = fgets(logo_line, sizeof(logo_line), logo) != NULL;
        has_information = fgets(info_line, sizeof(info_line), information) != NULL;
        if (!has_logo && !has_information) break;

        if (has_logo) {
            logo_line[strcspn(logo_line, "\r\n")] = '\0';
            fputs(logo_line, stdout);
            size_t width = visible_width(logo_line);
            for (size_t i = width; i < info_column; ++i) putchar(' ');
        } else {
            for (size_t i = 0; i < info_column; ++i) putchar(' ');
        }
        if (has_information) {
            info_line[strcspn(info_line, "\r\n")] = '\0';
            fputs(info_line, stdout);
        }
        putchar('\n');
    } while (has_logo || has_information);
}

static int print_battery(const char *cyan, const char *reset) {
#if SFETCH_MACOS
    return sfetch_macos_print_battery(cyan, reset);
#endif
    DIR *directory = opendir("/sys/class/power_supply");
    struct dirent *entry;
    int found = 0;

    if (!directory) return 0;

    while ((entry = readdir(directory)) != NULL) {
        char type_path[PATH_MAX];
        char capacity_path[PATH_MAX];
        char status_path[PATH_MAX];
        char name_path[PATH_MAX];
        char type[32] = "";
        char status[32] = "Unknown";
        char name[128] = "";
        char value[64];
        int capacity;

        if (strncmp(entry->d_name, "BAT", 3) != 0) continue;
        snprintf(type_path, sizeof(type_path), "/sys/class/power_supply/%s/type", entry->d_name);
        if (!read_first_line(type_path, type, sizeof(type)) || strcmp(type, "Battery") != 0)
            continue;

        snprintf(capacity_path, sizeof(capacity_path),
                 "/sys/class/power_supply/%s/capacity", entry->d_name);
        snprintf(status_path, sizeof(status_path),
                 "/sys/class/power_supply/%s/status", entry->d_name);
        snprintf(name_path, sizeof(name_path),
                 "/sys/class/power_supply/%s/model_name", entry->d_name);
        if (!read_first_line(capacity_path, value, sizeof(value)) ||
            sscanf(value, "%d", &capacity) != 1)
            continue;
        (void)read_first_line(status_path, status, sizeof(status));
        if (!read_first_line(name_path, name, sizeof(name)) || name[0] == '\0')
            strncpy(name, entry->d_name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        const char *battery_color = capacity <= 20 ? ANSI_RED :
                                    capacity <= 40 ? ANSI_YELLOW : ANSI_GREEN;
        if (!colors_enabled()) battery_color = "";
        printf("%sBattery%s (%s): %s%d%%%s [%s]\n", cyan, reset, name,
               battery_color, capacity, reset, status);
        found = 1;
    }
    closedir(directory);
    return found;
}

static void print_chassis_type(const char *cyan, const char *reset, int has_battery) {
#if SFETCH_MACOS
    sfetch_macos_print_chassis(cyan, reset, has_battery);
    return;
#endif
    char value[32];
    int chassis_type;
    const char *chassis = "Unknown";

    if (read_first_line("/sys/class/dmi/id/chassis_type", value, sizeof(value)) &&
        sscanf(value, "%d", &chassis_type) == 1) {
        switch (chassis_type) {
        case 3: chassis = "Desktop"; break;
        case 4: chassis = "Low-profile Desktop"; break;
        case 5: chassis = "Pizza Box"; break;
        case 6: chassis = "Mini Tower"; break;
        case 7: chassis = "Tower"; break;
        case 8: chassis = "Portable"; break;
        case 9: chassis = "Laptop"; break;
        case 10: chassis = "Notebook"; break;
        case 14: chassis = "Sub Notebook"; break;
        case 30: chassis = "Tablet"; break;
        case 31: chassis = "Convertible"; break;
        case 32: chassis = "Detachable"; break;
        default: break;
        }
    }
    if (strcmp(chassis, "Unknown") == 0)
        chassis = has_battery ? "Laptop" : "Desktop";
    printf("%sChassis%s : %s\n", cyan, reset, chassis);
}

void print_gpus(void) {
#if SFETCH_MACOS
    sfetch_macos_print_gpus();
    return;
#endif
    DIR *dir = opendir("/sys/class/drm");
    if (!dir) {
        (void)print_wsl_gpus();
        return;
    }

    struct dirent *entry;
    int gpu_idx = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (!is_drm_card(entry->d_name)) continue;

        char vendor_path[1024];
        char uevent_path[1024];
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

        char gpu_name[512] = "";
        int has_gpu_name = get_lspci_name(entry->d_name, gpu_name, sizeof(gpu_name));

        const char *label_color = colors_enabled() ? ANSI_CYAN : "";
        const char *reset = colors_enabled() ? ANSI_RESET : "";
        double temperature = get_gpu_temperature(entry->d_name);
        if (has_gpu_name && driver[0] != '\0' && temperature > 0.0)
            printf("%sGPU%d%s    : %s (%s) | %.1f C\n", label_color, gpu_idx,
                   reset, gpu_name, driver, temperature);
        else if (has_gpu_name && driver[0] != '\0')
            printf("%sGPU%d%s    : %s (%s) | %s\n", label_color, gpu_idx, reset,
                   gpu_name, driver, "N/A");
        else if (has_gpu_name && temperature > 0.0)
            printf("%sGPU%d%s    : %s | %.1f C\n", label_color, gpu_idx, reset,
                   gpu_name, temperature);
        else if (has_gpu_name)
            printf("%sGPU%d%s    : %s | N/A\n", label_color, gpu_idx, reset,
                   gpu_name);
        else if (driver[0] != '\0' && temperature > 0.0)
            printf("%sGPU%d%s    : %s (%s) | %.1f C\n", label_color, gpu_idx,
                   reset, vendor_name, driver, temperature);
        else if (driver[0] != '\0')
            printf("%sGPU%d%s    : %s (%s) | %s\n", label_color, gpu_idx, reset,
                   vendor_name, driver, "N/A");
        else if (temperature > 0.0)
            printf("%sGPU%d%s    : %s | %.1f C\n", label_color, gpu_idx, reset,
                   vendor_name, temperature);
        else
            printf("%sGPU%d%s    : %s | N/A\n", label_color,
                   gpu_idx, reset, vendor_name);

        gpu_idx++;
    }
    closedir(dir);
    if (gpu_idx == 0) (void)print_wsl_gpus();
}

void print_gpus(void);

static int collect_gpu_lines(char gpus[][512], int max_gpus) {
    FILE *capture;
    int saved_stdout;
    int count = 0;
    char line[512];
    if (max_gpus <= 0 || (capture = tmpfile()) == NULL) return 0;
    fflush(stdout);
    saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0 || dup2(fileno(capture), STDOUT_FILENO) < 0) {
        if (saved_stdout >= 0) close(saved_stdout);
        fclose(capture);
        return 0;
    }
    print_gpus();
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    rewind(capture);
    while (count < max_gpus && fgets(line, sizeof(line), capture)) {
        char *value = trim_whitespace(line);
        if (strncmp(value, "GPU", 3) != 0) continue;
        value[strcspn(value, "\r\n")] = '\0';
        snprintf(gpus[count++], 512, "%s", value);
    }
    fclose(capture);
    return count;
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
#if SFETCH_MACOS
    sfetch_macos_print_disks(colors_enabled() ? ANSI_CYAN : "",
                             colors_enabled() ? ANSI_RESET : "");
    return;
#endif
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
#if SFETCH_MACOS
    sfetch_macos_print_swap(colors_enabled() ? ANSI_CYAN : "",
                            colors_enabled() ? ANSI_RESET : "");
    return;
#endif
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

#if !SFETCH_BSD
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
#endif

void print_packages(void) {
#if SFETCH_MACOS
    sfetch_macos_print_packages(colors_enabled() ? ANSI_CYAN : "",
                                colors_enabled() ? ANSI_RESET : "");
    return;
#endif
    unsigned long count = 0;
    const char *manager = NULL;

#if SFETCH_BSD
    if (access("/usr/local/sbin/pkg", X_OK) == 0 ||
        access("/usr/local/bin/pkg", X_OK) == 0) {
        count = count_command_lines("pkg info -a 2>/dev/null");
        manager = "pkg";
    } else if (access("/usr/sbin/pkg_info", X_OK) == 0 ||
               access("/usr/bin/pkg_info", X_OK) == 0) {
        count = count_command_lines("pkg_info -a 2>/dev/null");
        manager = "pkg_info";
    }
#else
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
#endif

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

static void print_json_string(const char *value) {
    for (const char *p = value; *p != '\0'; ++p) {
        switch (*p) {
        case '"': fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        default: putchar(*p); break;
        }
    }
}

static int collect_local_ip(char *buffer, size_t size) {
    struct ifaddrs *interfaces;
    struct ifaddrs *interface;
    char address[INET_ADDRSTRLEN];
    int printed = 0;

    buffer[0] = '\0';
    if (getifaddrs(&interfaces) != 0) return 0;

    for (interface = interfaces; interface != NULL; interface = interface->ifa_next) {
        struct sockaddr_in *ipv4;
        if (!interface->ifa_addr || interface->ifa_addr->sa_family != AF_INET ||
            (interface->ifa_flags & IFF_LOOPBACK) != 0)
            continue;

        ipv4 = (struct sockaddr_in *)interface->ifa_addr;
        if (!inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address))) continue;
        size_t remaining = size - strlen(buffer) - 1;
        int written = snprintf(buffer + strlen(buffer), remaining,
                               "%s%s", printed ? ", " : "", address);
        if (written < 0 || (size_t)written >= remaining) break;
        printed = 1;
    }
    freeifaddrs(interfaces);
    return printed;
}

static void collect_terminal(char *buffer, size_t size) {
    const char *terminal = getenv("TERM_PROGRAM");
    if (!terminal || terminal[0] == '\0') {
        if (getenv("WT_SESSION")) terminal = "Windows Terminal";
        else terminal = getenv("TERM");
    }
    snprintf(buffer, size, "%s", terminal && terminal[0] ? terminal : "Unknown");
}

static void collect_swap(SfetchSwap *swap) {
#if SFETCH_MACOS
    sfetch_macos_get_swap(swap);
    return;
#endif
    FILE *f = fopen("/proc/swaps", "r");
    unsigned long long total_kb = 0, used_kb = 0;

    if (f) {
        char line[512];
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
    }
    swap->total_kb = total_kb;
    swap->used_kb = used_kb;
}

static void collect_packages(SfetchPackages *packages) {
#if SFETCH_MACOS
    sfetch_macos_get_packages(packages);
    return;
#endif
    unsigned long count = 0;
    const char *manager = NULL;

    packages->count = 0;
    packages->available = 0;
    packages->manager[0] = '\0';

#if SFETCH_BSD
    if (access("/usr/local/sbin/pkg", X_OK) == 0 ||
        access("/usr/local/bin/pkg", X_OK) == 0) {
        count = count_command_lines("pkg info -a 2>/dev/null");
        manager = "pkg";
    } else if (access("/usr/sbin/pkg_info", X_OK) == 0 ||
               access("/usr/bin/pkg_info", X_OK) == 0) {
        count = count_command_lines("pkg_info -a 2>/dev/null");
        manager = "pkg_info";
    }
#else
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
#endif

    packages->count = count;
    if (manager) {
        packages->available = 1;
        snprintf(packages->manager, sizeof(packages->manager), "%s", manager);
    }
}

static void collect_display(SfetchDisplay *display) {
#if SFETCH_MACOS
    sfetch_macos_get_display(display);
    return;
#endif
    FILE *display_command;
    char line[512];
    int width = 0, height = 0;
    double refresh = 0.0;
    int reading_display_modes = 0;

    display->width = 0;
    display->height = 0;
    display->refresh_hz = 0.0;

#if defined(__linux__)
    for (int card = 0; card < 16; ++card) {
        char path[64];
        int fd;
        struct drm_mode_card_res resources = {0};
        uint32_t *connectors;
        snprintf(path, sizeof(path), "/dev/dri/card%d", card);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0 || ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &resources) != 0 ||
            resources.count_connectors == 0) {
            if (fd >= 0) close(fd);
            continue;
        }
        connectors = calloc(resources.count_connectors, sizeof(*connectors));
        if (!connectors) { close(fd); continue; }
        resources.connector_id_ptr = (uintptr_t)connectors;
        if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &resources) != 0) {
            free(connectors); close(fd); continue;
        }
        for (uint32_t i = 0; i < resources.count_connectors; ++i) {
            struct drm_mode_get_connector connector = { .connector_id = connectors[i] };
            struct drm_mode_modeinfo *modes;
            struct drm_mode_get_encoder encoder = {0};
            struct drm_mode_crtc crtc = {0};
            if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &connector) != 0 ||
                connector.connection != DRM_MODE_CONNECTED || connector.count_modes == 0)
                continue;
            modes = calloc(connector.count_modes, sizeof(*modes));
            if (!modes) continue;
            connector.modes_ptr = (uintptr_t)modes;
            if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &connector) == 0) {
                uint32_t crtc_id = 0;
                if (connector.encoder_id != 0) {
                    encoder.encoder_id = connector.encoder_id;
                    if (ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &encoder) == 0)
                        crtc_id = encoder.crtc_id;
                }
                if (crtc_id != 0) {
                    crtc.crtc_id = crtc_id;
                    if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &crtc) == 0 && crtc.mode_valid) {
                        display->width = (int)crtc.mode.hdisplay;
                        display->height = (int)crtc.mode.vdisplay;
                        display->refresh_hz = crtc.mode.vrefresh;
                    }
                }
                if (display->width == 0) {
                    for (uint32_t mode = 0; mode < connector.count_modes; ++mode) {
                        if ((modes[mode].type & DRM_MODE_TYPE_PREFERRED) != 0 || mode == 0) {
                            display->width = (int)modes[mode].hdisplay;
                            display->height = (int)modes[mode].vdisplay;
                            display->refresh_hz = modes[mode].vrefresh;
                            if ((modes[mode].type & DRM_MODE_TYPE_PREFERRED) != 0) break;
                        }
                    }
                }
            }
            free(modes);
            if (display->width > 0) break;
        }
        free(connectors);
        close(fd);
        if (display->width > 0) return;
    }
#endif

    display_command = popen("xrandr --current 2>/dev/null", "r");
    if (display_command) {
        while (fgets(line, sizeof(line), display_command)) {
            char *connected = strstr(line, " connected");
            if (connected) {
                for (char *position = connected; *position != '\0'; ++position) {
                    int candidate_width, candidate_height;
                    if (sscanf(position, " %dx%d+", &candidate_width, &candidate_height) == 2) {
                        width = candidate_width;
                        height = candidate_height;
                        reading_display_modes = 1;
                        break;
                    }
                }
                continue;
            }

            if (reading_display_modes && line[0] != ' ' && line[0] != '\t') break;
            if (reading_display_modes) {
                int mode_width, mode_height;
                char *mode = line;
                while (*mode != '\0' && !isdigit((unsigned char)*mode)) ++mode;
                if (sscanf(mode, "%dx%d", &mode_width, &mode_height) == 2 &&
                    mode_width == width && mode_height == height) {
                    char *active_marker = strchr(mode, '*');
                    if (active_marker) {
                        char *rate_start = active_marker;
                        while (rate_start > mode &&
                               (isdigit((unsigned char)rate_start[-1]) || rate_start[-1] == '.'))
                            --rate_start;
                        refresh = strtod(rate_start, NULL);
                        break;
                    }
                }
            }
        }
        (void)pclose(display_command);
    }

    display->width = width;
    display->height = height;
    display->refresh_hz = refresh;
}

static int collect_battery(SfetchBattery *batteries, int max) {
#if SFETCH_MACOS
    return sfetch_macos_get_battery(batteries, max);
#endif
    DIR *directory = opendir("/sys/class/power_supply");
    struct dirent *entry;
    int count = 0;

    if (!directory) return 0;

    while ((entry = readdir(directory)) != NULL && count < max) {
        char type_path[PATH_MAX];
        char capacity_path[PATH_MAX];
        char status_path[PATH_MAX];
        char name_path[PATH_MAX];
        char type[32] = "";
        char status[32] = "Unknown";
        char name[128] = "";
        char value[64];
        int capacity;

        if (strncmp(entry->d_name, "BAT", 3) != 0) continue;
        snprintf(type_path, sizeof(type_path), "/sys/class/power_supply/%s/type", entry->d_name);
        if (!read_first_line(type_path, type, sizeof(type)) || strcmp(type, "Battery") != 0)
            continue;

        snprintf(capacity_path, sizeof(capacity_path),
                 "/sys/class/power_supply/%s/capacity", entry->d_name);
        snprintf(status_path, sizeof(status_path),
                 "/sys/class/power_supply/%s/status", entry->d_name);
        snprintf(name_path, sizeof(name_path),
                 "/sys/class/power_supply/%s/model_name", entry->d_name);
        if (!read_first_line(capacity_path, value, sizeof(value)) ||
            sscanf(value, "%d", &capacity) != 1)
            continue;
        (void)read_first_line(status_path, status, sizeof(status));
        if (!read_first_line(name_path, name, sizeof(name)) || name[0] == '\0')
            strncpy(name, entry->d_name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        snprintf(batteries[count].name, sizeof(batteries[count].name), "%s", name);
        batteries[count].capacity = capacity;
        snprintf(batteries[count].status, sizeof(batteries[count].status), "%s", status);
        count++;
    }
    closedir(directory);
    return count;
}

static void collect_chassis(SfetchChassis *chassis, int has_battery) {
#if SFETCH_MACOS
    sfetch_macos_get_chassis(chassis, has_battery);
    return;
#endif
    char value[32];
    int chassis_type;
    const char *type = "Unknown";

    if (read_first_line("/sys/class/dmi/id/chassis_type", value, sizeof(value)) &&
        sscanf(value, "%d", &chassis_type) == 1) {
        switch (chassis_type) {
        case 3: type = "Desktop"; break;
        case 4: type = "Low-profile Desktop"; break;
        case 5: type = "Pizza Box"; break;
        case 6: type = "Mini Tower"; break;
        case 7: type = "Tower"; break;
        case 8: type = "Portable"; break;
        case 9: type = "Laptop"; break;
        case 10: type = "Notebook"; break;
        case 14: type = "Sub Notebook"; break;
        case 30: type = "Tablet"; break;
        case 31: type = "Convertible"; break;
        case 32: type = "Detachable"; break;
        default: break;
        }
    }
    if (strcmp(type, "Unknown") == 0)
        type = has_battery ? "Laptop" : "Desktop";
    snprintf(chassis->type, sizeof(chassis->type), "%s", type);
}

static void collect_disks(SfetchDisk *disks, int *count, int max_disks) {
#if SFETCH_MACOS
    sfetch_macos_get_disks(disks, count, max_disks);
    return;
#endif
    FILE *mounts = fopen("/proc/mounts", "r");
    char device[PATH_MAX], mountpoint[PATH_MAX], filesystem[64];
    char root_device[PATH_MAX] = "";
    int n = 0;

    *count = 0;
    if (!mounts) return;

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
                  device, mountpoint, filesystem) == 3 && n < max_disks) {
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

        strncpy(disks[n].mountpoint, mountpoint, sizeof(disks[n].mountpoint) - 1);
        disks[n].mountpoint[sizeof(disks[n].mountpoint) - 1] = '\0';
        snprintf(disks[n].filesystem, sizeof(disks[n].filesystem), "%s", filesystem);
        disks[n].total_gib = (double)total_bytes / (1024.0 * 1024.0 * 1024.0);
        disks[n].used_gib = (double)used_bytes / (1024.0 * 1024.0 * 1024.0);
        disks[n].percent = total_bytes
            ? (unsigned long)((double)used_bytes * 100.0 / total_bytes + 0.5)
            : 0;
        n++;
    }
    fclose(mounts);
    *count = n;
}

static void print_json_extras(void) {
    SfetchDisk disks[16];
    SfetchBattery batteries[8];
    SfetchSwap swap;
    SfetchPackages packages;
    SfetchDisplay display;
    SfetchChassis chassis;
    char ip_list[512] = "";
    char terminal[128] = "";
    int disk_count = 0;
    int battery_count = 0;

    collect_disks(disks, &disk_count, 16);
    battery_count = collect_battery(batteries, 8);
    collect_chassis(&chassis, battery_count > 0);
    collect_swap(&swap);
    collect_packages(&packages);
    collect_display(&display);
    collect_local_ip(ip_list, sizeof(ip_list));
    collect_terminal(terminal, sizeof(terminal));

    if (disk_count == 0) {
        printf("  \"disks\": [],\n");
    } else {
        printf("  \"disks\": [\n");
        for (int i = 0; i < disk_count; ++i) {
            printf("    {\"mountpoint\": \"");
            print_json_string(disks[i].mountpoint);
            printf("\", \"filesystem\": \"");
            print_json_string(disks[i].filesystem);
            printf("\", \"used_gib\": %.2f, \"total_gib\": %.2f, \"percent\": %lu}%s\n",
                   disks[i].used_gib, disks[i].total_gib, disks[i].percent,
                   i + 1 < disk_count ? "," : "");
        }
        printf("  ],\n");
    }

    printf("  \"swap\": {\"used_mb\": %llu, \"total_mb\": %llu},\n",
           swap.used_kb / 1024, swap.total_kb / 1024);

    if (packages.available)
        printf("  \"packages\": {\"count\": %lu, \"manager\": \"%s\"},\n",
               packages.count, packages.manager);
    else
        printf("  \"packages\": {\"count\": null, \"manager\": null},\n");

    printf("  \"terminal\": \"");
    print_json_string(terminal);
    printf("\",\n");

    if (ip_list[0] != '\0') {
        printf("  \"local_ip\": \"");
        print_json_string(ip_list);
        printf("\",\n");
    } else {
        printf("  \"local_ip\": null,\n");
    }

    printf("  \"display\": {\"width\": ");
    if (display.width > 0 && display.height > 0)
        printf("%d, \"height\": %d", display.width, display.height);
    else
        printf("null, \"height\": null");
    if (display.refresh_hz > 0.0)
        printf(", \"refresh_hz\": %.1f", display.refresh_hz);
    printf("},\n");

    if (battery_count == 0) {
        printf("  \"battery\": [],\n");
    } else {
        printf("  \"battery\": [\n");
        for (int i = 0; i < battery_count; ++i) {
            printf("    {\"name\": \"");
            print_json_string(batteries[i].name);
            printf("\", \"capacity\": %d, \"status\": \"", batteries[i].capacity);
            print_json_string(batteries[i].status);
            printf("\"}%s\n", i + 1 < battery_count ? "," : "");
        }
        printf("  ],\n");
    }

    printf("  \"chassis\": \"");
    print_json_string(chassis.type);
    printf("\"\n");
}

static void print_json_info(const char *user, const char *hostname, const char *os_name,
                            const char *kernel, const char *arch,
                            unsigned long uptime_seconds, long days, long hours, long minutes,
                            unsigned long total_ram, unsigned long free_ram,
                            int process_count, const char *cpu_model, int cpu_cores,
                            double cpu_mhz, double cpu_temperature, const char *shell) {
    unsigned long ram_used = total_ram > free_ram ? total_ram - free_ram : 0;
    unsigned long ram_percentage = total_ram
        ? (unsigned long)((double)ram_used * 100.0 / total_ram + 0.5) : 0;

    printf("{\n");
    printf("  \"user\": \"");
    print_json_string(user);
    printf("\",\n  \"hostname\": \"");
    print_json_string(hostname);
    printf("\",\n  \"os\": \"");
    print_json_string(os_name);
    printf("\",\n  \"kernel\": \"");
    print_json_string(kernel);
    printf("\",\n  \"arch\": \"");
    print_json_string(arch);
    printf("\",\n");
    printf("  \"uptime\": {\"seconds\": %lu, \"days\": %ld, \"hours\": %ld, \"minutes\": %ld},\n",
           uptime_seconds, days, hours, minutes);
    printf("  \"cpu\": {\"model\": \"");
    print_json_string(cpu_model);
    printf("\", \"cores\": %d", cpu_cores);
    if (cpu_mhz > 0.0) printf(", \"mhz\": %.2f", cpu_mhz);
    printf(", \"temperature_c\": ");
    if (cpu_temperature > 0.0) printf("%.1f", cpu_temperature);
    else printf("null");
    printf("},\n");
    printf("  \"memory\": {\"total_mb\": %lu, \"free_mb\": %lu, \"used_mb\": %lu, \"percentage\": %lu},\n",
           total_ram / 1024 / 1024, free_ram / 1024 / 1024, ram_used / 1024 / 1024,
           ram_percentage);
    printf("  \"processes\": %d,\n", process_count);
    printf("  \"shell\": \"");
    print_json_string(shell);
    printf("\",\n");
    print_json_extras();
    printf("}\n");
}

int main(int argc, char **argv)
{
    SfetchConfig config;
    load_config(&config);
    int show_logo = config.logo;
    const char *requested_logo = NULL;
    int is_json = 0;
    int is_larp = 1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "eno-logo") == 0) {
            show_logo = 0;

        } else if (strcmp(argv[i], "--logo") == 0 && i + 1 < argc) {
            requested_logo = argv[++i];

        } else if (strcmp(argv[i], "--help") == 0 ||
                strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--classic] [--no-logo] [--logo NAME] [--json] [--larp]\n", argv[0]);
            return 0;

        } else if (strcmp(argv[i], "--json") == 0) {
            is_json = 1;
            is_larp = 0;

        } else if (strcmp(argv[i], "--larp") == 0) {
            is_larp = 1;

        } else if (strcmp(argv[i], "--classic") == 0) {
            is_larp = 0;
        }
    }
    struct utsname sys;
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    char hostname[512];
    char os_name[128];
    unsigned long uptime;
    unsigned long total_ram;
    unsigned long free_ram;
    int process_count;

    if (gethostname(hostname, sizeof(hostname)) != 0) {
        perror("gethostname allocation or runtime error");
        return 1;
    }

    if (uname(&sys) != 0)
    {
        perror("uname");
        return 1;
    }

    if (pw == NULL)
    {
        perror("getpwuid");
        return 1;
    }
    get_pretty_name(os_name, sizeof(os_name));

#if SFETCH_MACOS
    {
        SfetchPlatformMemory mac_memory;
        sfetch_macos_get_memory(&mac_memory);
        uptime = mac_memory.uptime;
        total_ram = mac_memory.total_ram;
        free_ram = mac_memory.free_ram;
        process_count = mac_memory.process_count;
    }
#elif SFETCH_BSD
    {
        uint64_t bsd_total_ram;
        uint64_t bsd_free_ram;
        uptime = get_bsd_uptime();
        get_bsd_memory(&bsd_total_ram, &bsd_free_ram);
        total_ram = (unsigned long)bsd_total_ram;
        free_ram = (unsigned long)bsd_free_ram;
        process_count = (int)count_command_lines("ps -ax -o pid= 2>/dev/null");
    }
#else
    {
        struct sysinfo s_info;
        if (sysinfo(&s_info) != 0) {
            perror("sysinfo");
            return 1;
        }
        uptime = (unsigned long)s_info.uptime;
        total_ram = s_info.totalram * s_info.mem_unit;
        free_ram = s_info.freeram * s_info.mem_unit;
        process_count = s_info.procs;
    }
#endif

    {
            long days = uptime / 86400;
            long hours = (uptime % 86400) / 3600;
            long minutes = (uptime % 3600) / 60;

            char *shell_path = getenv("SHELL");

            double loads[3] = {0.0, 0.0, 0.0};
            if (getloadavg(loads, 3) < 0) {
                loads[0] = loads[1] = loads[2] = 0.0;
            }

            char cpu_model[128];
            get_cpu_model(cpu_model, sizeof(cpu_model));
            int cpu_cores = get_cpu_cores();
            double cpu_mhz = get_cpu_mhz();
            double cpu_temperature = get_cpu_temperature();

            const char *shell_name = "Unknown";
            if (shell_path) {
                shell_name = strrchr(shell_path, '/');
                shell_name = shell_name ? shell_name + 1 : shell_path;
            }

            if (is_larp) {
                SfetchLarpInfo larp_info;
                memset(&larp_info, 0, sizeof(larp_info));
                larp_info.disk_count = 0;
                larp_info.gpu_count = collect_gpu_lines(larp_info.gpus, 8);
                larp_info.battery_count = collect_battery(larp_info.batteries, 8);
                collect_disks(larp_info.disks, &larp_info.disk_count, 16);
                collect_chassis(&larp_info.chassis, larp_info.battery_count > 0);
                collect_swap(&larp_info.swap);
                collect_packages(&larp_info.packages);
                collect_display(&larp_info.display);
                collect_local_ip(larp_info.local_ip, sizeof(larp_info.local_ip));
                collect_terminal(larp_info.terminal, sizeof(larp_info.terminal));
                larp_info.terminal[sizeof(larp_info.terminal) - 1] = '\0';
                larp_info.local_ip[sizeof(larp_info.local_ip) - 1] = '\0';
                larp_info.user = pw->pw_name;
                larp_info.hostname = hostname;
                larp_info.os = os_name;
                larp_info.kernel = sys.release;
                larp_info.arch = sys.machine;
                larp_info.shell = shell_name;
                larp_info.cpu_model = cpu_model;
                larp_info.uptime = uptime;
                larp_info.total_ram = total_ram;
                larp_info.free_ram = free_ram;
                larp_info.process_count = process_count;
                larp_info.cpu_cores = cpu_cores;
                larp_info.cpu_mhz = cpu_mhz;
                larp_info.cpu_temperature = cpu_temperature;
                {
                    char os_id[64];
                    get_os_id(os_id, sizeof(os_id));
                    larp_info.os_id = os_id;
                    larp_info.larp_fps = config.larp_fps;
                    larp_info.larp_infinite = config.larp_infinite;
                    larp_info.larp_frames = config.larp_frames;
                    return sfetch_run_larp(argc, argv, &larp_info);
                }
            }

            if (is_json) {
                print_json_info(pw->pw_name, hostname, os_name, sys.release, sys.machine,
                                uptime, days, hours, minutes, total_ram, free_ram,
                                process_count, cpu_model, cpu_cores, cpu_mhz,
                                cpu_temperature, shell_name);
                return 0;
            }

            int use_colors = colors_enabled();
            /* Fastfetch-compatible $1 through $9 logo color slots. */
            const char *logo_colors[9] = {
                use_colors ? ANSI_GREEN : "",
                use_colors ? ANSI_CYAN : "",
                use_colors ? ANSI_YELLOW : "",
                use_colors ? ANSI_BLUE : "",
                use_colors ? ANSI_MAGENTA : "",
                use_colors ? ANSI_RED : "",
                use_colors ? ANSI_WHITE : "",
                use_colors ? FG_BRIGHT_CYAN : "",
                use_colors ? FG_BRIGHT_WHITE : ""
            };
            const char *green = use_colors ? ANSI_GREEN : "";
            const char *cyan = use_colors ? ANSI_CYAN : "";
            const char *dim = colors_enabled() ? ANSI_DIM : "";
            const char *reset = colors_enabled() ? ANSI_RESET : "";
            FILE *logo_output = show_logo ? tmpfile() : NULL;
            FILE *information_output = show_logo ? tmpfile() : NULL;
            int side_by_side = show_logo && logo_output != NULL && information_output != NULL;
            int saved_stdout = -1;

            if (side_by_side) {
                saved_stdout = dup(STDOUT_FILENO);
                fflush(stdout);
                dup2(fileno(logo_output), STDOUT_FILENO);
            }
            if (show_logo) print_logo(logo_colors, reset, requested_logo);
            if (side_by_side) {
                fflush(stdout);
                dup2(saved_stdout, STDOUT_FILENO);
                close(saved_stdout);
                saved_stdout = dup(STDOUT_FILENO);
                fflush(stdout);
                dup2(fileno(information_output), STDOUT_FILENO);
            }
            if (config.header) {
                printf("%s%s%s%s@%s%s\n", green, pw->pw_name, reset,
                       cyan, hostname, reset);

                size_t len = strlen(pw->pw_name) + strlen(hostname) + 1;
                for (size_t i = 0; i < len; i++) printf("%s-%s", dim, reset);
                putchar('\n');
            }

        if (shell_path == NULL) {
            printf("SHELL environment variable is not set.\n");
        }

            if (config.os) printf("%sOS%s      : %s\n", cyan, reset, os_name);
            if (config.kernel) printf("%sKernel%s  : %s\n", cyan, reset, sys.release);
            if (config.uptime) printf("%sUptime%s  : %ldd %02ldh %02ldm\n", cyan, reset, days, hours, minutes);
            if (config.cpu) {
                if (cpu_mhz > 0)
                    printf("%sCPU%s     : %s (%d) @ %.2f GHz", cyan, reset, cpu_model, cpu_cores, cpu_mhz / 1000.0);
                else
                    printf("%sCPU%s     : %s (%d)", cyan, reset, cpu_model, cpu_cores);
                if (cpu_temperature > 0.0) printf(" | %.1f C", cpu_temperature);
                else printf(" | N/A");
                putchar('\n');
            }
            if (config.gpu) print_gpus();
            if (config.memory) {
                unsigned long ram_used = total_ram - free_ram;
                unsigned long ram_percentage = total_ram
                    ? (unsigned long)((double)ram_used * 100.0 / total_ram + 0.5) : 0;
                const char *ram_color = ram_percentage >= 90 ? ANSI_RED :
                                        ram_percentage >= 75 ? ANSI_YELLOW : ANSI_GREEN;
                if (!colors_enabled()) ram_color = "";
                printf("%sMemory%s  : %lu MB / %lu MB (%s%lu%%%s)\n", cyan, reset,
                       ram_used / 1024 / 1024, total_ram / 1024 / 1024,
                       ram_color, ram_percentage, reset);
            }
            if (config.disks) print_disks();
            if (config.swap) print_swap();
            if (config.packages) print_packages();
            if (config.terminal) print_terminal();
            if (config.local_ip) print_local_ip(cyan, reset);
            if (config.display) print_display_info(cyan, reset);
            int has_battery = config.battery ? print_battery(cyan, reset) : battery_exists();
            if (config.chassis) print_chassis_type(cyan, reset, has_battery);
            if (config.processes) {
                if (process_count >= 0)
                    printf("%sProcesses%s: %d\n", cyan, reset, process_count);
                else
                    printf("%sProcesses%s: Unknown\n", cyan, reset);
            }
            if (config.arch) printf("%sArch%s    : %s\n", cyan, reset, sys.machine);
            if (config.shell) printf("%sShell%s   : %s\n", cyan, reset, shell_name);
            if (config.palette) printColorPalette();

            if (side_by_side) {
                fflush(stdout);
                dup2(saved_stdout, STDOUT_FILENO);
                close(saved_stdout);
                print_side_by_side(logo_output, information_output);
                fclose(logo_output);
                fclose(information_output);
            } else {
                if (logo_output) fclose(logo_output);
                if (information_output) fclose(information_output);
            }
        }

    return 0;
}
