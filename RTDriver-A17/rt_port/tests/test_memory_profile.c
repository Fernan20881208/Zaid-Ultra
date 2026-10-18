/* SPDX-License-Identifier: GPL-2.0-only */
/* Contract for the production memory-only build, not a device test. */
#include <stdio.h>
#define RT_A17_MEMORY_ONLY
#include "kernel_shim.h"
#include "../runtime/rt_main.c"

int main(void)
{
    struct file file = {0};
    struct rt_a17_info info;
    unsigned char value = 0;
    struct rt_copy_memory req = {
        .pid = 42, .addr = 0x10000, .buffer = (uintptr_t)&value, .size = 1,
    };
    assert(rt_a17_init() == 0);
    assert(rt_a17_fops.open(NULL, &file) == 0);
    assert(file.private_data == NULL && alloc_refs == 0);
    assert(rt_a17_ioctl(&file, RT_A17_INFO, (uintptr_t)&info) == 0);
    assert(info.abi == 2 && info.capabilities == 7 &&
           info.original_ioctls_implemented == 14 && info.reserved == 0);
    for (unsigned int op = RT_OP_INIT_KEY; op <= RT_OP_BP_CONTROL; op++)
        if (!(info.original_ioctls_implemented & (1U << (op - RT_OP_INIT_KEY))))
            assert(rt_a17_ioctl(&file, op, 0) == -EOPNOTSUPP);
    assert(rt_a17_ioctl(&file, RT_A17_BP_STATUS, 0) == -EOPNOTSUPP);
    nr_vmas = 1;
    test_vmas[0] = (struct vm_area_struct){0x10000, 0x11000, VM_READ | VM_WRITE, NULL};
    test_pages[0].bytes[0] = 0x5a;
    assert(rt_a17_ioctl(&file, RT_OP_READ_MEM, (uintptr_t)&req) == 0 && value == 0x5a);
    value = 0x73;
    assert(rt_a17_ioctl(&file, RT_OP_WRITE_MEM, (uintptr_t)&req) == 0);
    assert(test_pages[0].bytes[0] == 0x73 && dirty_pages == 1);
    deny_mm = 1;
    assert(rt_a17_ioctl(&file, RT_OP_WRITE_MEM, (uintptr_t)&req) == -EACCES);
    admin = 0;
    assert(rt_a17_ioctl(&file, RT_A17_INFO, (uintptr_t)&info) == -EPERM);
    assert(rt_a17_ioctl(&file, RT_OP_BP_CONTROL, 0) == -EPERM);
    assert(rt_a17_fops.release(NULL, &file) == 0);
    assert(!alloc_refs && !mm_refs && !pid_refs && !task_refs && !pin_refs && !locks);
    rt_a17_exit();
    puts("Memory-only INFO, unsupported breakpoints, memory and policy contracts passed.");
    return 0;
}
