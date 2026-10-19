#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
# Free software under the GNU General Public License v3 or later; see LICENSE.
"""Put the OStim moan packs of the base game's female voice types, and the OSIS Virgin Openers, into one installable zip.

Each moan pack is a mod folder made by make_ostim_voice_pack.py: an ESL-flagged ESP, the voice set SKSE/Plugins/OStim/voice sets/Fem<name>.json (assigned to
one voice type of Skyrim.esm) and its WAV files under Sound/fx/OStim/actor/{moan,climax}/Female/<name>/.  The openers (make_openers_pack.py) are
Sound/OSIS/Openers/<voice type>/<personality>/*.fuz.  All of them are laid out like Data/, so one zip holds them side by side; no two packs share a path.

    python tools/make_vanilla_voices_zip.py --packs "D:/SkyrimSE-MO2/mods" --packs "D:/osis_moan_packs/packs" \\
        --openers "mods/OSIS Virgin Openers.zip" --out "mods/OSIS Vanilla Voices and Virgin Openers.zip"

--packs is a folder holding "OStim Female <name> Voice Pack" mod folders (repeat it; the first folder that has a pack wins).  The names taken are the base game's
female voice types only (Commander, Commoner, Condescending, DarkElf, Eventoned, Haughty, Nord, Orc, Sultry, YoungEager, Shrill, Coward, OldKindly, OldGrumpy, Argonian,
Khajiit); a pack that is not there yet is left out and reported.  meta.ini (the mod manager's file) is never packed.  Unsupported like the other voice tools (see
tools/README.md): the audio is yours, and so is whatever you do with it.
"""
import argparse
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# pack name in "OStim Female <name> Voice Pack" -> the voice type it is for, in the order they are listed
PACKS = [
    ("YoungEager", "FemaleYoungEager"), ("Eventoned", "FemaleEvenToned"), ("Commoner", "FemaleCommoner"), ("Sultry", "FemaleSultry"),
    ("Shrill", "FemaleShrill"), ("Coward", "FemaleCoward"), ("Commander OSSO", "FemaleCommander"), ("Condescending", "FemaleCondescending"),
    ("Nord", "FemaleNord"), ("Orc", "FemaleOrc"), ("DarkElf", "FemaleDarkElf"), ("Haughty", "FemaleElfHaughty"),
    ("OldKindly", "FemaleOldKindly"), ("OldGrumpy", "FemaleOldGrumpy"), ("Argonian", "FemaleArgonian"), ("Khajiit", "FemaleKhajiit"),
]
# with --male: "OStim Male <name> Voice Pack" -> voice type
PACKS_MALE = [
    ("Commoner", "MaleCommoner"), ("CommonerAccented", "MaleCommonerAccented"), ("Brute", "MaleBrute"),
    ("Commander", "MaleCommander"), ("Condescending", "MaleCondescending"), ("Coward", "MaleCoward"),
]

README = """{title}

One mod holding:

 1. OStim voice sets (moans, climax cries, spank and muffled sounds) for the base game's {sex} voice types:
{voices}
    Each is an ESL-flagged plugin (OStim {Sex} <name> Voice.esp), a voice set (SKSE/Plugins/OStim/voice sets/{short}<name>.json) assigned to its voice type, and
    the sound files under Sound/fx/OStim/actor/. OStim picks the set by the actor's voice type.
{openers}
Needs OStim Standalone and OSSO (the speed stages are OSSO's dynamic voice sets). If you installed any "OStim {Sex} <name> Voice Pack" for these voice types as
separate mods, disable them: this contains the same plugins and files.
Voice types without a set yet (they keep OStim's own voices): {missing}.
Not included: voice sets for named NPCs and followers.
The sounds were voiced with a text-to-speech service; no real person's recording is in this mod.
"""


def find_pack(name, folders, sex="Female"):
    for f in folders:
        d = f / f"OStim {sex} {name} Voice Pack"
        if d.is_dir():
            return d
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--packs", action="append", required=True, help="a folder holding 'OStim Female <name> Voice Pack' (or Male, with --male) folders (repeatable)")
    ap.add_argument("--male", action="store_true", help="the male voice types' packs ('OStim Male <name> Voice Pack'), instead of the female ones")
    ap.add_argument("--openers", help="mods/OSIS Virgin Openers.zip")
    ap.add_argument("--out", required=True)
    ap.add_argument("--label", help="a word after the title in the README, e.g. preview")
    ap.add_argument("--license-file",help="the terms of the audio, packed as 'LICENSE - audio.txt' (e.g. docs/VOICE_PACK_LICENSE.txt); the README points to it")
    a = ap.parse_args()
    folders = [Path(p) for p in a.packs]

    entries = {}   # arcname -> source Path or (zip, name)
    included, missing = [], []
    sex = "Male" if a.male else "Female"
    for name, vt in (PACKS_MALE if a.male else PACKS):
        d = find_pack(name, folders, sex)
        if not d:
            missing.append(vt)
            continue
        included.append((name, vt, d))
        for f in sorted(d.rglob("*")):
            if f.is_file() and f.name.lower() != "meta.ini":
                arc = f.relative_to(d).as_posix()
                if arc in entries:
                    sys.exit(f"{arc} is in two packs ({d.name} and another): they cannot share one folder")
                entries[arc] = f
    opener_zip = None
    if a.openers:
        opener_zip = zipfile.ZipFile(a.openers)
        for n in opener_zip.namelist():
            if n.endswith("/") or n.lower() in ("readme.txt", "license - audio.txt"):
                continue
            if n in entries:
                sys.exit(f"{n} is in a moan pack and the openers")
            entries[n] = (opener_zip, n)

    voices = "\n".join(f"      {vt}  ({d.name})" for name, vt, d in included)
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    total = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        opener_text = " 2. The OSIS Virgin Openers: the lines a virgin says at the start of a scene (Sound/OSIS/Openers/<voice type>/<personality>/*.fuz), for OSIS 2.0 and later.\n" if opener_zip else ""
        title = ("OSIS Vanilla Male Voices" if a.male else "OSIS Vanilla Voices") + (" and Virgin Openers" if opener_zip else "") + (f" ({a.label})" if a.label else "")
        readme = README.format(voices=voices, missing=("every other male voice type (only the ones above have a set so far)" if a.male else (", ".join(missing) if missing else "none")), title=title, sex=sex.lower(), Sex=sex, short="Male" if a.male else "Fem", openers=opener_text)
        if a.license_file:
            readme += "\nThe audio is not under the GPL: see LICENSE - audio.txt for its terms.\n"
            z.write(a.license_file, "LICENSE - audio.txt")
        z.writestr("README.txt", readme)
        for arc in sorted(entries):
            src = entries[arc]
            if isinstance(src, Path):
                z.write(src, arc)
                total += src.stat().st_size
            else:
                data = src[0].read(src[1])
                z.writestr(arc, data)
                total += len(data)
    print(f"{len(included)} moan packs: {', '.join(vt for _, vt, _ in included)}")
    if missing:
        print(f"NOT included (no pack yet): {', '.join(missing)}")
    print(f"{len(entries)} files, {total / 1048576:.0f} MB -> {out} ({out.stat().st_size / 1048576:.0f} MB)")


if __name__ == "__main__":
    main()
