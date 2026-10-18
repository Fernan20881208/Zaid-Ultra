#!/usr/bin/env python3
"""Prepare pinned official GKI prebuilts and DDK tooling; never build or load."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "gki/14987696.json"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def verify(data, spec):
    if len(data) != spec["size"] or hashlib.md5(data).hexdigest() != spec["md5"]:
        raise ValueError("Published GKI artifact size/MD5 mismatch")
    if spec.get("sha256") and sha(data) != spec["sha256"]:
        raise ValueError("Pinned GKI artifact SHA-256 mismatch")


def download_one(name, spec, lock, cache, destination):
    cached = cache / name
    if cached.is_file():
        data = cached.read_bytes()
        verify(data, spec)
    else:
        encoded = urllib.parse.quote(name, safe="/")
        url = (f"https://ci.android.com/builds/submitted/{lock['build_id']}/"
               f"{lock['build_target']}/latest/raw/{encoded}")
        with urllib.request.urlopen(url, timeout=180) as response:
            data = response.read()
        verify(data, spec)
        cached.parent.mkdir(parents=True, exist_ok=True)
        cached.write_bytes(data)
    target = destination / spec["local_name"]
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    print(f"Verified GKI input {name}: {len(data)} bytes", flush=True)
    return name, sha(data)


def run(*args, cwd=None):
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def clone_project(project, kleaf):
    path, name, revision = (project.get(k) for k in ("path", "name", "revision"))
    if not path or not name or not revision or len(revision) != 40:
        raise ValueError("Manifest must contain full, pinned Git revisions")
    target = kleaf / path
    target.mkdir(parents=True, exist_ok=True)
    if not (target / ".git").exists():
        run("git", "init", "-q", target)
        run("git", "-C", target, "remote", "add", "origin",
            "https://android.googlesource.com/" + name)
    try:
        found = subprocess.check_output(["git", "-C", str(target), "rev-parse", "HEAD"],
                                        text=True, stderr=subprocess.DEVNULL).strip()
    except subprocess.CalledProcessError:
        found = None
    if found != revision:
        run("git", "-C", target, "-c", "http.version=HTTP/1.1", "fetch",
            "--depth=1", "--filter=blob:none", "origin", revision)
        # Retain only the toolchain versions used by this build. All source
        # and compiler contents still come from their pinned official commits.
        sparse = {"prebuilts/clang/host/linux-x86": ["clang-r536225"],
                  "prebuilts/rust": ["linux-x86/1.82.0"]}.get(path)
        if sparse:
            run("git", "-C", target, "sparse-checkout", "init", "--cone")
            run("git", "-C", target, "sparse-checkout", "set", *sparse)
        run("git", "-C", target, "checkout", "--detach", revision)
    found = subprocess.check_output(["git", "-C", str(target), "rev-parse", "HEAD"],
                                    text=True).strip()
    if found != revision:
        raise ValueError(f"Tooling revision mismatch: {path}")
    for link in project.findall("linkfile"):
        source = target / link.get("src")
        dest = kleaf / link.get("dest")
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.is_symlink():
            if dest.resolve() != source.resolve():
                raise ValueError(f"Conflicting manifest symlink: {dest}")
        elif not dest.exists():
            dest.symlink_to(source)
    print(f"Pinned DDK project {path}: {revision}", flush=True)


def prepare(workspace, cache):
    lock = json.loads(LOCK.read_text())
    workspace.mkdir(parents=True, exist_ok=True)
    prebuilts = workspace / "prebuilts/kernel"
    prebuilts.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=4) as pool:
        futures = [pool.submit(download_one, n, s, lock, cache, prebuilts)
                   for n, s in lock["files"].items()]
        hashes = dict(f.result() for f in futures)
    config = (prebuilts / "kernel_aarch64_dot_config").read_bytes()
    if sha(config) != lock["device_config_sha256"]:
        raise ValueError("Official kernel config differs from the received device config")
    manifest = ET.fromstring((prebuilts / "manifest.xml").read_text())
    common = next(p for p in manifest.findall("project") if p.get("path") == "common")
    if common.get("revision") != lock["source_commit"]:
        raise ValueError("GKI manifest source commit mismatch")
    kleaf = workspace / "external/kleaf"
    projects = [p for p in manifest.findall("project")
                if set(p.get("groups", "").replace(",", " ").split()) & {"ddk", "ddk-external"}]
    with ThreadPoolExecutor(max_workers=4) as pool:
        futures = [pool.submit(clone_project, p, kleaf) for p in projects]
        for future in futures:
            future.result()
    with tarfile.open(prebuilts / "kernel_aarch64_ddk_headers_archive.tar.gz") as archive:
        archive.extractall(kleaf, filter="data")
    shutil.copyfile(prebuilts / "build.config.constants", kleaf / "common/build.config.constants")
    run("python3", prebuilts / "init_ddk.zip", "--local", "--ddk_workspace", workspace,
        "--kleaf_repo", kleaf, "--prebuilts_dir", prebuilts)
    package = workspace / "rt"
    package.mkdir(exist_ok=True)
    # Nested Samsung BUILD.bazel files must not create Bazel package boundaries
    # inside the DDK package. Kbuild files are kept as source evidence.
    shutil.copytree(ROOT / "rt_port", package / "rt_port", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("BUILD.bazel", "__pycache__", "*.pyc"))
    shutil.copyfile(ROOT / "gki/BUILD.bazel", package / "BUILD.bazel")
    compiler = kleaf / "prebuilts/clang/host/linux-x86/clang-r536225/bin/clang"
    version = subprocess.check_output([str(compiler), "--version"], text=True)
    expected = "Android (14043575, +pgo, +bolt, +lto, +mlgo, based on r536225)"
    if expected not in version:
        raise ValueError("Compiler differs from device CONFIG_CC_VERSION_TEXT")
    evidence = {"kernel_release": lock["kernel_release"], "source_commit": lock["source_commit"],
                "device_config_sha256": sha(config), "official_input_sha256": hashes,
                "clang_version": version, "kernel_rebuilt": False, "device_tested": False}
    (workspace / "gki-input-verification.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print("DDK prepared; no kernel or module compiled or loaded.", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.workspace.resolve(), args.cache.resolve())
