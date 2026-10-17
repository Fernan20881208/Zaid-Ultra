/* SPDX-License-Identifier: GPL-2.0-only */
/* Status-only Linux/Android client; never loads a module or claims RT works. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "../rt_abi.h"

static int report(const char *state, int code, int error,
                  const struct rt_a17_info *info)
{
    printf("{\"state\":\"%s\",\"errno\":%d,\"abi\":%u,"
           "\"capabilities\":%u,\"original_ioctls_implemented\":%u,"
           "\"functional_verified\":false}\n",
           state, error, info->abi, info->capabilities,
           info->original_ioctls_implemented);
    return code;
}

int main(void)
{
    struct rt_a17_info info = {0};
    int fd = open("/dev/rt_a17_diag", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        int error = errno;
        fprintf(stderr, "RT diagnostic device unavailable: %s\n", strerror(error));
        if (error == ENOENT || error == ENODEV || error == ENXIO)
            return report("absent", 2, error, &info);
        return report("inaccessible", 5, error, &info);
    }
    if (ioctl(fd, RT_A17_INFO, &info) != 0) {
        int error = errno;
        fprintf(stderr, "RT diagnostic ioctl unavailable: %s\n", strerror(error));
        close(fd);
        return report(error == EPERM || error == EACCES ? "inaccessible" :
                      "compatibility_error", error == EPERM || error == EACCES ?
                      5 : 3, error, &info);
    }
    close(fd);
    if (info.abi != RT_A17_ABI_VERSION || info.capabilities != 0 ||
        info.original_ioctls_implemented != 0 || info.reserved != 0) {
        fprintf(stderr, "Unknown or inconsistent diagnostic protocol.\n");
        return report("compatibility_error", 3, 0, &info);
    }
    fprintf(stderr, "Driver is DIAGNOSTIC ONLY; RT features are unavailable.\n");
    return report("diagnostic", 4, 0, &info);
}
