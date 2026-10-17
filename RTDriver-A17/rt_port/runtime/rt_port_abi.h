/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef RT_A17_PORT_ABI_H
#define RT_A17_PORT_ABI_H

#include "../rt_abi.h"

/* ABI 1 remains diagnostic only. ABI 2 has memory operations; ABI 3 adds
 * explicitly one-shot hardware breakpoints and a per-descriptor hit queue.
 * An implemented bit describes code, never evidence of a device test.
 */
#define RT_A17_PORT_MEMORY_ABI_VERSION 2U
#define RT_A17_PORT_ABI_VERSION 3U
#define RT_A17_CAP_READ (1U << 0)
#define RT_A17_CAP_WRITE (1U << 1)
#define RT_A17_CAP_MODULE_BASE (1U << 2)
#define RT_A17_CAP_BP_CONTROL (1U << 3)
#define RT_A17_CAP_BP_HITS (1U << 4)
#define RT_A17_CAP_BP_ONESHOT (1U << 5)
#define RT_A17_MEMORY_CAPS (RT_A17_CAP_READ | RT_A17_CAP_WRITE | RT_A17_CAP_MODULE_BASE)
#define RT_A17_PORT_CAPS (RT_A17_MEMORY_CAPS | RT_A17_CAP_BP_CONTROL | \
			RT_A17_CAP_BP_HITS | RT_A17_CAP_BP_ONESHOT)
/* Bit n corresponds to original opcode (0x800 + n). */
#define RT_A17_MEMORY_OPS ((1U << 1) | (1U << 2) | (1U << 3))
#define RT_A17_PORT_OPS (RT_A17_MEMORY_OPS | (1U << 5) | (1U << 6))
#define RT_A17_MAX_TRANSFER 65536U
#define RT_A17_MAX_NAME 255U

/* Recovered original enum values and user_pt_regs layout, not kernel pt_regs. */
enum rt_bp_action { RT_BP_ADD = 1, RT_BP_REMOVE = 2 };
enum rt_bp_type { RT_BP_EXECUTE = 0, RT_BP_WRITE = 1, RT_BP_RW = 2 };
struct rt_user_regs { __u64 x[31]; __u64 sp, pc, pstate; };
struct rt_breakpoint_hit {
	__s32 pid;
	__u32 pad;
	__u64 timestamp; /* Monotonic nanoseconds, matching original ktime_get. */
	__u64 addr;      /* Instruction PC, not the watched data address. */
	struct rt_user_regs regs;
};

#define RT_A17_MAX_BREAKPOINTS 4U
#define RT_A17_MAX_BREAKPOINTS_GLOBAL 32U
#define RT_A17_MAX_HITS 64U
#define RT_A17_BP_STATUS 0x901
struct rt_a17_bp_status {
	__u32 abi, max_breakpoints, max_hits, registrations, queued, callback_errors;
	__u64 dropped;
	__u64 reserved;
};

#endif
