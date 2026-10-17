#!/usr/bin/env bash
set -euo pipefail
BASE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/kernel"
cat > "$TMP/kernel/Makefile" <<'EOF'
.PHONY: modules modules_install clean
modules modules_install clean:
	@printf '%s\n' '$@' >> "$(RESULTS)"
EOF
export RESULTS="$TMP/goals"
make --no-print-directory -s -C "$BASE/RTDriver-A17/rt_port" KERNEL_SRC="$TMP/kernel" M=/test/module O=/test/out
make --no-print-directory -s -C "$BASE/RTDriver-A17/rt_port" KERNEL_SRC="$TMP/kernel" M=/test/module O=/test/out modules_install
printf 'modules\nmodules_install\n' > "$TMP/expected"
diff -u "$TMP/expected" "$TMP/goals"
echo "New A17 diagnostic Kbuild rules validated without compiling."
