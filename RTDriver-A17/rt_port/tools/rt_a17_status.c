/* SPDX-License-Identifier: GPL-2.0-only */
/* Status-only Linux/Android client for new A17 RT ABI diagnostic module. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "../rt_abi.h"

int main(void)
{
    struct rt_a17_info info = {0};
    int fd = open("/dev/rt_a17_diag", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "RT diagnostic device unavailable: %s\n", strerror(errno));
        return 2;
    }
    if (ioctl(fd, RT_A17_INFO, &info) != 0) {
        fprintf(stderr, "RT diagnostic ioctl unavailable: %s\n", strerror(errno));
        close(fd);
        return 3;
    }
    close(fd);
    printf("ABI revision: %u\n", info.abi);
    printf("Original RT operations implemented: %u\n", info.original_ioctls_implemented);
    printf("Capabilities mask: 0x%08x\n", info.capabilities);
    if (info.abi != 1 || info.original_ioctls_implemented == 0) {
        fprintf(stderr, "Driver is DIAGNOSTIC ONLY; RT features are unavailable.\n");
        return 4;
    }
    return 0;
}
