#define _GNU_SOURCE

#include "embedded.h"

#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int readable_file(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static void report_python_status(PyStatus status) {
    fprintf(stderr, "sfetch: CPython initialization failed%s%s\n",
            status.err_msg ? ": " : "", status.err_msg ? status.err_msg : "");
}

static int set_python_string(PyObject *dictionary, const char *key, const char *value) {
    PyObject *object = PyUnicode_FromString(value ? value : "Unknown");
    int result;
    if (!object) return 0;
    result = PyDict_SetItemString(dictionary, key, object) == 0;
    Py_DECREF(object);
    return result;
}

static int set_python_ulong(PyObject *dictionary, const char *key, unsigned long value) {
    PyObject *object = PyLong_FromUnsignedLong(value);
    int result;
    if (!object) return 0;
    result = PyDict_SetItemString(dictionary, key, object) == 0;
    Py_DECREF(object);
    return result;
}

static int set_python_int(PyObject *dictionary, const char *key, int value) {
    PyObject *object = PyLong_FromLong(value);
    int result;
    if (!object) return 0;
    result = PyDict_SetItemString(dictionary, key, object) == 0;
    Py_DECREF(object);
    return result;
}

static int set_python_double_or_none(PyObject *dictionary, const char *key, double value) {
    PyObject *object = value > 0.0 ? PyFloat_FromDouble(value) : Py_None;
    int result;
    Py_INCREF(object);
    result = PyDict_SetItemString(dictionary, key, object) == 0;
    Py_DECREF(object);
    return result;
}

static int install_larp_info(const SfetchLarpInfo *info) {
    PyObject *main_module = PyImport_AddModule("__main__");
    PyObject *dictionary = PyDict_New();
    PyObject *globals;
    int ok = dictionary != NULL;

    if (!ok) return 0;
    ok = set_python_string(dictionary, "user", info->user) &&
         set_python_string(dictionary, "hostname", info->hostname) &&
         set_python_string(dictionary, "os", info->os) &&
         set_python_string(dictionary, "os_id", info->os_id) &&
         set_python_string(dictionary, "kernel", info->kernel) &&
         set_python_string(dictionary, "arch", info->arch) &&
         set_python_string(dictionary, "shell", info->shell) &&
         set_python_string(dictionary, "cpu_model", info->cpu_model) &&
         set_python_ulong(dictionary, "uptime", info->uptime) &&
         set_python_ulong(dictionary, "total_ram", info->total_ram) &&
         set_python_ulong(dictionary, "free_ram", info->free_ram) &&
         set_python_int(dictionary, "process_count", info->process_count) &&
         set_python_int(dictionary, "cpu_cores", info->cpu_cores) &&
         set_python_double_or_none(dictionary, "cpu_mhz", info->cpu_mhz) &&
         set_python_double_or_none(dictionary, "cpu_temperature", info->cpu_temperature);
    if (ok) {
        globals = PyModule_GetDict(main_module);
        ok = PyDict_SetItemString(globals, "sfetch_info", dictionary) == 0;
    }
    Py_DECREF(dictionary);
    return ok;
}

static int find_larp_script(const char *program, char *path, size_t size) {
    char executable[PATH_MAX];
    char candidate[PATH_MAX];
    char *last_separator;
    const char *override = getenv("SFETCH_LARP_SCRIPT");

    if (override && readable_file(override)) {
        snprintf(path, size, "%s", override);
        return 1;
    }
    if (readable_file("larp/larp.py")) {
        snprintf(path, size, "larp/larp.py");
        return 1;
    }
    if (program && program[0] != '\0') {
        if (realpath(program, executable) == NULL)
            snprintf(executable, sizeof(executable), "%s", program);
        last_separator = strrchr(executable, '/');
        if (last_separator) {
            *last_separator = '\0';
            if (snprintf(candidate, sizeof(candidate), "%s/../larp/larp.py", executable) <
                    (int)sizeof(candidate) && readable_file(candidate)) {
                snprintf(path, size, "%s", candidate);
                return 1;
            }
            if (snprintf(candidate, sizeof(candidate), "%s/larp/larp.py", executable) <
                    (int)sizeof(candidate) && readable_file(candidate)) {
                snprintf(path, size, "%s", candidate);
                return 1;
            }
        }
    }
    if (readable_file("/usr/local/share/sfetch/larp/larp.py")) {
        snprintf(path, size, "/usr/local/share/sfetch/larp/larp.py");
        return 1;
    }
    return 0;
}

int sfetch_run_larp(int argc, char **argv, const SfetchLarpInfo *info) {
    char script[PATH_MAX];
    wchar_t **wide_argv;
    PyConfig config;
    PyStatus status;
    int larp_argc = 0;
    int result = 1;

    if (!find_larp_script(argv[0], script, sizeof(script))) {
        fprintf(stderr, "sfetch: cannot find larp/larp.py (set SFETCH_LARP_SCRIPT to override)\n");
        return 1;
    }

    wide_argv = PyMem_RawCalloc((size_t)argc + 1, sizeof(*wide_argv));
    if (!wide_argv) {
        fprintf(stderr, "sfetch: unable to allocate LARP arguments\n");
        return 1;
    }
    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "--larp") == 0) continue;
        wide_argv[larp_argc] = Py_DecodeLocale(argv[i], NULL);
        if (!wide_argv[larp_argc++]) {
            fprintf(stderr, "sfetch: unable to decode LARP argument\n");
            goto cleanup_args;
        }
    }

    PyConfig_InitPythonConfig(&config);
    config.parse_argv = 0;
    status = PyConfig_SetArgv(&config, larp_argc, wide_argv);
    if (PyStatus_Exception(status)) {
        report_python_status(status);
        PyConfig_Clear(&config);
        goto cleanup_args;
    }
    status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) {
        report_python_status(status);
        goto cleanup_args;
    }
    if (!install_larp_info(info)) {
        PyErr_Print();
        Py_FinalizeEx();
        goto cleanup_args;
    }
    {
        FILE *file = fopen(script, "r");
        if (!file) {
            fprintf(stderr, "sfetch: cannot open %s\n", script);
        } else {
            result = PyRun_SimpleFileEx(file, script, 1);
        }
    }
    if (Py_FinalizeEx() < 0) result = 1;

cleanup_args:
    for (int i = 0; i < larp_argc; ++i) PyMem_RawFree(wide_argv[i]);
    PyMem_RawFree(wide_argv);
    return result == 0 ? 0 : 1;
}
