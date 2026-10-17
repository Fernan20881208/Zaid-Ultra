#!/usr/bin/env python3
"""Resolve exactly one new explicit request; ordinary code pushes never build."""
import json
import os
from pathlib import Path
import re
import subprocess


def resolve(changes, before, request):
    if len(changes) != 1:
        raise ValueError("Build-request commit must add exactly one request file")
    status, path = changes[0]
    match = re.fullmatch(r"RTDriver-A17/build-requests/rt-(diag|port)-[a-z0-9-]+\.json", path)
    if status != "A" or not match:
        raise ValueError("Expected a newly added, explicitly named RT build request")
    expected = "kernel_and_rt_" + match[1]
    if request.get("mode") != expected or request.get("source_commit") != before:
        raise ValueError("Request mode/source_commit must match filename and parent")
    return expected


def main():
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    before, after = event["before"], event["after"]
    if subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip() != after:
        raise ValueError("Checkout differs from requested commit")
    diff = subprocess.check_output(["git", "diff", "--name-status", before, after], text=True)
    changes = [line.split("\t") for line in diff.splitlines()]
    if len(changes) != 1 or len(changes[0]) != 2:
        raise ValueError("Expected one request path, without renames")
    request = json.loads(Path(changes[0][1]).read_text())
    print("BUILD_MODE=" + resolve(changes, before, request))


if __name__ == "__main__":
    main()
