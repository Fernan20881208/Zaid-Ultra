#!/usr/bin/env bash
# Wrapped real status/client syscalls, without opening any device.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/syscalls.c" <<'C'
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "runtime/rt_port_abi.h"
static int opened;
int __wrap_open(const char *path, int flags, ...)
{
    int s = atoi(getenv("SCENARIO"));
    (void)flags;
    assert(!strcmp(path, opened ? "/dev/rt_a17_diag" : "/dev/rt_a17"));
    opened++;
    if (s == 0 || ((s == 2 || s == 10) && opened == 1)) {
        errno = s == 10 ? ENODEV : ENOENT; return -1;
    }
    if (s == 1) { assert(opened == 1); errno = EACCES; return -1; }
    return 42;
}
int __wrap_close(int fd) { assert(fd == 42); return 0; }
int __wrap_ioctl(int fd, unsigned long op, ...)
{
    int s = atoi(getenv("SCENARIO"));
    assert(fd == 42 && op == RT_A17_INFO);
    if (s == 8 || s == 9) { errno = s == 8 ? ENOTTY : EPERM; return -1; }
    va_list ap;
    va_start(ap, op);
    struct rt_a17_info *info = va_arg(ap, struct rt_a17_info *);
    va_end(ap);
    int diagnostic = s == 2 || s == 10;
    info->abi = s == 4 ? 99 : diagnostic ? RT_A17_ABI_VERSION :
                (s == 12 || s == 13) ? RT_A17_PORT_MEMORY_ABI_VERSION : RT_A17_PORT_ABI_VERSION;
    info->capabilities = diagnostic ? 0 : s == 5 ? 0x80 : s == 12 ? RT_A17_MEMORY_CAPS :
                         s == 13 ? (RT_A17_MEMORY_CAPS | 0x80) : RT_A17_PORT_CAPS;
    info->original_ioctls_implemented = diagnostic ? 0 : s == 6 ? 0 :
                                       s == 12 ? RT_A17_MEMORY_OPS :
                                       s == 13 ? RT_A17_MEMORY_OPS : RT_A17_PORT_OPS;
    info->reserved = s == 7 ? 1 : 0;
    return s == 11 ? 1 : 0;
}
C
cc -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror -I"$ROOT" \
  "$ROOT/tools/rt_a17_port_status.c" "$ROOT/tools/rt_a17_client.c" "$TMP/syscalls.c" \
  -Wl,--wrap=open,--wrap=ioctl,--wrap=close -o "$TMP/status"
for scenario in {0..13}; do
  case "$scenario" in
    0) expected=2; state=absent;;
    1|9) expected=5; state=inaccessible;;
    2|10) expected=4; state=diagnostic;;
    3|12) expected=6; state=partial_unverified;;
    *) expected=3; state=compatibility_error;;
  esac
  rc=0
  SCENARIO="$scenario" "$TMP/status" > "$TMP/result.json" || rc=$?
  [[ "$rc" == "$expected" ]]
  python3 - "$TMP/result.json" "$state" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
assert data['state'] == sys.argv[2], data
assert data['functional_verified'] is False, data
PY
done
echo 'Port status: 14 capability/error/fallback scenarios passed; no verified-state fabrication.'
