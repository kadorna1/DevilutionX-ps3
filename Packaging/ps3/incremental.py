#!/usr/bin/env python3
"""Preserve unchanged source timestamps in cached CMake/Ninja build trees."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

EPOCH = 946684800  # 2000-01-01 UTC; older than cached object files.
MANIFEST_NAME = ".ps3-source-hashes.json"


def tracked_source_hashes() -> dict[str, str]:
    result = subprocess.run(
        ["git", "ls-files", "--recurse-submodules", "-z"],
        check=True,
        stdout=subprocess.PIPE,
    )
    hashes: dict[str, str] = {}
    for raw_path in result.stdout.split(b"\0"):
        if not raw_path:
            continue
        name = os.fsdecode(raw_path)
        path = Path(name)
        if not path.is_file() or path.is_symlink():
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        hashes[name] = digest
    return hashes


def prepare(build_dir: Path) -> None:
    manifest = build_dir / MANIFEST_NAME
    if not manifest.is_file():
        print("No source-hash manifest in the restored build cache; this is a full build.")
        return
    try:
        previous = json.loads(manifest.read_text(encoding="utf-8"))
        if not isinstance(previous, dict):
            raise ValueError("manifest must contain an object")
    except (OSError, ValueError) as exc:
        print(f"Invalid incremental manifest ({exc}); leaving checkout timestamps unchanged.")
        return

    current = tracked_source_hashes()
    unchanged = 0
    changed = 0
    for name, digest in current.items():
        path = Path(name)
        if previous.get(name) == digest:
            os.utime(path, (EPOCH, EPOCH), follow_symlinks=False)
            unchanged += 1
        else:
            changed += 1

    removed = len(set(previous) - set(current))
    print(
        "Incremental source scan: "
        f"{unchanged} unchanged files timestamp-normalized, "
        f"{changed} added/changed files left for rebuild, "
        f"{removed} tracked files removed."
    )


def record(build_dir: Path) -> None:
    build_dir.mkdir(parents=True, exist_ok=True)
    manifest = build_dir / MANIFEST_NAME
    manifest.write_text(
        json.dumps(tracked_source_hashes(), sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"Recorded source hashes in {manifest}")


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] not in {"prepare", "record"}:
        print(
            f"Usage: {sys.argv[0]} prepare|record BUILD_DIR",
            file=sys.stderr,
        )
        return 2
    mode = sys.argv[1]
    build_dir = Path(sys.argv[2])
    if mode == "prepare":
        prepare(build_dir)
    else:
        record(build_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
