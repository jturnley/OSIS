#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
# Free software under the GNU General Public License v3 or later; see LICENSE.
"""Make the OSIS Virgin Openers voice pack from the clips elevenlabs_osis_library.py voiced.

The library tool writes one folder per voice (its --out): <voice>/<personality>/opener/<hash>_<words>.mp3.  Skyrim cannot play MP3 and a
bare WAV has no lip data, so each clip becomes a .fuz, the file the game's own dialogue uses:

    MP3 --ffmpeg--> 44.1 kHz mono WAV (one loudness for every voice, a short fade at the end)
        --Skyrim's LipGenerator (the Creation Kit's, with the line's text)--> .lip   (the mouth movement)
        --xwmaencode--> .xwm                                                        (the compressed audio)
        --FUZE header--> Sound/OSIS/Openers/<voice type>/<personality>/<hash>_<words>.fuz

The folder names under --source must be the base game's female voice types (FemaleNord, FemaleOldKindly ...).  The text of each line comes from
the library (--library, the folder with openers.json and personalities.json): the file name's hash is the one the voicing tool made from the
personality, the direction and the text, so a clip whose line was edited since is skipped with a warning.  Needs the Skyrim Special Edition
Creation Kit's Tools folder (LipGenerator, FonixData.cdf, xwmaencode), which is a free download in Steam's Tools, and ffmpeg.
Fallout 4's LipGenerator writes a lip file that is not Skyrim's (a different time scale); use Skyrim's.

    python tools/make_openers_pack.py --source D:/osis_openers --out D:/osis_openers_pack --zip

Writes a folder you can install as a mod and, with --zip, mods/OSIS Virgin Openers.zip.  A clip already made and newer than its MP3 is kept, so it
can be run again after more clips have been voiced.  Unsupported like the other voice tools (see tools/README.md): the audio is yours, and so is
whatever you do with it.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_TOOLS = Path("D:/SteamLibrary/steamapps/common/Skyrim Special Edition/Tools")

README = """OSIS Virgin Openers - voice pack for OStim Standalone Immersive Sex

Install after OSIS (a mod of its own in your mod manager). When a woman OVirginity Reflowered says is a virgin starts a consensual scene, she tells her
partner so with one of five lines for her personality, in her voice type: "Please be gentle with me. I've never done this before."

    Sound/OSIS/Openers/<voice type>/<personality>/<clip>.fuz

