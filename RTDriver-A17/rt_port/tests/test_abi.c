/* Verify recovered original DWARF data sizes and offsets. */
#include <stddef.h>
#include "../rt_abi.h"
_Static_assert(sizeof(struct rt_copy_memory) == 32, "COPY_MEMORY 32");
_Static_assert(offsetof(struct rt_copy_memory, pid) == 0, "COPY_MEMORY.pid");
_Static_assert(offsetof(struct rt_copy_memory, addr) == 8, "COPY_MEMORY.addr");
_Static_assert(offsetof(struct rt_copy_memory, buffer) == 16, "COPY_MEMORY.buffer");
_Static_assert(offsetof(struct rt_copy_memory, size) == 24, "COPY_MEMORY.size");
_Static_assert(sizeof(struct rt_module_base) == 24, "MODULE_BASE 24");
_Static_assert(offsetof(struct rt_module_base, name) == 8, "MODULE_BASE.name");
_Static_assert(offsetof(struct rt_module_base, base) == 16, "MODULE_BASE.base");
_Static_assert(sizeof(struct rt_breakpoint_ctl) == 32, "HW_BREAKPOINT_CTL 32");
_Static_assert(offsetof(struct rt_breakpoint_ctl, action) == 16, "HW_BREAKPOINT_CTL.action");
_Static_assert(offsetof(struct rt_breakpoint_ctl, type) == 20, "HW_BREAKPOINT_CTL.type");
_Static_assert(offsetof(struct rt_breakpoint_ctl, len) == 24, "HW_BREAKPOINT_CTL.len");
_Static_assert(sizeof(struct rt_breakpoint_hits) == 16, "HW_BREAKPOINT_GET_HITS_CTL 16");
_Static_assert(RT_OP_INIT_KEY == 0x800, "init key opcode");
_Static_assert(RT_OP_BP_CONTROL == 0x806, "bp opcode");
int main(void) { return 0; }
