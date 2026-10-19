#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
# Free software under the GNU General Public License v3 or later; see LICENSE.
"""Bring the OSIS mod in a Mod Organizer 2 install up to date with what tools/assemble_mod.py just built.

Run it after `python tools/assemble_mod.py`:  every file of mods/<mod>/ is copied into <MO2>/mods/<mod>/ when it differs (hash), and a file the
previous build listed in BUILD-OSIS.txt and this one no longer has is removed, so the installed mod is exactly what a reinstall from the zip would
give, without the reinstall.  Files MO2 keeps there (meta.ini) and anything else are left alone.  If mods/OSIS Virgin Openers.zip exists, its files are
brought up to date in <MO2>/mods/OSIS Virgin Openers/ the same way (the folder is created if it is not there; tick it in MO2 yourself, this never
touches a profile).  Nothing is copied while SkyrimSE.exe is running, since it holds OSIS.dll open.

Usage: python tools/sync_mo2.py [--mo2 D:/SkyrimSE-MO2] [--edition lite|loverslab|both]
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODS = {"lite": "OStim Standalone Immersive Sex", "loverslab": "OStim Standalone Immersive Sex (LoversLab)"}
PACK = "OSIS Virgin Openers"
MANIFEST = "BUILD-OSIS.txt"
MANIFEST_LINE = re.compile(r"^  (.+?) +\d+  .+$")


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def listed(manifest):
    out = set()
    if manifest.is_file():
        for line in manifest.read_text(encoding="utf-8", errors="replace").splitlines():
            m = MANIFEST_LINE.match(line)
            if m:
                out.add(m.group(1).replace("\\", "/"))
    return out


def game_running():
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq SkyrimSE.exe"], capture_output=True, text=True).stdout
    except OSError:
        return False
    return "SkyrimSE.exe" in out


def sync_folder(src, dst):
    changed, removed = 0, 0
    old = listed(dst / MANIFEST)
    new = {p.relative_to(src).as_posix() for p in src.rglob("*") if p.is_file()}
    for rel in sorted(new):
        s, d = src / rel, dst / rel
        if d.is_file() and d.stat().st_size == s.stat().st_size and sha(d) == sha(s):
            continue
        d.parent.mkdir(parents=True, exist_ok=True)
        d.write_bytes(s.read_bytes())
        changed += 1
        print(f"  updated {rel}")
    for rel in sorted(old - new):
        d = dst / rel
        if d.is_file():
            d.unlink()
            removed += 1
            print(f"  removed {rel} (the build no longer has it)")
    return changed, removed


def sync_zip(zpath, dst):
    changed = 0
    with zipfile.ZipFile(zpath) as z:
        names = [n for n in z.namelist() if not n.endswith("/")]
        for n in names:
            data = z.read(n)
            d = dst / n
            if d.is_file() and d.stat().st_size == len(data) and hashlib.sha256(data).hexdigest() == sha(d):
                continue
            d.parent.mkdir(parents=True, exist_ok=True)
            d.write_bytes(data)
            changed += 1
        keep = set(names)
    for p in list(dst.rglob("*")):
        if p.is_file() and p.suffix.lower() in (".fuz", ".wav") and p.relative_to(dst).as_posix() not in keep:
            p.unlink()
            print(f"  removed {p.relative_to(dst).as_posix()} (not in the pack any more)")
    return changed


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mo2", default="D:/SkyrimSE-MO2", help="the Mod Organizer 2 folder (the one with mods/ and profiles/)")
    ap.add_argument("--edition", choices=("lite", "loverslab", "both"), default="lite", help="which assembled mod to bring up to date (default: lite)")
    a = ap.parse_args()
    mods = Path(a.mo2) / "mods"
    if not mods.is_dir():
        sys.exit(f"{mods} is not a folder")
    if game_running():
        sys.exit("SkyrimSE.exe is running and holds OSIS.dll open: close the game, then run this again")
    for key in ("lite", "loverslab"):
        if a.edition not in (key, "both"):
            continue
        src, dst = ROOT / "mods" / MODS[key], mods / MODS[key]
        if not src.is_dir():
            print(f"{MODS[key]}: not assembled (run tools/assemble_mod.py first)")
            continue
        if not dst.is_dir():
            print(f"{MODS[key]}: not in {mods}, skipped (install it once from its zip)")
            continue
        print(f"{MODS[key]}:")
        changed, removed = sync_folder(src, dst)
        dll_src, dll_dst = src / "SKSE" / "Plugins" / "OSIS.dll", dst / "SKSE" / "Plugins" / "OSIS.dll"
        same = dll_src.is_file() and dll_dst.is_file() and sha(dll_src) == sha(dll_dst)
        print(f"  {changed} updated, {removed} removed; OSIS.dll {'matches' if same else 'DIFFERS from'} the build ({sha(dll_dst)[:12] if dll_dst.is_file() else 'missing'})")
        if not same:
            sys.exit(1)
    zpath = ROOT / "mods" / f"{PACK}.zip"
    if zpath.is_file():
        dst = mods / PACK
        existed = dst.is_dir()
        print(f"{PACK}:")
        n = sync_zip(zpath, dst)
        print(f"  {n} file(s) updated" + ("" if existed else f"; new mod folder {dst}: tick it in MO2 to use it"))


if __name__ == "__main__":
    main()
