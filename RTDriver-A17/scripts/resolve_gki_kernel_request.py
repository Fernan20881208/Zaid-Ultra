#!/usr/bin/env python3
"""Gate an explicit full source-kernel request; ordinary edits never build it."""
import json
import os
from pathlib import Path
import re
import subprocess


def resolve(changes, before, request):
    if (len(changes) != 1 or len(changes[0]) != 2 or changes[0][0] != "A" or
        not re.fullmatch(r"RTDriver-A17/kernel-requests/taipei-[a-z0-9-]+\.json", changes[0][1])):
        raise ValueError("Expected one newly added taipei kernel request")
    if request.get("source_commit") != before or request.get("build_id") != "14987696":
        raise ValueError("Request must pin its source parent and official GKI baseline")
    if request.get("rt_integration") != "builtin" or request.get("mode") not in ("preflight", "kernel"):
        raise ValueError("Expected preflight/kernel and built-in RT")
    return request["mode"]


if __name__ == "__main__":
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    before, after = event["before"], event["after"]
    if subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip() != after:
        raise ValueError("Checkout differs from requested source")
    changes = [line.split("\t") for line in subprocess.check_output(
        ["git", "diff", "--name-status", before, after], text=True).splitlines()]
    if len(changes) != 1 or len(changes[0]) != 2:
        raise ValueError("Expected one kernel request change")
    request = json.loads(Path(changes[0][1]).read_text())
    print("GKI_KERNEL_MODE=" + resolve(changes, before, request))
