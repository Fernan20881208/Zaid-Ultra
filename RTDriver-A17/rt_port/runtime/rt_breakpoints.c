// SPDX-License-Identifier: GPL-2.0-only
/* Bounded, one-shot user breakpoints. No register modification or private APIs. */
#include <linux/atomic.h>
#include <linux/build_bug.h>
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/hw_breakpoint.h>
#include <linux/irqflags.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/perf_event.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/ptrace.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <asm/compat.h>
#include <asm/ptrace.h>
#include "rt_port_abi.h"
#include "rt_breakpoints.h"

struct rt_bp_session;
struct rt_bp_entry {
	struct perf_event *event;
	struct rt_bp_session *session;
	struct mm_struct *mm;
	const struct cred *cred;
	pid_t pid;
	u64 addr;
};

struct rt_bp_session {
	struct mutex mutex; /* Control/read/close; never acquired by the callback. */
	raw_spinlock_t lock;
	struct pid_namespace *pid_ns;
	struct rt_bp_entry entries[RT_A17_MAX_BREAKPOINTS];
	struct rt_breakpoint_hit hits[RT_A17_MAX_HITS];
	unsigned int head, count, callback_errors;
	u64 dropped;
};

static atomic_t rt_bp_total = ATOMIC_INIT(0);
static_assert(sizeof(struct rt_user_regs) == sizeof(struct user_pt_regs));
static_assert(sizeof(struct rt_breakpoint_hit) == 296);

static void rt_bp_hit(struct perf_event *event, struct perf_sample_data *data,
		      struct pt_regs *regs)
{
	struct rt_bp_entry *entry = event->overflow_handler_context;
	struct rt_bp_session *session = entry->session;
	struct perf_event_attr attr = event->attr;
	struct rt_breakpoint_hit *hit;
	unsigned long flags;
	int ret;

	/* ARM64 only single-steps its default perf handler automatically.
	 * Disarm first, otherwise a custom execute callback can trap forever.
	 * This API explicitly supports IRQs-off use for current's
	 * event. Keeping type/address/length unchanged avoids slot allocation.
	 */
	attr.disabled = 1;
	local_irq_save(flags);
	ret = modify_user_hw_breakpoint(event, &attr);
	local_irq_restore(flags);
	if (ret) {
		raw_spin_lock_irqsave(&session->lock, flags);
		session->callback_errors++;
		raw_spin_unlock_irqrestore(&session->lock, flags);
		return;
	}
	/* Never disclose registers from a new mm/credential identity or EL1.
	 * Held mm_count/cred references prevent pointer reuse (ABA).
	 */
	if (!regs || !user_mode(regs) || compat_user_mode(regs) ||
	    current->mm != entry->mm || current_cred() != entry->cred)
		return;
	raw_spin_lock_irqsave(&session->lock, flags);
	if (session->count == RT_A17_MAX_HITS) {
		if (session->dropped != U64_MAX)
			session->dropped++;
	} else {
		hit = &session->hits[(session->head + session->count) % RT_A17_MAX_HITS];
		hit->pid = entry->pid; /* TID in the registering descriptor's namespace. */
		hit->pad = 0;
		hit->timestamp = ktime_get_ns();
		hit->addr = instruction_pointer(regs);
		memcpy(&hit->regs, &regs->user_regs, sizeof(hit->regs));
		session->count++;
	}
	raw_spin_unlock_irqrestore(&session->lock, flags);
}

static void rt_bp_drop(struct rt_bp_entry *entry)
{
	/* unregister synchronously detaches the event from its CPU/context
	 * before the callback's referenced storage is released or reused.
	 */
	unregister_hw_breakpoint(entry->event);
	entry->event = NULL;
	put_cred(entry->cred);
	mmdrop(entry->mm);
	memset(entry, 0, sizeof(*entry));
	atomic_dec(&rt_bp_total);
}

