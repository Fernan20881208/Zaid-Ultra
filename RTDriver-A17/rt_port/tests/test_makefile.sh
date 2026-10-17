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
for mod in rt_port rt_port/runtime; do
  make --no-print-directory -s -C "$BASE/RTDriver-A17/$mod" KERNEL_SRC="$TMP/kernel" M=/test/module O=/test/out
  make --no-print-directory -s -C "$BASE/RTDriver-A17/$mod" KERNEL_SRC="$TMP/kernel" M=/test/module O=/test/out modules_install
done
printf 'modules\nmodules_install\nmodules\nmodules_install\n' > "$TMP/expected"
diff -u "$TMP/expected" "$TMP/goals"
echo "A17 diagnostic and runtime Kbuild delegation validated without compiling."
