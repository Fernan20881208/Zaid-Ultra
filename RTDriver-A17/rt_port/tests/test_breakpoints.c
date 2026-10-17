/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual production breakpoint C, with kernel API fault injection only. */
#include <stdio.h>
#include "bp_kernel_shim.h"
#include "../runtime/rt_breakpoints.c"
#include "../runtime/rt_main.c"

static struct rt_breakpoint_ctl ctl = {
    .pid=42, .addr=0x10004, .action=RT_BP_ADD, .type=RT_BP_WRITE, .len=4,
    .pad=0xdeadbeef, .pad2=0xdeadbeef,
};
static struct file open_file(void)
{
    struct file file={.f_cred=&caller_cred};
    assert(rt_a17_fops.open(NULL,&file)==0);return file;
}
static long invoke(struct file *f,unsigned int op,void *arg)
{ return rt_a17_fops.unlocked_ioctl(f,op,(unsigned long)arg); }
static long control(struct file *f,int action,uint64_t addr)
{ struct rt_breakpoint_ctl r=ctl;r.action=action;r.addr=addr;return invoke(f,RT_OP_BP_CONTROL,&r); }
static struct rt_a17_bp_status status(struct file *f)
{
    struct rt_a17_bp_status s;memset(&s,0xff,sizeof(s));
    assert(invoke(f,RT_A17_BP_STATUS,&s)==0);
    assert(s.abi==3 && s.max_breakpoints==4 && s.max_hits==64 && s.reserved==0);
    return s;
}
static void balanced(int sessions,int events)
{
    assert(alloc_refs==sessions && perf_refs==events && cred_refs==events && mm_count_refs==events);
    assert(namespace_refs==sessions && rt_bp_total==events);
    assert(!mm_refs && !pid_refs && !task_refs && !pin_refs && !locks);
    assert(!irq_mask && !raw_locks && !exec_refs && !mutex_refs);
}
static void close_file(struct file *f)
{ assert(rt_a17_fops.release(NULL,f)==0 && !f->private_data); }
static bool fire(struct file *file,uint64_t pc)
{
    struct rt_bp_session *s=file->private_data;struct perf_event *event=NULL;
    for(unsigned int i=0;i<4;i++)if(s->entries[i].event){event=s->entries[i].event;break;}
    assert(event);if(event->attr.disabled)return false;
    struct pt_regs regs={.sp=0x20000,.pc=pc,.pstate=0};
    regs.x[0]=0x1122334455667788ULL;regs.x[30]=0x9988776655443322ULL;
    struct task_struct saved=caller_task;
    caller_task.mm=&test_mm;caller_task.cred=&target_cred;
    event->handler(event,NULL,&regs);caller_task=saved;return true;
}
static struct file *interleave_file;
static void interleave_hit(void) { assert(fire(interleave_file,0xfeed0010)); }

