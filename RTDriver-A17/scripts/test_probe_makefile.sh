#!/usr/bin/env bash
# Offline regression for the specific error in GitHub Actions #39020223729:
#   make: *** No targets. Stop.
# No real kernel compile; the fake KERNEL_SRC records requested goals.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
mkdir -p "$TEMP/module" "$TEMP/kernel"
cp "$REPO/RTDriver-A17/src/Makefile" "$TEMP/module/Makefile"
cat > "$TEMP/kernel/Makefile" <<'EOF'
.PHONY: modules modules_install clean
modules modules_install clean:
	@printf "%s %s %s\n" "$@" "$(M)" "$(O)" >> "$(KERNEL_A17_TEST_OUT)"
EOF
export KERNEL_A17_TEST_OUT="$TEMP/calls.txt"
for goal in default modules_install clean; do
  if [[ "$goal" == default ]]; then
    make --no-print-directory -s -C "$TEMP/module" \
      KERNEL_SRC="$TEMP/kernel" M=/mock/samsung/module O=/mock/out
  else
    make --no-print-directory -s -C "$TEMP/module" \
      KERNEL_SRC="$TEMP/kernel" M=/mock/samsung/module O=/mock/out "$goal"
  fi
done
printf '%s\n' \
  "modules /mock/samsung/module /mock/out" \
  "modules_install /mock/samsung/module /mock/out" \
  "clean /mock/samsung/module /mock/out" > "$TEMP/expected.txt"
diff -u "$TEMP/expected.txt" "$TEMP/calls.txt"
if make --no-print-directory -s -C "$TEMP/module" M=/tmp O=/tmp >"$TEMP/bad.log" 2>&1; then
  echo "ERROR: module Makefile did not require KERNEL_SRC" >&2
  exit 1
fi
echo "A17 Kleaf Makefile offline goal tests PASSED (no compilation)."
