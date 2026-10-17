// SPDX-License-Identifier: GPL-2.0-only
/*
 * Partial RT ABI port for Samsung A17, Linux 6.12.38.
 * Recovered layouts/opcodes only; independently written implementation.
 * No physical memory access, symbol lookup tricks or fixed kernel offsets.
 * Device execution and compatibility with the original app are unverified.
 */
#include <linux/capability.h>
#include <linux/dcache.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/limits.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/pid.h>
#include <linux/ptrace.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include "rt_port_abi.h"
#include "rt_breakpoints.h"

/* Pin the authorized mm, rather than checking one mm then accessing a new
 * one after exec. mm_access preserves normal ptrace, credentials and LSM
 * policy. CAP_SYS_ADMIN is an additional restriction, not an authorization
 * substitute. PIDs are resolved in the caller's PID namespace.
 */
static struct mm_struct *rt_access_mm(pid_t nr)
{
	struct task_struct *task;
	struct pid *pid;
	struct mm_struct *mm;

	if (nr <= 0)
		return ERR_PTR(-EINVAL);
	pid = find_get_pid(nr);
	if (!pid)
		return ERR_PTR(-ESRCH);
	task = get_pid_task(pid, PIDTYPE_PID);
	put_pid(pid);
	if (!task)
		return ERR_PTR(-ESRCH);
	mm = mm_access(task, PTRACE_MODE_ATTACH_REALCREDS);
	put_task_struct(task);
	return mm ? mm : ERR_PTR(-ESRCH);
}

/* Called with mmap_lock held. Only ordinary user mappings are supported.
 * Code writes are deliberately excluded: this data-memory port does not
 * promise instruction-cache synchronization or executable-page patching.
 */
static int rt_check_vmas(struct mm_struct *mm, unsigned long addr,
			unsigned long end, bool write)
{
	struct vm_area_struct *vma;

	while (addr < end) {
		vma = find_vma(mm, addr);
		if (!vma || vma->vm_start > addr)
			return -EFAULT;
		if (vma->vm_flags & (VM_IO | VM_PFNMAP))
			return -EOPNOTSUPP;
		if (write && (vma->vm_flags & VM_EXEC))
			return -EOPNOTSUPP;
		if (!(vma->vm_flags & (write ? VM_WRITE : VM_READ)))
			return -EACCES;
		addr = min(vma->vm_end, end);
	}
	return 0;
}