int main(void)
{
    bp_test_fixture_init();assert(rt_a17_init()==0);
    nr_vmas=1;test_vmas[0]=(struct vm_area_struct){0x10000,0x12000,VM_READ|VM_WRITE|VM_EXEC,NULL};
    struct file a=open_file(),b=open_file();
    assert(control(&a,RT_BP_ADD,ctl.addr)==0);balanced(2,1);
    assert(control(&a,RT_BP_ADD,ctl.addr)==-EEXIST);balanced(2,1);
    assert(control(&b,RT_BP_REMOVE,ctl.addr)==-ENOENT);
    assert(status(&a).registrations==1 && status(&b).registrations==0);
    assert(fire(&a,0x12345678));assert(!fire(&a,0x12345678));
    assert(modify_calls==1 && status(&a).queued==1 && status(&b).queued==0);
    struct rt_breakpoint_hit hit[64];
    struct rt_breakpoint_hits read={.buffer=(uintptr_t)hit,.count=1};
    bad_write_user=hit;
    assert(invoke(&a,RT_OP_BP_GET_HITS,&read)==-EFAULT && status(&a).queued==1);
    bad_write_user=&read;
    assert(invoke(&a,RT_OP_BP_GET_HITS,&read)==-EFAULT && status(&a).queued==1);
    bad_write_user=NULL;read.count=0;read.buffer=0;
    assert(invoke(&a,RT_OP_BP_GET_HITS,&read)==0 && read.count==0 && status(&a).queued==1);
    read.count=1;read.buffer=(uintptr_t)&read;
    assert(invoke(&a,RT_OP_BP_GET_HITS,&read)==-EINVAL && status(&a).queued==1);
    read.buffer=UINT64_MAX-10;
    assert(invoke(&a,RT_OP_BP_GET_HITS,&read)==-EFAULT && status(&a).queued==1);
    read.buffer=(uintptr_t)hit;alloc_error=1;
    assert(invoke(&a,RT_OP_BP_GET_HITS,&read)==-ENOMEM && status(&a).queued==1);alloc_error=0;
    assert(invoke(&a,RT_OP_BP_GET_HITS,&read)==0 && read.count==1);
    assert(hit[0].pid==42 && hit[0].pad==0 && hit[0].addr==0x12345678);
    assert(hit[0].timestamp==1 && hit[0].regs.pc==hit[0].addr && hit[0].regs.sp==0x20000);
    assert(hit[0].regs.x[0]==0x1122334455667788ULL && hit[0].regs.x[30]==0x9988776655443322ULL);
    assert(status(&a).queued==0 && status(&a).callback_errors==0);
    assert(control(&a,RT_BP_REMOVE,ctl.addr)==0);balanced(2,0);
    /* Queue wraps without overwriting old records; one-shot rearm is explicit. */
    for(unsigned int i=0;i<65;i++) {
        assert(control(&a,RT_BP_ADD,ctl.addr)==0);assert(fire(&a,0x1000+i*4));
        assert(control(&a,RT_BP_REMOVE,ctl.addr)==0);
    }
    assert(status(&a).queued==64 && status(&a).dropped==1);
    read=(struct rt_breakpoint_hits){.buffer=(uintptr_t)hit,.count=1};
    assert(invoke(&a,RT_OP_BP_GET_HITS,&read)==0 && read.count==1 && hit[0].addr==0x1000);
    assert(status(&a).queued==63);
    /* Producer arriving during user copy must remain queued after snapshot commit. */
    assert(control(&a,RT_BP_ADD,ctl.addr)==0);interleave_file=&a;copy_hook=interleave_hit;
    read.count=UINT64_MAX;
    assert(invoke(&a,RT_OP_BP_GET_HITS,&read)==0 && read.count==63);
    assert(hit[0].addr==0x1004 && hit[62].addr==0x10fc && status(&a).queued==1);
    read.count=64;assert(invoke(&a,RT_OP_BP_GET_HITS,&read)==0 && read.count==1 && hit[0].addr==0xfeed0010);
    assert(control(&a,RT_BP_REMOVE,ctl.addr)==0);balanced(2,0);
    ptrace_allowed=0;assert(control(&a,RT_BP_ADD,ctl.addr)==-EPERM);ptrace_allowed=1;
    admin=0;assert(control(&a,RT_BP_ADD,ctl.addr)==-EPERM);admin=1;
    caller_task.cred=&changed_cred;assert(invoke(&a,RT_OP_BP_GET_HITS,&read)==-EPERM);caller_task.cred=&caller_cred;
    caller_ns=&changed_ns;assert(control(&a,RT_BP_ADD,ctl.addr)==-EPERM);caller_ns=&original_ns;
    mutex_error=1;assert(control(&a,RT_BP_ADD,ctl.addr)==-EINTR);mutex_error=0;
    deny_mm=1;assert(control(&a,RT_BP_ADD,ctl.addr)==-EACCES);deny_mm=0;
    no_mm=1;assert(control(&a,RT_BP_ADD,ctl.addr)==-ESRCH);no_mm=0;
    exec_error=1;assert(control(&a,RT_BP_ADD,ctl.addr)==-EINTR);exec_error=0;
    lock_error=1;assert(control(&a,RT_BP_ADD,ctl.addr)==-EINTR);lock_error=0;
    mm_race=1;assert(control(&a,RT_BP_ADD,ctl.addr)==-EAGAIN);mm_race=0;
    cred_calls=0;cred_race=1;assert(control(&a,RT_BP_ADD,ctl.addr)==-EAGAIN);cred_race=0;
    test_task.compat=1;assert(control(&a,RT_BP_ADD,ctl.addr)==-EOPNOTSUPP);test_task.compat=0;
    register_error=-ENOSPC;assert(control(&a,RT_BP_ADD,ctl.addr)==-ENOSPC);register_error=0;
    balanced(2,0);
    assert(control(&a,999,ctl.addr)==-EINVAL);
    ctl.pid=43;assert(control(&a,RT_BP_ADD,ctl.addr)==-ESRCH);ctl.pid=42;
    ctl.len=3;assert(control(&a,RT_BP_ADD,ctl.addr)==-EINVAL);ctl.len=4;
    assert(control(&a,RT_BP_ADD,ctl.addr+1)==-EINVAL);
    assert(control(&a,RT_BP_ADD,1ULL<<55)==-EFAULT);
    test_vmas[0].vm_flags=VM_IO|VM_READ;
    assert(control(&a,RT_BP_ADD,ctl.addr)==-EOPNOTSUPP);
    ctl.type=RT_BP_EXECUTE;test_vmas[0].vm_flags=VM_READ;
    assert(control(&a,RT_BP_ADD,ctl.addr)==-EACCES);
    test_vmas[0].vm_flags|=VM_EXEC;
    assert(control(&a,RT_BP_ADD,ctl.addr)==0);assert(fire(&a,0x12345000));
    assert(control(&a,RT_BP_REMOVE,ctl.addr)==0);ctl.type=RT_BP_RW;
    /* Changed credentials and mm must not disclose registers; event still disarms. */
    assert(control(&a,RT_BP_ADD,ctl.addr)==0);
    struct rt_bp_session *session=a.private_data;
    struct perf_event *e=session->entries[0].event;
    struct pt_regs regs={.pc=0x77770000};
    unsigned int queued=status(&a).queued;
    e->handler(e,NULL,&regs); /* controller creds do not match target */
    assert(e->attr.disabled && status(&a).queued==queued);
    assert(control(&a,RT_BP_REMOVE,ctl.addr)==0);
    assert(control(&a,RT_BP_ADD,ctl.addr)==0);
    e=session->entries[0].event;modify_error=-EINVAL;
    e->handler(e,NULL,&regs);modify_error=0;
    assert(status(&a).callback_errors==1 && status(&a).queued==queued);
    close_file(&a);close_file(&b);balanced(0,0);
    /* Closing a descriptor must unregister all its owned events. */
    struct file files[9];
    ctl.type=RT_BP_WRITE;test_vmas[0].vm_flags=VM_READ|VM_WRITE;
    for(int i=0;i<9;i++) {
        files[i]=open_file();
        for(unsigned int j=0;j<4;j++)
            assert(control(&files[i],RT_BP_ADD,0x10000+j*4)==(i<8?0:-ENOSPC));
    }
    assert(control(&files[0],RT_BP_ADD,0x10020)==-ENOSPC);balanced(9,32);
    for(int i=0;i<9;i++)close_file(&files[i]);balanced(0,0);
    alloc_error=1;struct file failed={.f_cred=&caller_cred};
    assert(rt_a17_fops.open(NULL,&failed)==-ENOMEM);alloc_error=0;balanced(0,0);
    rt_a17_exit();
    puts("Production BP tests passed: one-shot disable, ownership, FIFO, faults, races, quotas and close cleanup (host mocks).");
}
