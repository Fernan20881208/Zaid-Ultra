/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef RT_A17_CLIENT_H
#define RT_A17_CLIENT_H
#include <stddef.h>
#include <stdint.h>
#include "../runtime/rt_port_abi.h"

/* Returns 0 or negative errno. These calls never load a module. */
int rt_a17_query(int fd, struct rt_a17_info *info);
int rt_a17_read(int fd, int32_t pid, uint64_t address, void *buffer, size_t size);
int rt_a17_write(int fd, int32_t pid, uint64_t address, const void *buffer, size_t size);
int rt_a17_module_base(int fd, int32_t pid, const char *name, uint64_t *base);
#endif
