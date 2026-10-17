/* SPDX-License-Identifier: GPL-2.0-only */
/* Manual Android/Linux test. Never loads a module or touches other apps. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "rt_a17_client.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s (errno=%d)\n", __LINE__, #condition, errno); \
    failed = 1; goto cleanup; } } while (0)

int main(int argc, char **argv)
{
    char path[] = "/data/local/tmp/rt-a17-selftest-XXXXXX";
    struct rt_a17_info info;
    unsigned char original[96], changed[96], observed[96];
    unsigned char *memory = MAP_FAILED;
    void *filemap = MAP_FAILED;
    long page = sysconf(_SC_PAGESIZE);
    uint64_t base = 0;
    int fd = -1, backing = -1, pipefd[2] = {-1, -1}, status = 0, failed = 0;
    pid_t child = -1;
    char command;

    if (argc != 2 || strcmp(argv[1], "--run-owned-child")) {
        fprintf(stderr, "usage: %s --run-owned-child\n"
                "Requires an already loaded rt_a17 and normal policy access.\n"
                "Tests only this program's child and temporary mappings.\n", argv[0]);
        return 2;
    }
    CHECK(page > 0);
    fd = open("/dev/rt_a17", O_RDWR | O_CLOEXEC);
    CHECK(fd >= 0);
    CHECK(rt_a17_query(fd, &info) == 0);
    CHECK(info.abi == RT_A17_PORT_MEMORY_ABI_VERSION ||
          info.abi == RT_A17_PORT_ABI_VERSION);
    memory = mmap(NULL, 4 * page, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(memory != MAP_FAILED);
    for (size_t i = 0; i < sizeof(original); i++) {
        original[i] = (unsigned char)(i ^ 0x5a);
        changed[i] = (unsigned char)(i ^ 0xa5);
    }
    memcpy(memory + page - 32, original, sizeof(original));
    CHECK(mprotect(memory + 2 * page, page, PROT_READ) == 0);
    CHECK(munmap(memory + 3 * page, page) == 0);
    backing = mkstemp(path);
    CHECK(backing >= 0);
    CHECK(ftruncate(backing, page) == 0);
    filemap = mmap(NULL, page, PROT_READ, MAP_PRIVATE, backing, 0);
    CHECK(filemap != MAP_FAILED);
    CHECK(pipe(pipefd) == 0);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        close(pipefd[1]);
        close(fd);
        if (read(pipefd[0], &command, 1) != 1 || command != 'V')
            _exit(0);
        _exit(memcmp(memory + page - 32, changed, sizeof(changed)) ? 1 : 0);
    }
    close(pipefd[0]);
    pipefd[0] = -1;
    /* Across a page boundary, using the exact recovered opcodes/layouts. */
    CHECK(rt_a17_read(fd, child, (uintptr_t)(memory + page - 32),
                      observed, sizeof(observed)) == 0);
    CHECK(!memcmp(observed, original, sizeof(original)));
    CHECK(rt_a17_write(fd, child, (uintptr_t)(memory + page - 32),
                       changed, sizeof(changed)) == 0);
    CHECK(rt_a17_read(fd, child, (uintptr_t)(memory + page - 32),
                      observed, sizeof(observed)) == 0);
    CHECK(!memcmp(observed, changed, sizeof(changed)));
    CHECK(rt_a17_module_base(fd, child, strrchr(path, '/') + 1, &base) == 0);
    CHECK(base == (uintptr_t)filemap);
    CHECK(rt_a17_module_base(fd, child, "rt_a17_definitely_missing.so", &base) == 0);
    CHECK(base == 0);
    /* Whole range validation must reject this before modifying its first page. */
    CHECK(rt_a17_write(fd, child, (uintptr_t)(memory + 2 * page - 32),
                       changed, sizeof(changed)) == -EACCES);
    CHECK(rt_a17_read(fd, child, (uintptr_t)(memory + 2 * page - 32),
                      observed, 32) == 0);
    for (unsigned int i = 0; i < 32; i++)
        CHECK(observed[i] == 0);
    CHECK(rt_a17_read(fd, child, (uintptr_t)(memory + 3 * page), observed, 1) == -EFAULT);
    CHECK(rt_a17_read(fd, child, UINT64_MAX - 3, observed, 8) == -EFAULT);
    CHECK(rt_a17_read(fd, child, (uintptr_t)memory, (void *)1, 1) == -EFAULT);
    CHECK(rt_a17_write(fd, child, (uintptr_t)memory, (void *)1, 1) == -EFAULT);
    struct rt_copy_memory invalid = {
        .pid = child, .addr = (uintptr_t)memory,
        .buffer = (uintptr_t)observed, .size = RT_A17_MAX_TRANSFER + 1,
    };
    errno = 0;
    CHECK(ioctl(fd, RT_OP_WRITE_MEM, &invalid) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(ioctl(fd, RT_OP_BP_CONTROL, NULL) == -1 &&
          errno == (info.abi == RT_A17_PORT_ABI_VERSION ? EFAULT : EOPNOTSUPP));
    errno = 0;
    CHECK(ioctl(fd, RT_OP_INIT_KEY, NULL) == -1 && errno == EOPNOTSUPP);
    /* Also catches a target/current mmap-lock recursion regression. */
    CHECK(rt_a17_read(fd, getpid(), (uintptr_t)original, observed, sizeof(original)) == 0);
    CHECK(!memcmp(observed, original, sizeof(original)));
    command = 'V';
    CHECK(write(pipefd[1], &command, 1) == 1);
    CHECK(waitpid(child, &status, 0) == child);
    child = -1;
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
cleanup:
    if (pipefd[1] >= 0) close(pipefd[1]);
    if (pipefd[0] >= 0) close(pipefd[0]);
    if (child > 0) waitpid(child, &status, 0);
    if (filemap != MAP_FAILED) munmap(filemap, page);
    if (memory != MAP_FAILED) munmap(memory, 3 * page);
    if (backing >= 0) { close(backing); unlink(path); }
    if (fd >= 0) close(fd);
    printf("{\"test\":\"owned_child_memory\",\"passed\":%s,"
           "\"original_rt_fully_verified\":false}\n", failed ? "false" : "true");
    return failed;
}