static int rt_bp_add(struct rt_bp_session *session,
		     const struct rt_breakpoint_ctl *req)
{
	struct perf_event_attr attr;
	struct rt_bp_entry *entry = NULL;
	struct task_struct *task;
	struct pid *pid;
	struct mm_struct *mm, *check_mm;
	const struct cred *cred, *check_cred;
	struct vm_area_struct *vma;
	struct perf_event *event;
	unsigned long end;
	unsigned int type, i;
	int ret;

	if (req->pid <= 0 || !req->addr)
		return -EINVAL;
	switch (req->type) {
	case RT_BP_EXECUTE:
		type = HW_BREAKPOINT_X;
		if (req->len != 4)
			return -EINVAL;
		break;
	case RT_BP_WRITE:
		type = HW_BREAKPOINT_W;
		break;
	case RT_BP_RW:
		type = HW_BREAKPOINT_R | HW_BREAKPOINT_W;
		break;
	default:
		return -EINVAL;
	}
	if ((req->len != 1 && req->len != 2 && req->len != 4 && req->len != 8) ||
	    (req->addr & (req->len - 1)) ||
	    check_add_overflow((unsigned long)req->addr, (unsigned long)req->len, &end))
		return -EINVAL;
	for (i = 0; i < RT_A17_MAX_BREAKPOINTS; i++) {
		if (session->entries[i].event && session->entries[i].pid == req->pid &&
		    session->entries[i].addr == req->addr)
			return -EEXIST;
		if (!session->entries[i].event && !entry)
			entry = &session->entries[i];
	}
	if (!entry)
		return -ENOSPC;
	pid = find_get_pid(req->pid);
	if (!pid)
		return -ESRCH;
	task = get_pid_task(pid, PIDTYPE_PID);
	put_pid(pid);
	if (!task)
		return -ESRCH;
	cred = get_task_cred(task);
	mm = mm_access(task, PTRACE_MODE_ATTACH_REALCREDS);
	if (IS_ERR_OR_NULL(mm)) {
		ret = mm ? PTR_ERR(mm) : -ESRCH;
		goto put_task;
	}
	ret = down_read_killable(&task->signal->exec_update_lock);
	if (ret)
		goto put_mm;
	/* mm_access has its own exec lock. Acquire this lock afterwards and
	 * reject a changed mm/cred instead of nesting the same rwsem or
	 * attaching to a newly privileged exec after authorization.
	 */
	check_mm = get_task_mm(task);
	check_cred = get_task_cred(task);
	ret = check_mm == mm && check_cred == cred ? 0 : -EAGAIN;
	if (check_mm)
		mmput(check_mm);
	put_cred(check_cred);
	if (ret)
		goto unlock_exec;
	if (is_compat_thread(task_thread_info(task))) {
		ret = -EOPNOTSUPP;
		goto unlock_exec;
	}
	ret = mmap_read_lock_killable(mm);
	if (ret)
		goto unlock_exec;
	vma = find_vma(mm, req->addr);
	if (end > mm->task_size || !vma || vma->vm_start > req->addr || end > vma->vm_end)
		ret = -EFAULT;
	else if (vma->vm_flags & (VM_IO | VM_PFNMAP))
		ret = -EOPNOTSUPP;
	else if (!(vma->vm_flags & (req->type == RT_BP_EXECUTE ? VM_EXEC : VM_READ)))
		ret = -EACCES;
	else
		ret = 0;
	/* perf's documented order is exec_update_lock -> ctx->mutex ->
	 * mmap_lock. Registration takes ctx->mutex, so do not enter it while
	 * holding the VMA-validation lock. This validates the mapping at this
	 * point; the target can remap it later, including after registration.
	 * The authorized mm/cred references and exec lock remain held.
	 */
	mmap_read_unlock(mm);
	if (ret)
		goto unlock_exec;
	if (atomic_inc_return(&rt_bp_total) > (int)RT_A17_MAX_BREAKPOINTS_GLOBAL) {
		atomic_dec(&rt_bp_total);
		ret = -ENOSPC;
		goto unlock_exec;
	}
	hw_breakpoint_init(&attr);
	attr.bp_addr = req->addr;
	attr.bp_len = req->len;
	attr.bp_type = type;
	attr.exclude_kernel = 1;
	attr.exclude_hv = 1;
	attr.inherit = 0;
	attr.remove_on_exec = 1;
	entry->session = session;
	entry->pid = req->pid;
	entry->addr = req->addr;
	entry->cred = cred;
	entry->mm = mm;
	mmgrab(mm); /* Only mm_count: do not keep exited tasks' pages alive. */
	event = register_user_hw_breakpoint(&attr, rt_bp_hit, entry, task);
	if (IS_ERR_OR_NULL(event)) {
		ret = event ? PTR_ERR(event) : -EOPNOTSUPP;
		mmdrop(mm);
		memset(entry, 0, sizeof(*entry));
		atomic_dec(&rt_bp_total);
	} else {
		entry->event = event;
		cred = NULL; /* Entry owns the reference until unregister. */
		ret = 0;
	}
unlock_exec:
	up_read(&task->signal->exec_update_lock);
put_mm:
	mmput(mm);
put_task:
	if (cred)
		put_cred(cred);
	put_task_struct(task);
	return ret;
}

static long rt_bp_control(struct rt_bp_session *session, void __user *arg)
{
	struct rt_breakpoint_ctl req;
	unsigned int i;

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;
	if (req.action == RT_BP_ADD)
		return rt_bp_add(session, &req);
	if (req.action != RT_BP_REMOVE || req.pid <= 0)
		return -EINVAL;
	/* Removal only affects this descriptor's event, even after target exit.
	 * Type/length and recovered ABI padding are ignored for REMOVE.
	 */
	for (i = 0; i < RT_A17_MAX_BREAKPOINTS; i++) {
		struct rt_bp_entry *entry = &session->entries[i];

		if (entry->event && entry->pid == req.pid && entry->addr == req.addr) {
			rt_bp_drop(entry);
			return 0;
		}
	}
	return -ENOENT;
}

