#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/include/linux/sched" "$TMP/include/asm"
# Only test compilation uses these API mocks. Production includes unchanged
# Samsung kernel headers and is compiled independently by Kleaf.
for h in atomic build_bug capability cred dcache err fs highmem hw_breakpoint irqflags ktime limits miscdevice mm module mutex overflow perf_event pid pid_namespace ptrace sched/signal slab spinlock string uaccess sched/mm sched/task; do
  printf '#include "%s"\n' "$ROOT/RTDriver-A17/rt_port/tests/kernel_shim.h" > "$TMP/include/linux/$h.h"
done
for h in compat ptrace cacheflush; do
  printf '#include "%s"\n' "$ROOT/RTDriver-A17/rt_port/tests/bp_kernel_shim.h" > "$TMP/include/asm/$h.h"
done
for test in test_runtime test_breakpoints; do
  cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-misleading-indentation -I"$TMP/include" \
    "$ROOT/RTDriver-A17/rt_port/tests/$test.c" -o "$TMP/$test"
  "$TMP/$test"
done
for tool in rt_a17_port_status rt_a17_selftest rt_a17_bp_selftest; do
  cc -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror \
    "$ROOT/RTDriver-A17/rt_port/tools/$tool.c" \
    "$ROOT/RTDriver-A17/rt_port/tools/rt_a17_client.c" -o "$TMP/$tool"
done
echo 'Runtime C and userspace tools passed host compilation; ARM64/device execution still required.'