Switch it off in the OSIS menu (Personality > First times > "A virgin tells her partner so"), or remove the mod.
The clips were voiced with a text-to-speech service; no real person's recording is in this pack.
"""


def slug(text, n=40):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")[:n] or "clip"


def load_texts(library):
    """-> {file stem: spoken text}, the stems the voicing tool gives: sha1(personality|[direction] text)[:8] + _ + slug(text)."""
    lib = Path(library)
    openers = json.loads((lib / "openers.json").read_text(encoding="utf-8-sig"))["virgin"]
    people = json.loads((lib / "personalities.json").read_text(encoding="utf-8-sig"))["personalities"]
    out = {}
    for person, lines in openers.items():
        direction = "[" + people[person]["opener_direction"] + "]"
        for text in lines:
            digest = hashlib.sha1(f"{person}|{direction} {text}".encode()).hexdigest()[:8]
            out[(person, f"{digest}_{slug(text)}")] = text
    return out


def run(cmd, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"{Path(cmd[0]).name} failed ({r.returncode}): {(r.stdout + r.stderr).strip()[:300]}")
    return r


def make_fuz(job):
    mp3, dst, text, a = job
    if dst.exists() and dst.stat().st_mtime >= mp3.stat().st_mtime:
        return dst, False
    dst.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="openers_") as tmp:
        work = Path(tmp)
        wav, lip, xwm = work / "clip.wav", work / "clip.lip", work / "clip.xwm"
        run([a.ffmpeg, "-y", "-loglevel", "error", "-i", str(mp3),
             "-af", "loudnorm=I=-20:TP=-2:LRA=7,areverse,afade=t=in:st=0:d=0.04,areverse",
             "-ar", "44100", "-ac", "1", "-sample_fmt", "s16", str(wav)], work)
        run([str(a.lipgen), "clip.wav", text, f"-GestureExaggeration:{a.exaggeration}"], work)
        if not lip.exists():
            raise RuntimeError("LipGenerator wrote no .lip")
        run([str(a.xwmaencode), "-b", "48000", str(wav), str(xwm)], work)
        data = lip.read_bytes()
        audio = xwm.read_bytes()
        if audio[:4] != b"RIFF":
            raise RuntimeError("xwmaencode wrote no xWMA")
        dst.write_bytes(b"FUZE" + struct.pack("<II", 1, len(data)) + data + audio)
    return dst, True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", required=True, help="folder of voice folders (each the --out of elevenlabs_osis_library.py)")
    ap.add_argument("--out", required=True, help="the pack folder to write")
    ap.add_argument("--library", default=str(REPO / "tools" / "osis_voice_library"), help="the folder with openers.json and personalities.json")
    ap.add_argument("--tools", default=str(DEFAULT_TOOLS), help="the Skyrim Special Edition Creation Kit's Tools folder")
    ap.add_argument("--exaggeration", type=float, default=1.0, help="LipGenerator's gesture exaggeration (1 is the Creation Kit's default; 1.5 moves the mouth more)")
    ap.add_argument("--zip", action="store_true", help="also write mods/OSIS Virgin Openers.zip")
    ap.add_argument("--license-file", help="the terms of the audio, packed as 'LICENSE - audio.txt' (e.g. docs/VOICE_PACK_LICENSE.txt); the README points to it")
    ap.add_argument("--ffmpeg", default=shutil.which("ffmpeg") or "ffmpeg")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--limit", type=int, help="make only this many clips (a trial)")
    a = ap.parse_args()
    tools = Path(a.tools)
    a.lipgen = tools / "LipGen" / "LipGenerator" / "LipGenerator.exe"
    a.xwmaencode = tools / "Audio" / "xwmaencode.exe"
    for exe in (a.lipgen, a.xwmaencode, tools / "LipGen" / "LipGenerator" / "FonixData.cdf"):
        if not exe.is_file():
            sys.exit(f"missing {exe} (--tools is the Skyrim Special Edition Creation Kit's Tools folder)")
    texts = load_texts(a.library)

    source, out = Path(a.source), Path(a.out)
    root = out / "Sound" / "OSIS" / "Openers"
    jobs, unknown = [], []
    for voice in sorted(p for p in source.iterdir() if p.is_dir() and not p.name.startswith("_")):
        for person in sorted(p for p in voice.iterdir() if p.is_dir()):
            for mp3 in sorted((person / "opener").glob("*.mp3")):
                text = texts.get((person.name, mp3.stem))
                if text is None:
                    unknown.append(mp3)
                    continue
                jobs.append((mp3, root / voice.name / person.name / (mp3.stem + ".fuz"), text, a))
    for mp3 in unknown:
        print(f"skipped (its line is not in {a.library}): {mp3}")
    if not jobs:
        sys.exit(f"no <voice>/<personality>/opener/*.mp3 under {source}")
    if a.limit:
        jobs = jobs[: a.limit]
    failed = 0
    made = 0
    with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        for job, fut in [(j, pool.submit(make_fuz, j)) for j in jobs]:
            try:
                _, new = fut.result()
                made += new
            except Exception as e:
                failed += 1
                print(f"FAILED {job[0].name}: {e}")
    voices = sorted({j[1].parts[-3] for j in jobs})
    print(f"{len(jobs)} clips for {len(voices)} voice type(s), {made} made now, {failed} failed")
    readme = README
    if a.license_file:
        shutil.copyfile(a.license_file, out / "LICENSE - audio.txt")
        readme += "\nThe audio is not under the GPL: see LICENSE - audio.txt for its terms.\n"
    (out / "README.txt").write_text(readme, encoding="utf-8")
    files = sorted(root.rglob("*.fuz"))
    print(f"{sum(f.stat().st_size for f in files) / 1048576:.1f} MB of FUZ in {root}")
    if a.zip and not failed:
        zpath = REPO / "mods" / "OSIS Virgin Openers.zip"
        zpath.parent.mkdir(exist_ok=True)
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            z.write(out / "README.txt", "README.txt")
            if a.license_file:
                z.write(out / "LICENSE - audio.txt", "LICENSE - audio.txt")
            for f in files:
                z.write(f, f.relative_to(out).as_posix())
        print(f"{zpath} ({zpath.stat().st_size / 1048576:.1f} MB)")


if __name__ == "__main__":
    main()
