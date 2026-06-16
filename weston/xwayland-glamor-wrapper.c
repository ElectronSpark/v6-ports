#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
    const char *real = "/bin/Xwayland.real";
    int extra = 4;
    char **child = calloc((size_t)argc + (size_t)extra + 1, sizeof(*child));
    if (!child) {
        perror("calloc");
        return 127;
    }

    child[0] = (char *)real;
    child[1] = "-glamor";
    child[2] = "es";
    child[3] = "-xkbdir";
    child[4] = "/usr/share/X11/xkb";
    for (int i = 1; i < argc; i++)
        child[i + extra] = argv[i];

    execv(real, child);
    fprintf(stderr, "Xwayland wrapper: execv(%s) failed: %s\n",
            real, strerror(errno));
    return 127;
}
