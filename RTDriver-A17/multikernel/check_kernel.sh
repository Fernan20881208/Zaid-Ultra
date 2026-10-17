#!/system/bin/sh
# Read-only profile check. No insmod, reboot, property or policy changes.
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BUNDLE_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)"
release="$(uname -r)"
arch="$(uname -m)"
printf 'kernel=%s\narchitecture=%s\n' "$release" "$arch"
case "$release" in 6.12.[0-9]*) ;; *) echo 'state=outside_6.12_family'; exit 2;; esac
case "$arch" in aarch64|arm64) ;; *) echo 'state=unsupported_architecture'; exit 3;; esac
matched=0
for target in "$BUNDLE_ROOT"/targets/*; do
  [ -d "$target" ] || continue
  [ -f "$target/kernel.release" ] && [ -f "$target/architecture" ] || continue
  [ "$(cat "$target/kernel.release")" = "$release" ] || continue
  [ "$(cat "$target/architecture")" = aarch64 ] || continue
  matched=1
  if ! (cd "$target" && sha256sum -c SHA256SUMS); then
    echo 'state=artifact_integrity_error'; exit 4
  fi
  printf 'profile=%s\n' "$(basename "$target")"
  if command -v getprop >/dev/null 2>&1; then
    model="$(getprop ro.product.model)"
    printf 'model=%s\n' "$model"
    if [ -n "$model" ] && [ "$model" != "$(cat "$target/device.model")" ]; then
      echo 'state=model_requires_validation'; exit 5
    fi
  fi
  echo 'state=candidate_device_validation_required'
  echo 'device_compatible_verified=false'
  echo 'signature_acceptance_verified=false'
  echo 'original_rt_fully_verified=false'
  exit 6
done
[ "$matched" -eq 0 ] && echo 'state=build_required_for_exact_kernel'
exit 7
