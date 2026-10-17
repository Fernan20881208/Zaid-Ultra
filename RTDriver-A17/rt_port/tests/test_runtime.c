/* SPDX-License-Identifier: GPL-2.0-only */
/* Executes the production ioctl/backend C with controlled kernel API mocks.
 * Does not emulate an ARM64 kernel or prove runtime/device compatibility. */
#include <stdio.h>
#include "../runtime/rt_a17.c"

static void reset(void)
{
    assert(!alloc_refs && !mm_refs && !pid_refs && !task_refs && !pin_refs && !locks);
    admin = 1; deny_mm = no_mm = lock_error = alloc_error = dirty_pages = 0;
    forced_pin = -999; bad_user = NULL;
    nr_vmas = 1;
    test_vmas[0] = (struct vm_area_struct){0x10000, 0x12000, VM_READ | VM_WRITE, NULL};
    memset(test_pages[0].bytes, 0x5a, PAGE_SIZE);
    memset(test_pages[1].bytes, 0xa5, PAGE_SIZE);
}

static long call(unsigned int op, void *arg)
{
    long ret = rt_a17_fops.unlocked_ioctl(NULL, op, (unsigned long)arg);
    assert(!alloc_refs && !mm_refs && !pid_refs && !task_refs && !pin_refs && !locks);
    return ret;
}

