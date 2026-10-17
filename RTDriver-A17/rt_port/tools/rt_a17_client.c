/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <sys/ioctl.h>
#include "rt_a17_client.h"

static int request(int fd, unsigned int op, void *arg)
{
    int result = ioctl(fd, op, arg);
    return result < 0 ? -errno : result == 0 ? 0 : -EPROTO;
}

int rt_a17_query(int fd, struct rt_a17_info *info)
{
    *info = (struct rt_a17_info){0};
    int result = request(fd, RT_A17_INFO, info);
    if (result)
        return result;
    if (info->reserved)
        return -EPROTO;
    if (info->abi == RT_A17_ABI_VERSION && !info->capabilities &&
        !info->original_ioctls_implemented)
        return 0;
    if (info->abi == RT_A17_PORT_ABI_VERSION &&
        info->capabilities == RT_A17_PORT_CAPS &&
        info->original_ioctls_implemented == RT_A17_PORT_OPS)
        return 0;
    return -EPROTO;
}

static int transfer(int fd, int32_t pid, uint64_t address, void *buffer,
                    size_t size, unsigned int op)
{
    struct rt_copy_memory req = {
        .pid = pid, .addr = address, .buffer = (uintptr_t)buffer, .size = size,
    };
    if (pid <= 0 || !size || size > RT_A17_MAX_TRANSFER)
        return -EINVAL;
    return request(fd, op, &req);
}

int rt_a17_read(int fd, int32_t pid, uint64_t address, void *buffer, size_t size)
{
    return transfer(fd, pid, address, buffer, size, RT_OP_READ_MEM);
}

int rt_a17_write(int fd, int32_t pid, uint64_t address, const void *buffer, size_t size)
{
    return transfer(fd, pid, address, (void *)buffer, size, RT_OP_WRITE_MEM);
}

int rt_a17_module_base(int fd, int32_t pid, const char *name, uint64_t *base)
{
    struct rt_module_base req = {.pid = pid, .name = (uintptr_t)name};
    if (!base)
        return -EINVAL;
    *base = 0;
    int result = request(fd, RT_OP_MODULE_BASE, &req);
    if (result)
        return result;
    *base = req.base;
    return 0;
}
