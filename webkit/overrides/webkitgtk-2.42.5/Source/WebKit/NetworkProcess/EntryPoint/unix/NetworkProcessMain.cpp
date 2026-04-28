/*
 * Copyright (C) 2014 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "NetworkProcessMain.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ucontext.h>
#include <unistd.h>

static void writeAll(int fd, const char* text)
{
    if (!text)
        return;
    write(fd, text, strlen(text));
}

static void dumpFile(int out, const char* path)
{
    int in = open(path, O_RDONLY);
    if (in < 0)
        return;

    char buffer[512];
    ssize_t n;
    while ((n = read(in, buffer, sizeof(buffer))) > 0)
        write(out, buffer, n);
    close(in);
}

static void networkProcessAbortHandler(int signalNumber, siginfo_t*, void* context)
{
    int fd = open("/tmp/webkit-network-abort.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        char line[256];
        auto* ucontext = static_cast<ucontext_t*>(context);
        snprintf(line, sizeof(line), "SIGABRT pid=%d rip=%p rsp=%p\n",
            static_cast<int>(getpid()),
            reinterpret_cast<void*>(ucontext->uc_mcontext.gregs[REG_RIP]),
            reinterpret_cast<void*>(ucontext->uc_mcontext.gregs[REG_RSP]));
        writeAll(fd, line);
        writeAll(fd, "-- maps --\n");
        dumpFile(fd, "/proc/self/maps");
        writeAll(fd, "-- end --\n");
        close(fd);
    }

    signal(signalNumber, SIG_DFL);
    raise(signalNumber);
}

int main(int argc, char** argv)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = networkProcessAbortHandler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    sigaction(SIGABRT, &action, nullptr);

    return WebKit::NetworkProcessMain(argc, argv);
}
