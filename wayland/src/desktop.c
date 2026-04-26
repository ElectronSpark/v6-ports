/*
 * desktop.c — xv6 Wayland Session Manager
 *
 * Launches the wlcomp Wayland compositor, then starts Wayland clients
 * (e.g. NetSurf browser).  Monitors child processes and performs clean
 * shutdown on SIGTERM / SIGINT.
 *
 * Started automatically by init via /etc/daemons.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define WAYLAND_SOCKET_PATH  "/tmp/wayland-0.lock" /* lockfile (real VFS file) */
#define SOCKET_WAIT_TRIES    200      /* 200 × 20 ms = 4 s */
#define SOCKET_WAIT_US       20000

static volatile sig_atomic_t g_running = 1;
static pid_t wlcomp_pid;
static pid_t client_pid;

static void sighandler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* Wait for wlcomp to create the Wayland socket.  Returns 0 on success. */
static int wait_for_socket(void)
{
    struct stat st;
    for (int i = 0; i < SOCKET_WAIT_TRIES; i++) {
        if (stat(WAYLAND_SOCKET_PATH, &st) == 0)
            return 0;
        usleep(SOCKET_WAIT_US);
    }
    return -1;
}

static pid_t launch_wlcomp(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = { "wlcomp", NULL };
        char *envp[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            NULL
        };
        execve("/bin/wlcomp", argv, envp);
        _exit(127);
    }
    return pid;
}

static pid_t launch_client(const char *path, const char *name, const char *arg1)
{
    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = { (char *)name, (char *)arg1, NULL };
        if (arg1 == NULL)
            argv[1] = NULL;
        char *envp[] = {
            "HOME=/",
            "PATH=/bin:/usr/bin",
            "XDG_RUNTIME_DIR=/tmp",
            "WAYLAND_DISPLAY=wayland-0",
            "GDK_BACKEND=wayland",
            NULL
        };
        execve(path, argv, envp);
        _exit(127);
    }
    return pid;
}

static void kill_and_reap(pid_t *pidp)
{
    if (*pidp > 0) {
        kill(*pidp, SIGTERM);
        waitpid(*pidp, NULL, 0);
        *pidp = 0;
    }
}

static void cleanup(void)
{
    kill_and_reap(&client_pid);
    kill_and_reap(&wlcomp_pid);
}

int main(void)
{
    signal(SIGINT,  sighandler);
    signal(SIGTERM, sighandler);

    fprintf(stderr, "[desktop] starting Wayland session\n");

    /* 1. Launch compositor */
    wlcomp_pid = launch_wlcomp();
    if (wlcomp_pid < 0) {
        perror("[desktop] fork wlcomp");
        return 1;
    }
    fprintf(stderr, "[desktop] wlcomp pid=%d\n", wlcomp_pid);

    /* 2. Wait for Wayland socket */
    if (wait_for_socket() < 0) {
        fprintf(stderr, "[desktop] timed out waiting for %s\n",
                WAYLAND_SOCKET_PATH);
        cleanup();
        return 1;
    }

    /* 3. Launch NetSurf as a Wayland GTK3 client. */
    client_pid = launch_client("/bin/netsurf", "netsurf", NULL);
    if (client_pid < 0) {
        perror("[desktop] fork netsurf");
        cleanup();
        return 1;
    }
    fprintf(stderr, "[desktop] netsurf pid=%d\n", client_pid);

    /* 4. Supervise compositor and client */
    while (g_running) {
        int status;
        pid_t exited = waitpid(-1, &status, WNOHANG);
        if (exited > 0) {
            if (exited == wlcomp_pid) {
                fprintf(stderr, "[desktop] wlcomp exited (status %d)\n",
                        WEXITSTATUS(status));
                wlcomp_pid = 0;
                break;  /* compositor gone → session over */
            } else if (exited == client_pid) {
                fprintf(stderr, "[desktop] netsurf exited (status %d)\n",
                        WEXITSTATUS(status));
                client_pid = 0;
            }
        }
        usleep(100000);  /* 100 ms poll */
    }

    fprintf(stderr, "[desktop] shutting down\n");
    cleanup();
    return 0;
}
