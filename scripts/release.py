#!/usr/bin/env python3
"""Bump the clhash project version.

Updates the project version in two places in lock-step:
  - CMakeLists.txt    (project(clhash ... VERSION X.Y.Z ...))
  - include/clhash.h  (CLHASH_VERSION_MAJOR / MINOR / PATCH / STRING)

It does NOT touch git; commit, tag, and push are left to the caller so you can
review the diff first.

Usage:
    scripts/release.py 1.0.1            # bump to 1.0.1
    scripts/release.py --show           # print the current version
    scripts/release.py --dry-run 1.0.1  # show what would change
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CMAKE = REPO / "CMakeLists.txt"
HEADER = REPO / "include" / "clhash.h"

# X.Y.Z. We intentionally do NOT accept pre-release or build metadata here:
# the release process is meant to produce clean, monotonic releases.
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")


def parse_version(s: str) -> tuple[int, int, int]:
    if not VERSION_RE.match(s):
        sys.exit(f"error: version must be MAJOR.MINOR.PATCH, got {s!r}")
    a, b, c = s.split(".")
    return int(a), int(b), int(c)


def read_current_version() -> str:
    text = CMAKE.read_text()
    m = re.search(
        r"project\s*\(\s*clhash\b[^)]*?\bVERSION\s+(\d+\.\d+\.\d+)",
        text, re.S,
    )
    if not m:
        sys.exit("error: could not find project(clhash ... VERSION X.Y.Z) in "
                 f"{CMAKE.relative_to(REPO)}")
    return m.group(1)


def bump_cmake(new: str) -> str:
    text = CMAKE.read_text()
    pattern = re.compile(
        r"(project\s*\(\s*clhash\b[^)]*?\bVERSION\s+)\d+\.\d+\.\d+",
        re.S,
    )
    new_text, n = pattern.subn(rf"\g<1>{new}", text, count=1)
    if n != 1:
        sys.exit(f"error: failed to rewrite project version in "
                 f"{CMAKE.relative_to(REPO)}")
    return new_text


def bump_header(new: str) -> str:
    text = HEADER.read_text()
    major, minor, patch = new.split(".")
    replacements = [
        (r"(#\s*define\s+CLHASH_VERSION_MAJOR\s+)\d+", rf"\g<1>{major}"),
        (r"(#\s*define\s+CLHASH_VERSION_MINOR\s+)\d+", rf"\g<1>{minor}"),
        (r"(#\s*define\s+CLHASH_VERSION_PATCH\s+)\d+", rf"\g<1>{patch}"),
        (r'(#\s*define\s+CLHASH_VERSION_STRING\s+)"[^"]*"', rf'\g<1>"{new}"'),
    ]
    new_text = text
    for pat, repl in replacements:
        new_text, n = re.subn(pat, repl, new_text, count=1)
        if n != 1:
            sys.exit(f"error: failed to rewrite {pat!r} in "
                     f"{HEADER.relative_to(REPO)}")
    return new_text


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Bump the clhash project version.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Usage:", 1)[1] if __doc__ and "Usage:" in __doc__ else None,
    )
    ap.add_argument("version", nargs="?",
                    help="New version in MAJOR.MINOR.PATCH form (e.g. 1.0.1).")
    ap.add_argument("--show", action="store_true",
                    help="Print the current version and exit.")
    ap.add_argument("--dry-run", action="store_true",
                    help="Show the planned change without writing files.")
    args = ap.parse_args()

    current = read_current_version()

    if args.show:
        print(current)
        return

    if not args.version:
        ap.error("missing version argument (or pass --show)")

    new = args.version
    new_t = parse_version(new)
    cur_t = parse_version(current)
    if new_t == cur_t:
        sys.exit(f"error: new version {new} equals current version")
    if new_t < cur_t:
        sys.exit(f"error: new version {new} is older than current {current}")

    cmake_new = bump_cmake(new)
    header_new = bump_header(new)

    if args.dry_run:
        print(f"would bump {current} -> {new}")
        print(f"  {CMAKE.relative_to(REPO)}")
        print(f"  {HEADER.relative_to(REPO)}")
        return

    CMAKE.write_text(cmake_new)
    HEADER.write_text(header_new)
    print(f"bumped {current} -> {new}")
    print(f"  updated {CMAKE.relative_to(REPO)}")
    print(f"  updated {HEADER.relative_to(REPO)}")
    print()
    print("Next steps:")
    print(f"  git add CMakeLists.txt include/clhash.h")
    print(f"  git commit -m 'Bump version to {new}'")
    print(f"  git tag -a v{new} -m 'Release {new}'")
    print(f"  git push origin HEAD && git push origin v{new}")


if __name__ == "__main__":
    main()
