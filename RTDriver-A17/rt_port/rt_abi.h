/* SPDX-License-Identifier: GPL-2.0-only */
/* DWARF-extracted interface definitions from original ARM64 RT .ko.
 * Data layout only; no execution or memory modification. */
#ifndef ZAID_A17_RT_ABI_H
#define ZAID_A17_RT_ABI_H
#include <linux/types.h>
enum rt_original_op {
 RT_OP_INIT_KEY = 0x800,
 RT_OP_READ_MEM = 0x801,
 RT_OP_WRITE_MEM = 0x802,
 RT_OP_MODULE_BASE = 0x803,
 RT_OP_GYRO_CONFIG = 0x804,
 RT_OP_BP_GET_HITS = 0x805,
 RT_OP_BP_CONTROL = 0x806,
};
struct rt_copy_memory { __s32 pid; __u32 pad; __u64 addr; __u64 buffer; __u64 size; };
struct rt_module_base { __s32 pid; __u32 pad; __u64 name; __u64 base; };
struct rt_breakpoint_ctl { __s32 pid; __u32 pad; __u64 addr; __s32 action; __s32 type; __s32 len; __u32 pad2; };
struct rt_breakpoint_hits { __u64 buffer; __u64 count; };
#define RT_A17_INFO 0x900
struct rt_a17_info { __u32 abi; __u32 capabilities; __u32 original_ioctls_implemented; __u32 reserved; };
#endif
