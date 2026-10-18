#!/usr/bin/env python3
"""Gate one explicit GKI request; ordinary source pushes never build modules."""
import json
import os
from pathlib import Path
import re
import subprocess


def resolve(changes, before, request):
    if len(changes) != 1 or len(changes[0]) != 2:
        raise ValueError("Expected one newly added GKI request file")
    status, path = changes[0]
    if status != "A" or not re.fullmatch(r"RTDriver-A17/build-requests/gki-[a-z0-9-]+\.json", path):
        raise ValueError("Expected one newly added GKI request file")
    if request.get("source_commit") != before or request.get("build_id") != "14987696":
        raise ValueError("Request must pin its source parent and the supported GKI build")
    mode = request.get("mode")
    if mode not in ("preflight", "modules"):
        raise ValueError("Expected preflight or modules mode")
    return mode


if __name__ == "__main__":
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    before, after = event["before"], event["after"]
    if subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip() != after:
        raise ValueError("Checkout differs from requested source")
    diff = subprocess.check_output(["git", "diff", "--name-status", before, after], text=True)
    changes = [line.split("\t") for line in diff.splitlines()]
    if len(changes) != 1 or len(changes[0]) != 2:
        raise ValueError("Expected one GKI request change")
    request = json.loads(Path(changes[0][1]).read_text())
    print("GKI_STAGE=" + resolve(changes, before, request))
