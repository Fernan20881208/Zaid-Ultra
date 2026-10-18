#!/usr/bin/env bash
# Full source kernel build. No device operations and no ELF metadata patching.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
: "${GKI_KERNEL_WORKSPACE:?Set a fresh kernel workspace}"
: "${GKI_KERNEL_REPORT:?Set an evidence/output directory}"
MODE=${1:-preflight}
case "$MODE" in preflight|kernel) ;; *) echo 'Expected preflight or kernel' >&2; exit 2 ;; esac
mkdir -p "$GKI_KERNEL_REPORT"
GKI_KERNEL_REPORT=$(realpath "$GKI_KERNEL_REPORT")
GKI_KERNEL_WORKSPACE=$(realpath -m "$GKI_KERNEL_WORKSPACE")
git -C "$ROOT" rev-parse HEAD > "$GKI_KERNEL_REPORT/source-commit.txt"
python3 -u "$ROOT/scripts/prepare_gki_kernel.py" --workspace "$GKI_KERNEL_WORKSPACE" \
  --cache "$GKI_KERNEL_WORKSPACE-downloads" 2>&1 | tee "$GKI_KERNEL_REPORT/prepare.log"
cp "$GKI_KERNEL_WORKSPACE/kernel-input-verification.json" "$GKI_KERNEL_WORKSPACE/rt-integration.patch" "$GKI_KERNEL_REPORT/"
cp "$GKI_KERNEL_WORKSPACE/reference/manifest.xml" "$GKI_KERNEL_REPORT/"
cd "$GKI_KERNEL_WORKSPACE"
# Batch mode also works in container namespaces without readable PID starttime.
BAZEL=(tools/bazel --batch)
# The pinned gki_defconfig already selects LTO_NONE. --lto=none would also
# force FRAME_WARN=0, differing from this device's official value of 2048.
FLAGS=(--config=stamp --defconfig_fragment=//common:rt_taipei_defconfig --jobs=2
  --repo_manifest="$GKI_KERNEL_WORKSPACE:$GKI_KERNEL_WORKSPACE/reference/manifest.xml"
  --ignore_missing_projects)
"${BAZEL[@]}" build --nobuild "${FLAGS[@]}" //common:kernel_aarch64 \
  2>&1 | tee "$GKI_KERNEL_REPORT/bazel-nobuild.log"
# Build only Kconfig first. Unexpected configuration changes stop the job before
# compiling the kernel, including changes to signing/CFI/KMI/permissions.
"${BAZEL[@]}" build "${FLAGS[@]}" //common:kernel_aarch64_config \
  2>&1 | tee "$GKI_KERNEL_REPORT/bazel-config.log"
"${BAZEL[@]}" cquery "${FLAGS[@]}" //common:kernel_aarch64_config --output=files \
  > "$GKI_KERNEL_REPORT/config-output-paths.txt"
python3 - "$GKI_KERNEL_REPORT" <<'PY'
from pathlib import Path
import shutil, sys
r = Path(sys.argv[1])
configs = [Path(p) / '.config' for p in (r / 'config-output-paths.txt').read_text().splitlines()]
configs = [p for p in configs if p.is_file()]
if len(configs) != 1:
    raise ValueError('Expected exactly one generated kernel configuration')
shutil.copyfile(configs[0], r / 'kernel.config')
PY
python3 "$ROOT/scripts/verify_gki_kernel.py" --workspace "$GKI_KERNEL_WORKSPACE" \
  --report "$GKI_KERNEL_REPORT" --config-only "$GKI_KERNEL_REPORT/kernel.config" \
  | tee "$GKI_KERNEL_REPORT/config-diff.json"
if [[ "$MODE" == preflight ]]; then
  echo 'Kleaf and Kconfig passed. No kernel image compiled.'
  exit 0
fi
GROUPS_OUT='default,modules_staging_archive,Module.symvers,.config,certs/signing_key.x509'
"${BAZEL[@]}" build "${FLAGS[@]}" --output_groups="$GROUPS_OUT" //common:kernel_aarch64 \
  2>&1 | tee "$GKI_KERNEL_REPORT/bazel-build.log"
"${BAZEL[@]}" cquery "${FLAGS[@]}" --output_groups="$GROUPS_OUT" //common:kernel_aarch64 --output=files \
  > "$GKI_KERNEL_REPORT/kernel-output-paths.txt"
# Copy actual outputs before verification so an incompatible result is preserved.
python3 - "$GKI_KERNEL_REPORT" <<'PY'
from pathlib import Path
import shutil, sys
r = Path(sys.argv[1])
names = {'Image', 'Image.gz', 'Image.lz4', 'vmlinux', 'System.map', 'Module.symvers',
         'modules.builtin', 'modules.builtin.modinfo', 'modules_staging_dir.tar.gz',
         'signing_key.x509'}
found = set()
for line in (r / 'kernel-output-paths.txt').read_text().splitlines():
    p = Path(line)
    if p.name in names and p.is_file():
        if p.name in found:
            raise ValueError('Duplicate output ' + p.name)
        shutil.copyfile(p, r / p.name)
        found.add(p.name)
if names != found:
    raise ValueError('Missing actual outputs: ' + repr(names - found))
PY
readelf -h "$GKI_KERNEL_REPORT/vmlinux" > "$GKI_KERNEL_REPORT/vmlinux-elf.txt"
openssl x509 -inform DER -in "$GKI_KERNEL_REPORT/signing_key.x509" -noout \
  -subject -issuer -fingerprint -sha256 > "$GKI_KERNEL_REPORT/gki-public-certificate.txt"
NDK_CC="$GKI_KERNEL_WORKSPACE/prebuilts/ndk-r26/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"
"$NDK_CC" --version > "$GKI_KERNEL_REPORT/userspace-compiler.txt"
for tool in rt_a17_port_status rt_a17_selftest rt_a17_bp_selftest; do
  "$NDK_CC" -D_GNU_SOURCE -std=gnu11 -O2 -static -Wall -Wextra -Werror \
    "$ROOT/rt_port/tools/$tool.c" "$ROOT/rt_port/tools/rt_a17_client.c" \
    -o "$GKI_KERNEL_REPORT/$tool"
done
python3 "$ROOT/scripts/verify_gki_kernel.py" --workspace "$GKI_KERNEL_WORKSPACE" --report "$GKI_KERNEL_REPORT"
