#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef int (*webkit_process_main_fn)(int, char **);

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

static bool
helper_log_enabled(void)
{
    const char *enabled = getenv("XV6_WEBKIT_HELPER_LOG");

    return enabled && strcmp(enabled, "1") == 0;
}

static void
helper_log(const char *exe, const char *fmt, ...)
{
    FILE *file;
    struct timespec ts;
    va_list ap;

    if (!helper_log_enabled())
        return;

    file = fopen("/tmp/webkit_log.txt", "a");
    if (!file)
        return;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        fprintf(file, "xv6-webkit-wrapper: t=%lld.%09ld pid=%ld exe=%s ",
                (long long)ts.tv_sec, ts.tv_nsec, (long)getpid(), exe);
    else
        fprintf(file, "xv6-webkit-wrapper: t=? pid=%ld exe=%s ",
                (long)getpid(), exe);

    va_start(ap, fmt);
    vfprintf(file, fmt, ap);
    va_end(ap);
    fputc('\n', file);
    fclose(file);
}

int
main(int argc, char **argv)
{
    char exe[96];
    const char *symbol;
    webkit_process_main_fn process_main;

    base_exe_name(argv[0], exe, sizeof(exe));
    helper_log(exe, "entry argc=%d argv0=%s", argc,
               argv[0] ? argv[0] : "(null)");
    symbol = process_symbol(exe);
    if (!symbol) {
        helper_log(exe, "unknown-process");
        fprintf(stderr, "xv6-webkit-wrapper: unknown process %s\n", exe);
        return 127;
    }
    helper_log(exe, "dlsym-begin symbol=%s", symbol);

    process_main = (webkit_process_main_fn)dlsym(RTLD_DEFAULT, symbol);
    if (!process_main) {
        const char *error = dlerror();

        helper_log(exe, "dlsym-fail symbol=%s error=%s", symbol,
                   error ? error : "(none)");
        fprintf(stderr, "xv6-webkit-wrapper: missing %s for %s: %s\n",
                symbol, exe, error ? error : "(none)");
        return 127;
    }

    helper_log(exe, "process-main-enter symbol=%s", symbol);
    fprintf(stderr, "xv6-webkit-wrapper: enter %s symbol=%s\n", exe, symbol);
    int ret = process_main(argc, argv);
    helper_log(exe, "process-main-return ret=%d", ret);
    return ret;
}
