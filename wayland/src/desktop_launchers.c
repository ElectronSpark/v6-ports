#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *
base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int
run(char *const argv[])
{
    execv(argv[0], argv);
    fprintf(stderr, "%s: exec %s failed: %s\n",
            base_name(argv[0]), argv[0], strerror(errno));
    return 127;
}

int
main(int argc, char **argv)
{
    const char *name = (argc > 0 && argv[0]) ? base_name(argv[0]) : "";

    if (strcmp(name, "xv6-open-files-root") == 0 ||
        strcmp(name, "Files") == 0) {
        char *const args[] = { "/bin/filemgr", "/root", NULL };
        return run(args);
    }
    if (strcmp(name, "xv6-open-files-proc") == 0 ||
        strcmp(name, "Proc Files") == 0) {
        char *const args[] = { "/bin/filemgr", "/proc", NULL };
        return run(args);
    }
    if (strcmp(name, "xv6-open-files-etc") == 0 ||
        strcmp(name, "Config Files") == 0) {
        char *const args[] = { "/bin/filemgr", "/etc", NULL };
        return run(args);
    }
    if (strcmp(name, "xv6-open-python") == 0 ||
        strcmp(name, "Python") == 0) {
        char *const args[] = { "/bin/kde-terminal-launcher",
                               "/bin/python3.12", NULL };
        return run(args);
    }
    if (strcmp(name, "xv6-open-editor") == 0 ||
        strcmp(name, "Editor") == 0) {
        char *const args[] = { "/bin/kde-terminal-launcher",
                               "/bin/vim", NULL };
        return run(args);
    }
    if (strcmp(name, "xv6-open-gl-sphere") == 0) {
        char *const args[] = { "/bin/mesaglsmoke", "--demo", NULL };
        return run(args);
    }
    if (strcmp(name, "xv6-open-egl-demo") == 0) {
        char *const args[] = { "/bin/mesawlegl", "--demo", NULL };
        return run(args);
    }
    if (strcmp(name, "xv6-open-game-boy") == 0 ||
        strcmp(name, "Game Boy") == 0) {
        char *const args[] = {
            "/bin/peanutgb",
            "/root/roms/Pokemon_Blue_Version_USA_Europe_SGB_Enhanced.gb",
            NULL
        };
        return run(args);
    }
    if (strcmp(name, "xv6-open-webkit") == 0 ||
        strcmp(name, "WebKit") == 0) {
        char *const args[] = {
            "/libexec/webkit2gtk-4.1/MiniBrowser",
            "https://www.google.com/search?q=xv6&gbv=1",
            NULL
        };
        return run(args);
    }

    fprintf(stderr, "%s: unknown desktop launcher name\n", name);
    return 127;
}
