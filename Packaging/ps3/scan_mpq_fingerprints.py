#!/usr/bin/env python3
"""Generate a streaming checksum manifest for user-supplied Diablo MPQ files.

This script never downloads, modifies, or distributes game data.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
import zlib
from pathlib import Path
from typing import Iterable


CHUNK_SIZE = 1024 * 1024


def collect_mpqs(inputs: Iterable[Path]) -> list[Path]:
    found: dict[str, Path] = {}
    for item in inputs:
        if not item.exists():
            raise FileNotFoundError(f"Input does not exist: {item}")
        candidates = item.rglob("*") if item.is_dir() else [item]
        for candidate in candidates:
            if candidate.is_file() and candidate.suffix.lower() == ".mpq":
                resolved = candidate.resolve()
                found[str(resolved).lower()] = resolved
    return sorted(found.values(), key=lambda path: (path.name.lower(), str(path).lower()))


def fingerprint(path: Path) -> dict[str, object]:
    md5 = hashlib.md5()
    sha1 = hashlib.sha1()
    sha256 = hashlib.sha256()
    crc32 = 0
    size = 0
    with path.open("rb") as source:
        while True:
            chunk = source.read(CHUNK_SIZE)
            if not chunk:
                break
            size += len(chunk)
            md5.update(chunk)
            sha1.update(chunk)
            sha256.update(chunk)
            crc32 = zlib.crc32(chunk, crc32)
    return {
        "filename": path.name,
        "size_bytes": size,
        "md5": md5.hexdigest(),
        "sha1": sha1.hexdigest(),
        "sha256": sha256.hexdigest(),
        "crc32": f"{crc32 & 0xffffffff:08x}",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path, help="MPQ files and/or directories")
    parser.add_argument("-o", "--output", default="-", help="JSON output path (default: stdout)")
    args = parser.parse_args()

    try:
        files = collect_mpqs(args.paths)
        if not files:
            parser.error("No .mpq files were found in the supplied paths")
        result = {
            "schema_version": 1,
            "algorithm": "whole-file",
            "archives": [fingerprint(path) for path in files],
        }
        encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
        if args.output == "-":
            sys.stdout.write(encoded)
        else:
            output_path = Path(args.output)
            output_path.parent.mkdir(parents=True, exist_ok=True)
            output_path.write_text(encoded, encoding="utf-8")
            print(f"Wrote {len(files)} MPQ fingerprints to {output_path}", file=sys.stderr)
    except OSError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
