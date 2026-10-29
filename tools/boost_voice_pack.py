#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
# Free software under the GNU General Public License v3 or later; see LICENSE.
"""Make the WAV files of an OSIS voice pack louder by a percentage, without letting a loud one clip.

    python tools/boost_voice_pack.py "D:/SkyrimSE-MO2/mods/OSIS Vanilla Voices and Virgin Openers" --percent 25 --backup "D:/osis_audio_backup"

Every .wav under the folder (16-bit PCM) is multiplied by 1 + percent/100. Samples past the knee (0.70 of full scale) are bent smoothly toward the ceiling (-1 dBFS) instead
of clipping, so a clip that peaked at 0.95 comes out at about 0.88 and the quiet ones are simply louder. The original of each file is first copied to --backup (same
relative layout), and a file already there is never overwritten, so running this twice would boost twice: use --restore to put the originals back first.
Unsupported like the other voice tools (see tools/README.md).
"""
import argparse
import shutil
import sys
import wave
from pathlib import Path

import numpy as np

KNEE = 0.70
CEILING = 10 ** (-1.0 / 20.0)


def limit(x: np.ndarray) -> np.ndarray:
    a = np.abs(x)
    over = a > KNEE
    span = CEILING - KNEE
    y = a.copy()
    y[over] = KNEE + span * np.tanh((a[over] - KNEE) / span)
    return np.sign(x) * y


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", type=Path)
    ap.add_argument("--percent", type=float, default=25.0)
    ap.add_argument("--backup", type=Path, required=True, help="where the originals are kept (same layout as the folder)")
    ap.add_argument("--restore", action="store_true", help="copy the originals back and stop")
    a = ap.parse_args()
    root = a.folder.resolve()
    back = a.backup.resolve() / root.name
    files = sorted(root.rglob("*.wav"))
    if a.restore:
        n = 0
        for f in sorted(back.rglob("*.wav")):
            shutil.copy2(f, root / f.relative_to(back))
            n += 1
        print(f"restored {n} file(s)")
        return 0
    gain = 1.0 + a.percent / 100.0
    done = skipped = clipped = 0
    for f in files:
        rel = f.relative_to(root)
        b = back / rel
        if b.exists():
            print(f"already boosted once (backup exists): {rel}; run --restore first", file=sys.stderr)
            return 1
    for f in files:
        rel = f.relative_to(root)
        with wave.open(str(f)) as w:
            p = w.getparams()
            raw = w.readframes(w.getnframes())
        if p.sampwidth != 2:
            skipped += 1
            continue
        b = back / rel
        b.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(f, b)
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
        y = limit(x * gain)
        if np.abs(x * gain).max() > KNEE:
            clipped += 1
        out = np.clip(np.round(y * 32767.0), -32768, 32767).astype("<i2")
        with wave.open(str(f), "wb") as w:
            w.setparams(p)
            w.writeframes(out.tobytes())
        done += 1
    print(f"{done} file(s) boosted by {a.percent:g}%, {clipped} of them limited past the knee, {skipped} skipped (not 16-bit); originals in {back}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
