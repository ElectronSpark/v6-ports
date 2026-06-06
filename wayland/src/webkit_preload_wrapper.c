#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int (*webkit_process_main_fn)(int, char **);

void xv6_webkit_skia_signal_recovery_force_install(void);

static const char *
base_name(const char *path)
{
    const char *slash;

    if (!path || !path[0])
        return "WebKitWebProcess";
    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void
base_exe_name(const char *arg0, char *exe, size_t exe_size)
{
    const char *base = base_name(arg0);
    size_t len = strlen(base);
    static const char real_suffix[] = ".real";
    size_t suffix_len = sizeof(real_suffix) - 1;

    if (len > suffix_len &&
        strcmp(base + len - suffix_len, real_suffix) == 0)
        len -= suffix_len;
    if (len >= exe_size)
        len = exe_size - 1;
    memcpy(exe, base, len);
    exe[len] = '\0';
}

static const char *
process_symbol(const char *exe)
{
    if (strcmp(exe, "WebKitWebProcess") == 0)
        return "_ZN6WebKit14WebProcessMainEiPPc";
    if (strcmp(exe, "WebKitNetworkProcess") == 0)
        return "_ZN6WebKit18NetworkProcessMainEiPPc";
    if (strcmp(exe, "WebKitGPUProcess") == 0)
        return "_ZN6WebKit14GPUProcessMainEiPPc";
    return NULL;
}

static void *
segv_recovery_watchdog(void *arg)
{
    (void)arg;
    for (;;) {
        xv6_webkit_skia_signal_recovery_force_install();
        usleep(100000);
    }
    return NULL;
}

int
main(int argc, char **argv)
{
    char exe[96];
    const char *symbol;
    webkit_process_main_fn process_main;
    pthread_t watchdog;

    setenv("XV6_WEBKIT_SKIA_NULL_MEMBER_RECOVER", "1", 0);
    setenv("XV6_WEBKIT_SKIA_NULL_MEMBER_RECOVER_LOG", "1", 0);
    xv6_webkit_skia_signal_recovery_force_install();
    if (pthread_create(&watchdog, NULL, segv_recovery_watchdog, NULL) == 0)
        pthread_detach(watchdog);

    base_exe_name(argv[0], exe, sizeof(exe));
    symbol = process_symbol(exe);
    if (!symbol) {
        fprintf(stderr, "xv6-webkit-wrapper: unknown process %s\n", exe);
        return 127;
    }

    process_main = (webkit_process_main_fn)dlsym(RTLD_DEFAULT, symbol);
    if (!process_main) {
        fprintf(stderr, "xv6-webkit-wrapper: missing %s for %s: %s\n",
                symbol, exe, dlerror());
        return 127;
    }

    fprintf(stderr, "xv6-webkit-wrapper: enter %s symbol=%s\n", exe, symbol);
    return process_main(argc, argv);
}
