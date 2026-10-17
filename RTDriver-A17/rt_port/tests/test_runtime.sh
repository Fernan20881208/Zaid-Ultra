#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/include/linux/sched" "$TMP/include/asm"
# Only test compilation uses these API mocks. Production includes unchanged
# Samsung kernel headers and is compiled independently by Kleaf.
for h in capability dcache err fs highmem limits miscdevice mm module overflow pid ptrace slab string uaccess sched/mm sched/task; do
  printf '#include "%s"\n' "$ROOT/RTDriver-A17/rt_port/tests/kernel_shim.h" > "$TMP/include/linux/$h.h"
done
printf '#include "%s"\n' "$ROOT/RTDriver-A17/rt_port/tests/kernel_shim.h" > "$TMP/include/asm/cacheflush.h"
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
  -Wno-misleading-indentation -I"$TMP/include" \
  "$ROOT/RTDriver-A17/rt_port/tests/test_runtime.c" -o "$TMP/runtime"
"$TMP/runtime"
for tool in rt_a17_port_status rt_a17_selftest; do
  cc -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror \
    "$ROOT/RTDriver-A17/rt_port/tools/$tool.c" \
    "$ROOT/RTDriver-A17/rt_port/tools/rt_a17_client.c" -o "$TMP/$tool"
done
echo 'Runtime C and userspace tools passed host compilation; ARM64/device execution still required.'
