/* SPDX-License-Identifier: GPL-2.0-only */
/* Host-only fault-injection harness. Not kernel ABI/device evidence. */
#ifndef RT_TEST_KERNEL_SHIM_H
#define RT_TEST_KERNEL_SHIM_H
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <linux/types.h>
#define __user
#define __init
#define __exit
#define PAGE_SIZE 4096UL
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define PATH_MAX 4096
#define GFP_KERNEL 0
#define CAP_SYS_ADMIN 21
#define PIDTYPE_PID 0
#define PTRACE_MODE_ATTACH_REALCREDS 0x12
#define FOLL_WRITE 1U
#define VM_READ 1UL
#define VM_WRITE 2UL
#define VM_EXEC 4UL
#define VM_IO 8UL
#define VM_PFNMAP 16UL
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define offset_in_page(n) ((n) & (PAGE_SIZE - 1))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define check_add_overflow(a, b, out) __builtin_add_overflow(a, b, out)
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define u64_to_user_ptr(n) ((void *)(uintptr_t)(n))
#define THIS_MODULE NULL
#define MISC_DYNAMIC_MINOR 255
#define MODULE_LICENSE(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_VERSION(x)
#define module_init(fn)
#define module_exit(fn)

struct inode { int unused; };
struct file { struct { const char *name; } f_path; };
struct file_operations {
    void *owner;
    int (*open)(struct inode *, struct file *);
    long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
};
struct miscdevice {
    int minor;
    const char *name;
    const struct file_operations *fops;
    unsigned int mode;
};
struct mm_struct { unsigned long task_size; };
struct pid { int unused; };
struct task_struct { int unused; };
struct page { unsigned char bytes[PAGE_SIZE]; };
struct vm_area_struct {
    unsigned long vm_start, vm_end, vm_flags;
    struct file *vm_file;
};
static struct mm_struct test_mm = {.task_size = 1UL << 48};
static struct pid test_pid;
static struct task_struct test_task;
static struct page test_pages[20];
static struct vm_area_struct test_vmas[4];
static unsigned int nr_vmas;
static int admin = 1, deny_mm, no_mm, lock_error, alloc_error;
static long forced_pin = -999;
static int alloc_refs, mm_refs, pid_refs, task_refs, pin_refs, locks, dirty_pages;
static const void *bad_user;

static bool access_ok(const void *p, size_t n)
{
    return (uintptr_t)p >= 4096 && n <= (1ULL << 48) &&
           (uintptr_t)p <= (1ULL << 48) - n;
}
static size_t copy_from_user(void *dst, const void *src, size_t n)
{
    if (src == bad_user || !access_ok(src, n)) return n;
    memcpy(dst, src, n); return 0;
}
static size_t copy_to_user(void *dst, const void *src, size_t n)
{
    if (dst == bad_user || !access_ok(dst, n)) return n;
    memcpy(dst, src, n); return 0;
}
static long strncpy_from_user(char *dst, const char *src, long n)
{
    if (src == bad_user || !access_ok(src, n)) return -EFAULT;
    for (long i = 0; i < n; i++) {
        dst[i] = src[i]; if (!src[i]) return i;
    }
    return n;
}
static void *kmalloc(size_t n, int flags)
{
    (void)flags; if (alloc_error) return NULL;
    void *p = malloc(n); if (p) alloc_refs++; return p;
}
static void kfree(void *p) { if (p) alloc_refs--; free(p); }
static void kfree_sensitive(void *p) { kfree(p); }
static bool capable(int cap) { assert(cap == CAP_SYS_ADMIN); return admin; }
static struct pid *find_get_pid(pid_t nr)
{
    if (nr != 42) return NULL; pid_refs++; return &test_pid;
}
static struct task_struct *get_pid_task(struct pid *pid, int type)
{
    assert(pid == &test_pid && type == PIDTYPE_PID); task_refs++; return &test_task;
}
static void put_pid(struct pid *pid) { assert(pid == &test_pid); pid_refs--; }
static void put_task_struct(struct task_struct *task)
{ assert(task == &test_task); task_refs--; }
static struct mm_struct *mm_access(struct task_struct *task, unsigned int mode)
{
    assert(task == &test_task && mode == PTRACE_MODE_ATTACH_REALCREDS);
    if (deny_mm) return ERR_PTR(-EACCES);
    if (no_mm) return NULL;
    mm_refs++; return &test_mm;
}
static void mmput(struct mm_struct *mm) { assert(mm == &test_mm); mm_refs--; }
static int mmap_read_lock_killable(struct mm_struct *mm)
{
    assert(mm == &test_mm); if (lock_error) return -EINTR; locks++; return 0;
}
static void mmap_read_unlock(struct mm_struct *mm)
{ assert(mm == &test_mm && locks == 1); locks--; }
static struct vm_area_struct *find_vma(struct mm_struct *mm, unsigned long addr)
{
    assert(mm == &test_mm && locks == 1);
    for (unsigned int i = 0; i < nr_vmas; i++)
        if (test_vmas[i].vm_end > addr) return &test_vmas[i];
    return NULL;
}
static long pin_user_pages_remote(struct mm_struct *mm, unsigned long addr,
    unsigned long count, unsigned int flags, struct page **pages, int *locked)
{
    assert(mm == &test_mm && locks == 1 && locked == NULL);
    assert(flags == 0 || flags == FOLL_WRITE);
    long result = forced_pin == -999 ? (long)count : forced_pin;
    if (result <= 0) return result;
    unsigned long index = (addr - 0x10000) / PAGE_SIZE;
    assert(index + result <= 20);
    for (long i = 0; i < result; i++) pages[i] = &test_pages[index + i];
    pin_refs += result; return result;
}
static void unpin_user_pages(struct page **p, unsigned long n)
{ (void)p; pin_refs -= n; }
static void unpin_user_pages_dirty_lock(struct page **p, unsigned long n, bool dirty)
{ if (dirty) dirty_pages += n; unpin_user_pages(p, n); }
static void *kmap_local_page(struct page *p) { return p->bytes; }
static void kunmap_local(const void *p) { (void)p; }
static void flush_dcache_page(struct page *p) { (void)p; }
static char *d_path(const void *path, char *buf, int n)
{
    const struct file *file = path; /* f_path is the first and only member. */
    size_t len = strlen(file->f_path.name);
    if (len >= (size_t)n) return ERR_PTR(-ENAMETOOLONG);
    memcpy(buf, file->f_path.name, len + 1); return buf;
}
static int nonseekable_open(struct inode *i, struct file *f)
{ (void)i; (void)f; return 0; }
static int misc_register(struct miscdevice *d) { assert(d->mode == 0600); return 0; }
static void misc_deregister(struct miscdevice *d) { (void)d; }
#endif
