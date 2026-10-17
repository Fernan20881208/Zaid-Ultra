/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef RT_A17_CLIENT_H
#define RT_A17_CLIENT_H
#include <stddef.h>
#include <stdint.h>
#include "../runtime/rt_port_abi.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0 or negative errno. These calls never load a module. */
int rt_a17_query(int fd, struct rt_a17_info *info);
int rt_a17_read(int fd, int32_t pid, uint64_t address, void *buffer, size_t size);
int rt_a17_write(int fd, int32_t pid, uint64_t address, const void *buffer, size_t size);
int rt_a17_module_base(int fd, int32_t pid, const char *name, uint64_t *base);
int rt_a17_bp_control(int fd, const struct rt_breakpoint_ctl *control);
int rt_a17_bp_get_hits(int fd, struct rt_breakpoint_hit *hits,
                       size_t capacity, size_t *count);
int rt_a17_bp_status(int fd, struct rt_a17_bp_status *status);
#ifdef __cplusplus
}
#endif
#endif
