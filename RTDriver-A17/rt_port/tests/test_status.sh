#!/usr/bin/env bash
# Exercise the actual client with wrapped syscalls; never access a device.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/syscalls.c" <<'C'
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include "rt_abi.h"
int __wrap_open(const char *path, int flags, ...)
{
    int scenario = atoi(getenv("SCENARIO"));
    (void)path; (void)flags;
    if (scenario < 2) { errno = scenario ? EACCES : ENOENT; return -1; }
    return 42;
}
int __wrap_close(int fd) { (void)fd; return 0; }
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    int scenario = atoi(getenv("SCENARIO"));
    va_list args;
    struct rt_a17_info *info;
    (void)fd;
    if (request != RT_A17_INFO) abort();
    if (scenario == 7) { errno = ENOTTY; return -1; }
    if (scenario == 8) { errno = EPERM; return -1; }
    va_start(args, request);
    info = va_arg(args, struct rt_a17_info *);
    va_end(args);
    info->abi = scenario == 3 ? 99 : RT_A17_ABI_VERSION;
    info->capabilities = scenario == 4 ? 1 : 0;
    info->original_ioctls_implemented = scenario == 5 ? 7 : 0;
    info->reserved = scenario == 6 ? 1 : 0;
    return 0;
}
C
cc -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror -I"$ROOT" \
  "$ROOT/tools/rt_a17_status.c" "$TMP/syscalls.c" \
  -Wl,--wrap=open,--wrap=ioctl,--wrap=close -o "$TMP/status"
for scenario in 0 1 2 3 4 5 6 7 8; do
  case "$scenario" in
    0) expected=2; state=absent;;
    1|8) expected=5; state=inaccessible;;
    2) expected=4; state=diagnostic;;
    *) expected=3; state=compatibility_error;;
  esac
  rc=0
  SCENARIO="$scenario" "$TMP/status" > "$TMP/result.json" 2>"$TMP/stderr" || rc=$?
  [[ "$rc" == "$expected" ]]
  python3 - "$TMP/result.json" "$state" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
assert data['state'] == sys.argv[2], data
assert data['functional_verified'] is False, data
PY
done
echo 'Actual status client: absent, denied, diagnostic and incompatible responses PASSED.'