static long rt_bp_get_hits(struct rt_bp_session *session, void __user *arg)
{
	struct rt_breakpoint_hits req;
	struct rt_breakpoint_hit *snapshot = NULL;
	unsigned long flags, bytes, end, arg_end;
	unsigned int n, i;
	long ret = -EFAULT;

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;
	raw_spin_lock_irqsave(&session->lock, flags);
	n = min_t(u64, req.count, session->count);
	raw_spin_unlock_irqrestore(&session->lock, flags);
	bytes = n * sizeof(*snapshot); /* n <= 64, independent of input capacity. */
	if (n) {
		if (!req.buffer || (req.buffer >> 56) ||
		    check_add_overflow((unsigned long)req.buffer, bytes, &end) ||
		    check_add_overflow((unsigned long)arg, sizeof(req), &arg_end) ||
		    !access_ok(u64_to_user_ptr(req.buffer), bytes))
			return -EFAULT;
		if (req.buffer < arg_end && (unsigned long)arg < end)
			return -EINVAL;
		snapshot = kmalloc(bytes, GFP_KERNEL);
		if (!snapshot)
			return -ENOMEM;
		raw_spin_lock_irqsave(&session->lock, flags);
		for (i = 0; i < n; i++)
			snapshot[i] = session->hits[(session->head + i) % RT_A17_MAX_HITS];
		raw_spin_unlock_irqrestore(&session->lock, flags);
		if (copy_to_user(u64_to_user_ptr(req.buffer), snapshot, bytes))
			goto out;
	}
	req.count = n;
	if (copy_to_user(arg, &req, sizeof(req)))
		goto out;
	/* The consumer mutex prevents another drain; producers drop NEW hits
	 * on overflow, keeping the snapshot valid until both user copies work.
	 * A zero capacity query leaves all records queued.
	 */
	raw_spin_lock_irqsave(&session->lock, flags);
	for (i = 0; i < n; i++)
		memzero_explicit(&session->hits[(session->head + i) % RT_A17_MAX_HITS],
				 sizeof(*snapshot));
	session->head = (session->head + n) % RT_A17_MAX_HITS;
	session->count -= n;
	raw_spin_unlock_irqrestore(&session->lock, flags);
	ret = 0;
out:
	kfree_sensitive(snapshot);
	return ret;
}

static long rt_bp_status(struct rt_bp_session *session, void __user *arg)
{
	struct rt_a17_bp_status status = {
		.abi = RT_A17_PORT_ABI_VERSION,
		.max_breakpoints = RT_A17_MAX_BREAKPOINTS,
		.max_hits = RT_A17_MAX_HITS,
	};
	unsigned long flags;
	unsigned int i;

	for (i = 0; i < RT_A17_MAX_BREAKPOINTS; i++)
		status.registrations += !!session->entries[i].event;
	raw_spin_lock_irqsave(&session->lock, flags);
	status.queued = session->count;
	status.dropped = session->dropped;
	status.callback_errors = session->callback_errors;
	raw_spin_unlock_irqrestore(&session->lock, flags);
	return copy_to_user(arg, &status, sizeof(status)) ? -EFAULT : 0;
}

int rt_bp_open(struct file *file)
{
	struct rt_bp_session *session = kzalloc(sizeof(*session), GFP_KERNEL);

	if (!session)
		return -ENOMEM;
	mutex_init(&session->mutex);
	raw_spin_lock_init(&session->lock);
	session->pid_ns = get_pid_ns(task_active_pid_ns(current));
	file->private_data = session;
	return 0;
}

int rt_bp_release(struct file *file)
{
	struct rt_bp_session *session = file->private_data;
	unsigned int i;

	if (!session)
		return 0;
	for (i = 0; i < RT_A17_MAX_BREAKPOINTS; i++)
		if (session->entries[i].event)
			rt_bp_drop(&session->entries[i]);
	put_pid_ns(session->pid_ns);
	kfree_sensitive(session);
	file->private_data = NULL;
	return 0;
}

long rt_bp_ioctl(struct file *file, unsigned int cmd, void __user *arg)
{
	struct rt_bp_session *session = file->private_data;
	long ret;

	if (!capable(CAP_SYS_PTRACE) || current_cred() != file->f_cred ||
	    !session || task_active_pid_ns(current) != session->pid_ns)
		return -EPERM;
	ret = mutex_lock_interruptible(&session->mutex);
	if (ret)
		return ret;
	switch (cmd) {
	case RT_OP_BP_CONTROL:
		ret = rt_bp_control(session, arg);
		break;
	case RT_OP_BP_GET_HITS:
		ret = rt_bp_get_hits(session, arg);
		break;
	case RT_A17_BP_STATUS:
		ret = rt_bp_status(session, arg);
		break;
	default:
		ret = -ENOTTY;
	}
	mutex_unlock(&session->mutex);
	return ret;
}
