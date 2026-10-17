/* SPDX-License-Identifier: GPL-2.0-only */
/* Host fault injection only; these are not hardware or device tests. */
#ifndef RT_TEST_BP_KERNEL_SHIM_H
#define RT_TEST_BP_KERNEL_SHIM_H
#include "kernel_shim.h"
#define u64 uint64_t
#define U64_MAX UINT64_MAX
#define static_assert _Static_assert
#define ATOMIC_INIT(n) (n)
typedef int atomic_t;
static int atomic_inc_return(atomic_t *n) { return ++*n; }
static void atomic_dec(atomic_t *n) { --*n; }
struct cred { int id; };
struct rw_semaphore { int held; };
struct signal_struct { struct rw_semaphore exec_update_lock; };
struct pid_namespace { int id; };
struct mutex { int held; };
typedef struct { int held; } raw_spinlock_t;
struct user_pt_regs { uint64_t regs[31], sp, pc, pstate; };
struct pt_regs { union { struct user_pt_regs user_regs; struct { uint64_t x[31], sp, pc, pstate; }; }; int el1, compat; };
struct perf_sample_data { int unused; };
struct perf_event_attr {
    unsigned long bp_addr;
    unsigned int bp_len, bp_type, exclude_kernel, exclude_hv, inherit;
    unsigned int remove_on_exec, disabled, sample_period, pinned;
};
struct perf_event;
typedef void (*perf_overflow_handler_t)(struct perf_event *, struct perf_sample_data *, struct pt_regs *);
struct perf_event {
    struct perf_event_attr attr;
    void *overflow_handler_context;
    perf_overflow_handler_t handler;
};
#define HW_BREAKPOINT_X 4U
#define HW_BREAKPOINT_W 2U
#define HW_BREAKPOINT_R 1U
static struct cred caller_cred = {1}, target_cred = {2}, changed_cred = {3};
static struct signal_struct target_signal;
static struct task_struct caller_task = {.mm=&test_mm, .cred=&caller_cred};
static struct mm_struct changed_mm;
static struct pid_namespace original_ns = {1}, changed_ns = {2};
static struct pid_namespace *caller_ns = &original_ns;
static int cred_refs, mm_count_refs, perf_refs, namespace_refs, mutex_refs, exec_refs;
static int register_error, modify_error, exec_error, mutex_error, mm_race, cred_race;
static int cred_calls, modify_calls;
static uint64_t fake_time;
static void bp_test_fixture_init(void)
{
    test_task.mm=&test_mm;test_task.signal=&target_signal;test_task.cred=&target_cred;
    changed_ns.id=2;
}
#define current (&caller_task)
static const struct cred *current_cred(void) { return current->cred; }
static const struct cred *get_task_cred(struct task_struct *task)
{
    assert(task == &test_task); cred_refs++; cred_calls++;
    return cred_race && cred_calls % 2 == 0 ? &changed_cred : &target_cred;
}
static void put_cred(const struct cred *cred) { assert(cred); cred_refs--; }
static struct mm_struct *get_task_mm(struct task_struct *task)
{ assert(task == &test_task); mm_refs++; return mm_race ? &changed_mm : &test_mm; }
static void mmgrab(struct mm_struct *mm) { assert(mm == &test_mm); mm_count_refs++; }
static void mmdrop(struct mm_struct *mm) { assert(mm == &test_mm); mm_count_refs--; }
static int down_read_killable(struct rw_semaphore *sem)
{
    assert(!sem->held); if (exec_error) return -EINTR;
    sem->held=1; exec_refs++; return 0;
}
static void up_read(struct rw_semaphore *sem) { assert(sem->held); sem->held=0; exec_refs--; }
#define task_thread_info(task) (task)
static int is_compat_thread(struct task_struct *task) { return task->compat; }
static struct pid_namespace *task_active_pid_ns(struct task_struct *task)
{ assert(task==current); return caller_ns; }
static struct pid_namespace *get_pid_ns(struct pid_namespace *ns) { namespace_refs++; return ns; }
static void put_pid_ns(struct pid_namespace *ns) { assert(ns==&original_ns); namespace_refs--; }
static void *kzalloc(size_t n, int flags)
{ void *p=kmalloc(n,flags); if(p)memset(p,0,n); return p; }
static void mutex_init(struct mutex *m) { m->held=0; }
static int mutex_lock_interruptible(struct mutex *m)
{
    assert(!irq_mask && !raw_locks && !m->held);
    if(mutex_error)return -EINTR; m->held=1;mutex_refs++;return 0;
}
static void mutex_unlock(struct mutex *m) { assert(m->held);m->held=0;mutex_refs--; }
static void raw_spin_lock_init(raw_spinlock_t *l) { l->held=0; }
#define local_irq_save(flags) do { (flags)=irq_mask; irq_mask=1; } while(0)
#define local_irq_restore(flags) do { irq_mask=(flags); } while(0)
#define raw_spin_lock_irqsave(l, flags) do { assert(!(l)->held);local_irq_save(flags);(l)->held=1;raw_locks++; } while(0)
#define raw_spin_unlock_irqrestore(l, flags) do { assert((l)->held);(l)->held=0;raw_locks--;local_irq_restore(flags); } while(0)
static void hw_breakpoint_init(struct perf_event_attr *attr)
{ memset(attr,0,sizeof(*attr));attr->sample_period=1;attr->pinned=1; }
static struct perf_event *register_user_hw_breakpoint(struct perf_event_attr *attr,
    perf_overflow_handler_t handler, void *context, struct task_struct *task)
{
    assert(!irq_mask && !raw_locks && exec_refs==1 && locks==1 && task==&test_task);
    assert(attr->exclude_kernel && attr->exclude_hv && !attr->inherit && attr->remove_on_exec);
    assert(attr->sample_period==1 && attr->pinned);
    if(register_error)return ERR_PTR(register_error);
    struct perf_event *e=malloc(sizeof(*e));assert(e);perf_refs++;
    e->attr=*attr;e->overflow_handler_context=context;e->handler=handler;return e;
}
static void unregister_hw_breakpoint(struct perf_event *e)
{ assert(!irq_mask && !raw_locks);if(e){perf_refs--;free(e);} }
static int modify_user_hw_breakpoint(struct perf_event *e, struct perf_event_attr *a)
{
    assert(irq_mask && !raw_locks && a->disabled);
    assert(e->attr.bp_addr==a->bp_addr && e->attr.bp_type==a->bp_type && e->attr.bp_len==a->bp_len);
    modify_calls++;if(modify_error)return modify_error;e->attr=*a;return 0;
}
static bool user_mode(const struct pt_regs *r) { return !r->el1; }
static bool compat_user_mode(const struct pt_regs *r) { return r->compat; }
static uint64_t instruction_pointer(const struct pt_regs *r) { return r->pc; }
static uint64_t ktime_get_ns(void) { return ++fake_time; }
static void memzero_explicit(void *p,size_t n) { memset(p,0,n); }
#endif
