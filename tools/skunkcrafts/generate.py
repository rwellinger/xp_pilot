#!/usr/bin/env python3
"""
SkunkCrafts Updater control-file generator for xp_pilot.

Walks an installed/staged plugin tree and emits the three server-side control
files the updater compares against:

    skunkcrafts_updater_whitelist.txt   <relative_path>|<crc32 unsigned decimal>
    skunkcrafts_updater_sizeslist.txt   <relative_path>|<size in bytes>
    skunkcrafts_updater.cfg             rendered from the .cfg.template (version filled in)

It also writes an (empty) skunkcrafts_updater_oncelist.txt. xp_pilot keeps no
user-editable files inside the plugin tree — flights, reports and settings.json
live under <X-Plane>/Output/x_pilot_reports/ — so there is nothing to protect
from being overwritten. The only tracked content is the three platform binaries
(mac_x64/lin_x64/win_x64) plus the bundled read-only config
data/flight_logger_profiles.json, which is intentionally tracked so updated
landing-quality thresholds reach existing users.

Anything not in the whitelist is left untouched by the updater.

Usage:
    python3 tools/skunkcrafts/generate.py \
        --tree  "dist/xp_pilot" \
        --version 1.5.3

Run it against the *release* tree you are about to publish, then commit/push
that tree + the control files to the `release` branch the cfg `module` URL
points at.
"""
import argparse
import fnmatch
import os
import zlib
from pathlib import Path

# Paths (relative to the plugin root) the updater must NOT manage.
# Glob patterns, matched against the forward-slash relative path.
IGNORE_GLOBS = [
    ".DS_Store",
    "**/.DS_Store",
    "skunkcrafts_updater_*.txt",  # the control files themselves
    "skunkcrafts_updater.cfg",
]

# Files the updater should download only if absent, never overwrite.
# xp_pilot has none: all user-editable runtime data lives outside the tree.
ONCE_GLOBS = []


def crc32(fp: Path) -> int:
    checksum = 0
    with fp.open("rb") as f:
        while chunk := f.read(65536):
            checksum = zlib.crc32(chunk, checksum)
    return checksum & 0xFFFFFFFF


def matches(rel: str, globs: list[str]) -> bool:
    return any(fnmatch.fnmatch(rel, g) for g in globs)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", required=True, help="installed plugin root directory")
    ap.add_argument("--version", required=True, help="version string for the cfg")
    args = ap.parse_args()

    tree = Path(args.tree).resolve()
    if not tree.is_dir():
        raise SystemExit(f"not a directory: {tree}")

    here = Path(__file__).parent
    template = (here / "skunkcrafts_updater.cfg.template").read_text()

    whitelist, sizes, oncelist = [], [], []
    for dirpath, _dirs, files in os.walk(tree):
        for name in files:
            abs_path = Path(dirpath) / name
            rel = abs_path.relative_to(tree).as_posix()
            if matches(rel, IGNORE_GLOBS):
                continue
            whitelist.append(f"{rel}|{crc32(abs_path)}")
            sizes.append(f"{rel}|{abs_path.stat().st_size}")
            if matches(rel, ONCE_GLOBS):
                oncelist.append(rel)

    whitelist.sort()
    sizes.sort()
    oncelist.sort()

    (tree / "skunkcrafts_updater_whitelist.txt").write_text("\n".join(whitelist) + "\n")
    (tree / "skunkcrafts_updater_sizeslist.txt").write_text("\n".join(sizes) + "\n")
    (tree / "skunkcrafts_updater_oncelist.txt").write_text("\n".join(oncelist) + "\n")
    (tree / "skunkcrafts_updater.cfg").write_text(
        template.replace("@VERSION@", args.version)
    )

    print(f"tracked {len(whitelist)} files, {len(oncelist)} once-only")
    print(f"wrote control files into {tree}")


if __name__ == "__main__":
    main()
