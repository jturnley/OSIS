#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
# Free software under the GNU General Public License v3 or later; see LICENSE.
"""How strong are the face morphs in a head .tri file, compared with another?

The faces OSIS writes are weights (0..1) on the morph targets in the head's .tri file: the phonemes, the brows, the lids, the moods. A mod that
re-sculpts those targets (Expressive Facial Animation, Waifu Expression Redux, High Poly Head's own sets...) makes the same weight look bigger or
smaller, which is how a face that is right with one set looks pinched or slack with another. This prints, for every morph in the first file, its
largest vertex displacement in game units, and for each further file how many times that it is.

    python tools/tri_morph_strength.py reference.tri other.tri [more.tri ...]
    python tools/tri_morph_strength.py --vanilla-female other.tri       (reads the base game's femalehead.tri out of Skyrim - Meshes0.bsa)

The files: Data/meshes/actors/character/character assets/femalehead.tri and malehead.tri (the vanilla heads), and for High Poly Head
Data/meshes/actors/character/facegenmorphs/morphs/KL/femalehead_expr.tri and malehead_expr.tri. For a bug report, run it on the file that wins in your
mod manager and paste the output. Unsupported like the other voice and audio tools (see tools/README.md).
"""
import argparse
import math
import os
import re
import struct
import sys
from pathlib import Path

DEFAULT_DATA = "C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition/Data"


def parse(path_or_bytes):
    d = path_or_bytes if isinstance(path_or_bytes, (bytes, bytearray)) else open(path_or_bytes, "rb").read()
    if d[:8] != b"FRTRI003":
        raise ValueError("not a FRTRI003 file")
    hdr = struct.unpack_from("<11I", d, 8)
    vc, nmorph = hdr[0], hdr[7]
    start = None
    for m in re.finditer(rb"[A-Za-z][A-Za-z0-9_]{0,30}\x00", d):
        o = m.start() - 4
        if o < 40:
            continue
        ln = struct.unpack_from("<I", d, o)[0]
        if ln != len(m.group()):
            continue
        q = o + 4 + ln + 4
        if q + 6 * vc <= len(d) and 0.0 < struct.unpack_from("<f", d, q - 4)[0] < 0.1:
            start = o
            break
    if start is None:
        raise ValueError("no morph records found")
    out = {}
    p = start
    for _ in range(nmorph):
        if p + 8 > len(d):  # some files (stock High Poly Head) declare one morph more than they hold
            break
        ln = struct.unpack_from("<I", d, p)[0]
        if not 2 <= ln <= 40 or p + 8 + ln + 6 * vc > len(d):
            break
        name = d[p + 4:p + 3 + ln].decode("latin1")
        scale = abs(struct.unpack_from("<f", d, p + 4 + ln)[0])
        q = p + 8 + ln
        vals = struct.unpack_from(f"<{vc * 3}h", d, q)
        best = 0.0
        for i in range(0, vc * 3, 3):
            best = max(best, math.sqrt(vals[i] ** 2 + vals[i + 1] ** 2 + vals[i + 2] ** 2) * scale)
        out[name] = best
        p = q + 6 * vc
    return vc, out


def vanilla(which, data):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import extract_common_voice_lines as ecv  # the BSA reader (needs: pip install lz4)
    key = f"meshes/actors/character/character assets/{which}head.tri"
    for arc in ("Skyrim - Meshes0.bsa", "Skyrim - Meshes1.bsa"):
        b = ecv.Bsa(Path(data) / arc)
        if key in b.entries:
            return b.read(key)
    raise SystemExit(f"{key} not found in the Skyrim meshes archives under {data}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="the reference .tri first, then the ones to compare with it")
    ap.add_argument("--vanilla-female", action="store_true", help="use the base game's femalehead.tri as the reference")
    ap.add_argument("--vanilla-male", action="store_true", help="use the base game's malehead.tri as the reference")
    ap.add_argument("--data", default=os.environ.get("SKYRIM_DATA", DEFAULT_DATA), help="the game's Data folder (for --vanilla-*)")
    a = ap.parse_args()
    sets = []
    if a.vanilla_female or a.vanilla_male:
        which = "female" if a.vanilla_female else "male"
        sets.append((f"vanilla {which}head.tri", parse(vanilla(which, a.data))))
    for f in a.files:
        sets.append((os.path.basename(f), parse(f)))
    if len(sets) < 2:
        ap.error("give a reference and at least one file to compare (or --vanilla-female / --vanilla-male and one file)")
    ref_name, (ref_vc, ref) = sets[0]
    print(f"reference: {ref_name} ({ref_vc} vertices)")
    for n, (vc, _) in sets[1:]:
        print(f"compared:  {n} ({vc} vertices)")
    print()
    width = max(len(k) for k in ref)
    print(f"{'morph':{width}s}  {'units':>6s}  " + "  ".join(f"{n[:18]:>18s}" for n, _ in sets[1:]))
    for name in sorted(ref):
        cols = []
        for _, (_, m) in sets[1:]:
            o = m.get(name) or m.get("EXPR_" + name)  # High Poly Head's expression file names its morphs EXPR_...
            cols.append(f"{o / ref[name]:>17.2f}x" if o is not None and ref[name] > 0 else f"{'-':>18s}")
        print(f"{name:{width}s}  {ref[name]:6.2f}  " + "  ".join(cols))
    # a summary of what moves faces most: the mouth, the brows and squint, the moods
    groups = {"mouth (the 16 phonemes)": ("Aah BigAah BMP ChJSh DST Eee Eh FV I K N Oh OohQ R Th W").split(),
              "brows": ("BrowDownLeft BrowDownRight BrowInLeft BrowInRight BrowUpLeft BrowUpRight").split(),
              "lids": ("BlinkLeft BlinkRight SquintLeft SquintRight").split(),
              "moods": ("MoodAnger MoodDisgusted MoodFear MoodHappy MoodPuzzled MoodSad MoodSurprise").split()}
    print()
    for g, names in groups.items():
        row = []
        for _, (_, m) in sets[1:]:
            r = [(m.get(n) or m.get("EXPR_" + n)) / ref[n] for n in names if ref.get(n) and (m.get(n) or m.get("EXPR_" + n))]
            row.append(f"{sum(r) / len(r):>17.2f}x" if r else f"{'-':>18s}")
        print(f"{g:{width}s}  {'mean':>6s}  " + "  ".join(row))


if __name__ == "__main__":
    main()