int main(void)
{
    unsigned char buffer[64];
    struct rt_a17_info info;
    struct rt_copy_memory req = {
        .pid = 42, .pad = 0xdeadbeef, .addr = 0x10ff0,
        .buffer = (uintptr_t)buffer, .size = 64,
    };
    assert(rt_a17_init() == 0);
    reset();
    assert(rt_a17_fops.open(NULL, NULL) == 0);
    assert(call(RT_A17_INFO, &info) == 0);
    assert(info.abi == 2 && info.capabilities == 7 &&
           info.original_ioctls_implemented == 14 && info.reserved == 0);
    admin = 0;
    assert(rt_a17_fops.open(NULL, NULL) == -EPERM);
    assert(call(RT_OP_READ_MEM, &req) == -EPERM);
    assert(call(RT_A17_INFO, &info) == -EPERM);
    reset();
    for (unsigned int op = RT_OP_INIT_KEY; op <= RT_OP_BP_CONTROL; op++)
        if (!(RT_A17_PORT_OPS & (1U << (op - RT_OP_INIT_KEY))))
            assert(call(op, NULL) == -EOPNOTSUPP);
    assert(call(0xbeef, NULL) == -ENOTTY);
    assert(call(RT_OP_READ_MEM, &req) == 0);
    for (unsigned int i = 0; i < sizeof(buffer); i++)
        assert(buffer[i] == (i < 16 ? 0x5a : 0xa5));
    memset(buffer, 0x19, sizeof(buffer));
    assert(call(RT_OP_WRITE_MEM, &req) == 0);
    assert(dirty_pages == 2);
    assert(test_pages[0].bytes[PAGE_SIZE - 1] == 0x19);
    assert(test_pages[1].bytes[47] == 0x19 && test_pages[1].bytes[48] == 0xa5);
    reset(); forced_pin = 1;
    assert(call(RT_OP_WRITE_MEM, &req) == -EFAULT);
    assert(test_pages[0].bytes[PAGE_SIZE - 1] == 0x5a && dirty_pages == 0);
    forced_pin = -ENOMEM;
    assert(call(RT_OP_READ_MEM, &req) == -ENOMEM);
    reset(); deny_mm = 1;
    assert(call(RT_OP_WRITE_MEM, &req) == -EACCES);
    reset(); no_mm = 1;
    assert(call(RT_OP_READ_MEM, &req) == -ESRCH);
    reset(); lock_error = 1;
    assert(call(RT_OP_READ_MEM, &req) == -EINTR);
    reset(); alloc_error = 1;
    assert(call(RT_OP_READ_MEM, &req) == -ENOMEM);
    reset(); bad_user = &req;
    assert(call(RT_OP_READ_MEM, &req) == -EFAULT);
    reset(); bad_user = buffer;
    assert(call(RT_OP_WRITE_MEM, &req) == -EFAULT);
    assert(dirty_pages == 0);
    assert(call(RT_OP_READ_MEM, &req) == -EFAULT);
    reset();
    req.pid = 0; assert(call(RT_OP_READ_MEM, &req) == -EINVAL);
    req.pid = 43; assert(call(RT_OP_READ_MEM, &req) == -ESRCH);
    req.pid = 42; req.size = 0; assert(call(RT_OP_READ_MEM, &req) == -EINVAL);
    req.size = RT_A17_MAX_TRANSFER + 1; assert(call(RT_OP_READ_MEM, &req) == -EINVAL);
    req.size = 64; req.addr = UINT64_MAX - 20; assert(call(RT_OP_READ_MEM, &req) == -EFAULT);
    req.addr = 1UL << 55; assert(call(RT_OP_READ_MEM, &req) == -EFAULT);
    req.addr = 0x10000; req.buffer = UINT64_MAX - 20;
    assert(call(RT_OP_WRITE_MEM, &req) == -EFAULT);
    req.buffer = (uintptr_t)buffer | (1UL << 56);
    assert(call(RT_OP_READ_MEM, &req) == -EFAULT);
    req.buffer = (uintptr_t)buffer;
    req.addr = 0x11ff0; assert(call(RT_OP_WRITE_MEM, &req) == -EFAULT);
    req.addr = 0x10ff0;
    nr_vmas = 2;
    test_vmas[0].vm_end = 0x11000;
    test_vmas[1] = (struct vm_area_struct){0x11000, 0x12000, VM_READ, NULL};
    assert(call(RT_OP_WRITE_MEM, &req) == -EACCES);
    assert(test_pages[0].bytes[PAGE_SIZE - 1] == 0x5a);
    test_vmas[1].vm_start = 0x11001;
    assert(call(RT_OP_READ_MEM, &req) == -EFAULT);
    reset(); test_vmas[0].vm_flags |= VM_EXEC;
    assert(call(RT_OP_WRITE_MEM, &req) == -EOPNOTSUPP);
    assert(call(RT_OP_READ_MEM, &req) == 0);
    test_vmas[0].vm_flags |= VM_IO;
    assert(call(RT_OP_READ_MEM, &req) == -EOPNOTSUPP);
    reset();
    struct file mapped = {.f_path.name = "/data/local/tmp/example.so"};
    test_vmas[0].vm_file = &mapped;
    struct rt_module_base module = {.pid = 42, .name = (uintptr_t)"example.so"};
    assert(call(RT_OP_MODULE_BASE, &module) == 0 && module.base == 0x10000);
    module.name = (uintptr_t)"absent.so"; module.base = UINT64_MAX;
    assert(call(RT_OP_MODULE_BASE, &module) == 0 && module.base == 0);
    module.name = (uintptr_t)""; assert(call(RT_OP_MODULE_BASE, &module) == -EINVAL);
    module.name = (uintptr_t)"/example.so"; assert(call(RT_OP_MODULE_BASE, &module) == -EINVAL);
    char longname[RT_A17_MAX_NAME + 2]; memset(longname, 'X', sizeof(longname));
    module.name = (uintptr_t)longname;
    assert(call(RT_OP_MODULE_BASE, &module) == -ENAMETOOLONG);
    module.name = 1; assert(call(RT_OP_MODULE_BASE, &module) == -EFAULT);
    module.name = (uintptr_t)"example.so"; deny_mm = 1;
    assert(call(RT_OP_MODULE_BASE, &module) == -EACCES);
    rt_a17_exit();
    puts("Production RT ioctl/backend host fault-injection tests passed (not device tests).");
    return 0;
}
