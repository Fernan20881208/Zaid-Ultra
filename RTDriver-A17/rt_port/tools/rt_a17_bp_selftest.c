/* SPDX-License-Identifier: GPL-2.0-only */
/* Explicit manual test on this program's child only; never loads a module. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "rt_a17_client.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL:%d %s errno=%d\n", \
    __LINE__, #x, errno); failed = 1; goto out; } } while (0)

static volatile uint32_t watched_word;
__attribute__((noinline, used)) static void execute_probe(void)
{
    __asm__ volatile("" ::: "memory");
}

static int wait_byte(int fd, char expected)
{
    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    char got;
    return poll(&pfd, 1, 5000) == 1 && read(fd, &got, 1) == 1 && got == expected;
}

int main(int argc, char **argv)
{
    struct rt_a17_info info;
    struct rt_a17_bp_status bpstatus;
    struct rt_breakpoint_hit hits[RT_A17_MAX_HITS];
    int fd = -1, commands[2] = {-1,-1}, replies[2] = {-1,-1};
    pid_t child = -1;
    int failed = 0, status = 0;
    size_t count = 0;
    if (argc != 2 || strcmp(argv[1], "--run-owned-child")) {
        fprintf(stderr, "usage: %s --run-owned-child (explicit manual run)\n", argv[0]);
        return 2;
    }
    fd = open("/dev/rt_a17", O_RDWR | O_CLOEXEC);
    CHECK(fd >= 0);
    CHECK(rt_a17_query(fd, &info) == 0);
    CHECK(info.abi == RT_A17_PORT_ABI_VERSION &&
          (info.capabilities & (RT_A17_CAP_BP_CONTROL | RT_A17_CAP_BP_HITS |
                                RT_A17_CAP_BP_ONESHOT)) ==
          (RT_A17_CAP_BP_CONTROL | RT_A17_CAP_BP_HITS | RT_A17_CAP_BP_ONESHOT));
    CHECK(pipe(commands) == 0 && pipe(replies) == 0);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        close(commands[1]); close(replies[0]); close(fd);
        if (!wait_byte(commands[0], 'G')) _exit(10);
        watched_word = 0x12345678;
        execute_probe();
        if (write(replies[1], "D", 1) != 1) _exit(11);
        if (!wait_byte(commands[0], 'S')) _exit(12);
        watched_word = 0x87654321;
        execute_probe();
        if (write(replies[1], "D", 1) != 1) _exit(13);
        _exit(0);
    }
    close(commands[0]); commands[0] = -1;
    close(replies[1]); replies[1] = -1;
    struct rt_breakpoint_ctl ctl = {.pid=child, .addr=(uintptr_t)&watched_word,
        .action=RT_BP_ADD, .type=RT_BP_WRITE, .len=sizeof(watched_word)};
    CHECK(rt_a17_bp_control(fd, &ctl) == 0);
    ctl.addr = (uintptr_t)execute_probe; ctl.type = RT_BP_EXECUTE; ctl.len = 4;
    CHECK(rt_a17_bp_control(fd, &ctl) == 0);
    CHECK(write(commands[1], "G", 1) == 1);
    CHECK(wait_byte(replies[0], 'D'));
    CHECK(rt_a17_bp_status(fd, &bpstatus) == 0);
    CHECK(bpstatus.registrations == 2 && bpstatus.callback_errors == 0);
    CHECK(rt_a17_bp_get_hits(fd, hits, RT_A17_MAX_HITS, &count) == 0);
    CHECK(count >= 2);
    int saw_write = 0, saw_exec = 0;
    for (size_t i = 0; i < count; i++) {
        CHECK(hits[i].pid == child && hits[i].regs.pc != 0);
        if (hits[i].addr == (uintptr_t)execute_probe) saw_exec = 1;
        if (hits[i].addr != (uintptr_t)execute_probe) saw_write = 1;
    }
    CHECK(saw_write && saw_exec);
    /* One-shot events have been disabled; a second execution/write adds no hit. */
    CHECK(write(commands[1], "S", 1) == 1);
    CHECK(wait_byte(replies[0], 'D'));
    CHECK(rt_a17_bp_get_hits(fd, hits, RT_A17_MAX_HITS, &count) == 0 && count == 0);
out:
    /* Closing the owner fd synchronously unregisters breakpoints before child teardown. */
    if (fd >= 0) { close(fd); fd = -1; }
    if (commands[1] >= 0) close(commands[1]);
    if (commands[0] >= 0) close(commands[0]);
    if (replies[0] >= 0) close(replies[0]);
    if (replies[1] >= 0) close(replies[1]);
    if (child > 0) {
        if (waitpid(child, &status, WNOHANG) == 0) { kill(child, SIGKILL); waitpid(child, &status, 0); }
    }
    printf("{\"test\":\"owned_child_breakpoints\",\"passed\":%s,\"device_verified\":false}\n",
           failed ? "false" : "true");
    return failed;
}