static long rt_copy_memory(void __user *arg, bool write)
{
	struct page *pages[DIV_ROUND_UP(RT_A17_MAX_TRANSFER, PAGE_SIZE) + 1];
	struct rt_copy_memory req;
	struct mm_struct *mm;
	unsigned long end, local_end, count, offset, remaining, done = 0;
	unsigned char *buffer;
	long pinned, ret;
	unsigned int i;

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;
	if (req.pid <= 0 || !req.size || req.size > RT_A17_MAX_TRANSFER)
		return -EINVAL;
	/* Native AArch64 ABI only. Tagged pointers are rejected, not silently
	 * truncated. Recovered C padding is ignored for original-client ABI.
	 */
	if (!req.addr || !req.buffer || (req.buffer >> 56) ||
	    check_add_overflow((unsigned long)req.addr, (unsigned long)req.size, &end) ||
	    check_add_overflow((unsigned long)req.buffer, (unsigned long)req.size, &local_end) ||
	    !access_ok(u64_to_user_ptr(req.buffer), req.size))
		return -EFAULT;
	buffer = kmalloc(req.size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	/* Stage the whole source before changing any target page. No user-copy
	 * faults while holding the target mmap_lock, including self-access.
	 */
	if (write && copy_from_user(buffer, u64_to_user_ptr(req.buffer), req.size)) {
		ret = -EFAULT;
		goto free_buffer;
	}
	mm = rt_access_mm(req.pid);
	if (IS_ERR(mm)) {
		ret = PTR_ERR(mm);
		goto free_buffer;
	}
	ret = mmap_read_lock_killable(mm);
	if (ret)
		goto put_mm;
	if (end > mm->task_size) {
		ret = -EFAULT;
		goto unlock;
	}
	ret = rt_check_vmas(mm, req.addr, end, write);
	if (ret)
		goto unlock;
	offset = offset_in_page(req.addr);
	count = DIV_ROUND_UP(offset + req.size, PAGE_SIZE);
	/* NULL locked keeps mmap_lock held; page faults cannot drop it, so the
	 * VMA permission and non-executable checks remain valid during copy.
	 * FOLL_FORCE is never used. Pin the entire bounded request before writes.
	 */
	pinned = pin_user_pages_remote(mm, req.addr & PAGE_MASK, count,
				       write ? FOLL_WRITE : 0, pages, NULL);
	if (pinned != (long)count) {
		if (pinned > 0)
			unpin_user_pages(pages, pinned);
		ret = pinned < 0 ? pinned : -EFAULT;
		goto unlock;
	}
	remaining = req.size;
	for (i = 0; i < count; i++) {
		unsigned long bytes = min(remaining, PAGE_SIZE - offset);
		unsigned char *mapped = kmap_local_page(pages[i]);

		if (write) {
			memcpy(mapped + offset, buffer + done, bytes);
			flush_dcache_page(pages[i]);
		} else {
			memcpy(buffer + done, mapped + offset, bytes);
		}
		kunmap_local(mapped);
		done += bytes;
		remaining -= bytes;
		offset = 0;
	}
	unpin_user_pages_dirty_lock(pages, count, write);
	ret = 0;
unlock:
	mmap_read_unlock(mm);
put_mm:
	mmput(mm);
	if (!ret && !write &&
	    copy_to_user(u64_to_user_ptr(req.buffer), buffer, req.size))
		ret = -EFAULT;
free_buffer:
	kfree_sensitive(buffer);
	/* Original successful READ/WRITE ioctls return 0, not a byte count. */
	return ret;
}

static long rt_module_base(void __user *arg)
{
	struct rt_module_base req;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	char name[RT_A17_MAX_NAME + 1], *pathbuf, *path, *leaf;
	unsigned long addr = 0;
	long copied, ret;

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;
	if (req.pid <= 0)
		return -EINVAL;
	if (!req.name)
		return -EFAULT;
	copied = strncpy_from_user(name, u64_to_user_ptr(req.name), sizeof(name));
	if (copied < 0)
		return copied;
	if (!copied)
		return -EINVAL;
	if (copied >= (long)sizeof(name))
		return -ENAMETOOLONG;
	if (strchr(name, '/'))
		return -EINVAL;
	pathbuf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!pathbuf)
		return -ENOMEM;
	mm = rt_access_mm(req.pid);
	if (IS_ERR(mm)) {
		ret = PTR_ERR(mm);
		goto free_path;
	}
	ret = mmap_read_lock_killable(mm);
	if (ret)
		goto put_mm;
	req.base = 0;
	/* Lowest mapping start with exactly this file basename, as observed in
	 * original dispatch_ioctl. This is not a general ELF load-bias query.
	 */
	while ((vma = find_vma(mm, addr))) {
		if (vma->vm_file) {
			path = d_path(&vma->vm_file->f_path, pathbuf, PATH_MAX);
			if (IS_ERR(path)) {
				ret = PTR_ERR(path);
				goto unlock;
			}
			leaf = strrchr(path, '/');
			if (!strcmp(leaf ? leaf + 1 : path, name)) {
				req.base = vma->vm_start;
				break;
			}
		}
		if (vma->vm_end == ULONG_MAX)
			break;
		addr = vma->vm_end;
	}
	ret = 0;
unlock:
	mmap_read_unlock(mm);
put_mm:
	mmput(mm);
	/* A missing mapping is success with base=0, matching the recovered ABI. */
	if (!ret && copy_to_user(arg, &req, sizeof(req)))
		ret = -EFAULT;
free_path:
	kfree(pathbuf);
	return ret;
}

static int rt_a17_open(struct inode *inode, struct file *file)
{
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	ret = nonseekable_open(inode, file);
	return ret ? ret : rt_bp_open(file);
}

static int rt_a17_release(struct inode *inode, struct file *file)
{
	return rt_bp_release(file);
}

static long rt_a17_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	const struct rt_a17_info info = {
		.abi = RT_A17_PORT_ABI_VERSION,
		.capabilities = RT_A17_PORT_CAPS,
		.original_ioctls_implemented = RT_A17_PORT_OPS,
		.reserved = 0,
	};
	void __user *ptr = (void __user *)arg;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	switch (cmd) {
	case RT_A17_INFO:
		return copy_to_user(ptr, &info, sizeof(info)) ? -EFAULT : 0;
	case RT_OP_READ_MEM:
		return rt_copy_memory(ptr, false);
	case RT_OP_WRITE_MEM:
		return rt_copy_memory(ptr, true);
	case RT_OP_MODULE_BASE:
		return rt_module_base(ptr);
	case RT_OP_BP_GET_HITS:
	case RT_OP_BP_CONTROL:
	case RT_A17_BP_STATUS:
		return rt_bp_ioctl(file, cmd, ptr);
	case RT_OP_INIT_KEY:
	case RT_OP_GYRO_CONFIG:
		return -EOPNOTSUPP;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations rt_a17_fops = {
	.owner = THIS_MODULE,
	.open = rt_a17_open,
	.release = rt_a17_release,
	.unlocked_ioctl = rt_a17_ioctl,
};

static struct miscdevice rt_a17_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "rt_a17",
	.fops = &rt_a17_fops,
	.mode = 0600,
};

static int __init rt_a17_init(void)
{
	return misc_register(&rt_a17_device);
}

static void __exit rt_a17_exit(void)
{
	misc_deregister(&rt_a17_device);
}
module_init(rt_a17_init);
module_exit(rt_a17_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung A17 partial RT ABI port; device validation pending");
MODULE_VERSION("0.2");
