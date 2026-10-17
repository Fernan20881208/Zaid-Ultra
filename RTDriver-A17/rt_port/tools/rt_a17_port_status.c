/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "rt_a17_client.h"

int main(void)
{
    struct rt_a17_info info = {0};
    const char *state;
    int code, error = 0, fd = open("/dev/rt_a17", O_RDWR | O_CLOEXEC);
    if (fd < 0 && (errno == ENOENT || errno == ENODEV || errno == ENXIO))
        fd = open("/dev/rt_a17_diag", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        error = errno;
        code = error == ENOENT || error == ENODEV || error == ENXIO ? 2 : 5;
        state = code == 2 ? "absent" : "inaccessible";
    } else {
        int result = rt_a17_query(fd, &info);
        close(fd);
        error = -result;
        if (result) {
            code = result == -EACCES || result == -EPERM ? 5 : 3;
            state = code == 5 ? "inaccessible" : "compatibility_error";
        } else {
            code = info.abi == RT_A17_ABI_VERSION ? 4 : 6;
            state = code == 4 ? "diagnostic" : "partial_unverified";
        }
    }
    printf("{\"state\":\"%s\",\"errno\":%d,\"abi\":%u,"
           "\"capabilities\":%u,\"original_ioctls_implemented\":%u,"
           "\"functional_verified\":false}\n", state, error, info.abi,
           info.capabilities, info.original_ioctls_implemented);
    return code;
}
