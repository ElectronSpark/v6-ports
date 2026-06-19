/*
 * xv6-owned Vim launcher.
 *
 * The imported Vim source is built unchanged as /libexec/vim.real.  This
 * launcher restores the terminal attributes that were active before Vim ran,
 * keeping xv6 runtime cleanup outside the upstream source tree.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
    (void)argc;

    struct termios saved;
    int have_saved = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved) == 0;

    pid_t pid = fork();
    if (pid < 0) {
        perror("vim: fork");
        return 1;
    }

    if (pid == 0) {
        execv("/libexec/vim.real", argv);
        fprintf(stderr, "vim: exec /libexec/vim.real: %s\n", strerror(errno));
        return 127;
    }

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("vim: waitpid");
            if (have_saved) {
                tcsetattr(STDIN_FILENO, TCSANOW, &saved);
            }
            return 1;
        }
    }

    if (have_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}
