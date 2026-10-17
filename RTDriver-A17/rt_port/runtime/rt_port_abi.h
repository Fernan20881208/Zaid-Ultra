/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef RT_A17_PORT_ABI_H
#define RT_A17_PORT_ABI_H

#include "../rt_abi.h"

/* ABI 1 remains diagnostic only. ABI 2 is a partial, unverified RT port.
 * An implemented bit describes code, never evidence of a device test. */
#define RT_A17_PORT_ABI_VERSION 2U
#define RT_A17_CAP_READ (1U << 0)
#define RT_A17_CAP_WRITE (1U << 1)
#define RT_A17_CAP_MODULE_BASE (1U << 2)
#define RT_A17_PORT_CAPS (RT_A17_CAP_READ | RT_A17_CAP_WRITE | RT_A17_CAP_MODULE_BASE)
/* Bit n corresponds to original opcode (0x800 + n). */
#define RT_A17_PORT_OPS ((1U << 1) | (1U << 2) | (1U << 3))
#define RT_A17_MAX_TRANSFER 65536U
#define RT_A17_MAX_NAME 255U

#endif
