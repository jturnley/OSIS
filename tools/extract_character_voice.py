#!/usr/bin/env python3
# UNSUPPORTED helper, not part of the OSIS mod. Read tools/README.md first: what you make with it, and every consequence, is yours alone.
"""About N minutes of dialogue from each custom character in one or more voiced mods, as ~9 MB WAV parts.

A character is a voice type that the vanilla game does not have (mm_ashevoice, hlioremivoice, ...).
Voice files are read straight out of each mod's BSAs and loose sound/voice folders (nothing is unpacked).
Lines are taken evenly across the character's whole range, so a long follower gets quests, banter and
combat alike, then put back in their original order with 1.5 s between them.

    python tools/extract_character_voice.py --out "E:/Sounds/CharacterVoices" ^
        --group "AsheCrystalHeart=Ashe - Crystal Heart SE|Serana Dialogue Add-On x Ashe - Crystal Heart - Full Banter Patch (Asherana)" ^
        --group "Remiel=Remiel-Custom Voiced Follower|Remiel Enhancement|Remiel Missing Voice Lines"

Output: <out>/<group>/<voicetype>/<voicetype>_NN.wav, each with a matching .txt (start time, length,
file name and, where the plugin has it, the spoken text).  Needs lz4 and ffmpeg like the other voice tools.
"""
import argparse
import re
import sys
import tempfile
import wave
from pathlib import Path

import extract_common_voice_lines as ex
import voice_line_transcript as vt

RATE = ex.RATE


def van_der_corput(n):
    """0..n-1 in an order where every prefix is spread evenly across the range."""
    bits = max(1, (n - 1).bit_length())
    order = []
    for i in range(1 << bits):
        j = int(format(i, f"0{bits}b")[::-1], 2)
        if j < n:
            order.append(j)
    return order


def collect_group(mod_dirs):
    """-> {voicetype: {(plugin, stem): (getter, ext)}} for every voice type in the mods."""
    voices = {}

    def add(key, getter):
        parts = key.split("/")
        if len(parts) != 5 or parts[:2] != ["sound", "voice"]:
            return
        ext = Path(parts[4]).suffix
        if ext not in ex.AUDIO_EXT:
            return
        voices.setdefault(parts[3], {})[(parts[2], Path(parts[4]).stem)] = (getter, ext)

    for mod in mod_dirs:
        for bsa in sorted(mod.glob("*.bsa")):
            try:
                b = ex.Bsa(bsa)
            except ValueError:
                continue
            for key in b.entries:
                add(key, lambda b=b, key=key: b.read(key))
        voice_root = next((p for p in mod.rglob("*") if p.is_dir() and p.name.lower() == "voice"
                           and p.parent.name.lower() == "sound"), None)
        if voice_root:
            for p in voice_root.rglob("*"):
                if p.is_file():
                    add("/".join(("sound", "voice") + tuple(x.lower() for x in p.relative_to(voice_root).parts)),
                        lambda p=p: p.read_bytes())
    return voices


def plugin_text(mod_dirs):
    """{(plugin file name lower, formid & 0xFFFFFF, response): text} from the mods' own non-localized plugins."""
    import struct
    text = {}
    for mod in mod_dirs:
        for esp in list(mod.glob("*.esp")) + list(mod.glob("*.esm")):
            try:
                buf = esp.read_bytes()
                flags = struct.unpack_from("<I", buf, 8)[0]
                if flags & 0x80:                      # localized: the text lives in string tables
                    continue
                hsize = struct.unpack_from("<I", buf, 4)[0]
                for t, fid, _f, data in vt.records(buf, 24 + hsize, len(buf), b"DIAL"):
                    if t != b"INFO":
                        continue
                    num = 0
                    for st, sd in vt.subrecords(data):
                        if st == b"TRDT":
                            num = sd[12]
                        elif st == b"NAM1":
                            text[(esp.name.lower(), fid & 0xFFFFFF, num)] = sd.rstrip(b"\0").decode("utf-8", "replace")
            except Exception:
                continue
    return text


def line_text(text, key):
    m = re.search(r"_([0-9a-f]{8})_(\d+)$", key[1])
    return text.get((key[0], int(m.group(1), 16) & 0xFFFFFF, int(m.group(2))), "") if m else ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mods-root", default=r"D:\SkyrimSE-MO2\mods")
    ap.add_argument("--group", action="append", required=True, metavar="NAME=MOD|MOD|...",
                    help="a name and the mod folders (separated by |) whose characters to extract")
    ap.add_argument("--out", required=True)
    ap.add_argument("--data", default=ex.DEFAULT_DATA, help="game Data folder, to tell vanilla voice types apart")
    ap.add_argument("--minutes", type=float, default=15, help="minutes of speech per character")
    ap.add_argument("--part-mb", type=float, default=9, help="size of each WAV part")
    ap.add_argument("--gap", type=float, default=1.5)
    ap.add_argument("--min-line", type=float, default=0.8, help="skip lines shorter than this (s)")
    ap.add_argument("--max-line", type=float, default=30, help="skip lines longer than this (s)")
    ap.add_argument("--min-lines", type=int, default=1, help="skip custom voice types with fewer lines than this")
    ap.add_argument("--vanilla-min", type=int, default=0,
                    help="also count a vanilla voice type as a character when the mod gives it this many lines")
    ap.add_argument("--only", help="comma list of voice types to extract")
    ap.add_argument("--ffmpeg", default=ex.DEFAULT_FFMPEG)
    args = ap.parse_args()

    vanilla = set(ex.collect(args.data, []))
    gap = b"\0" * (2 * int(RATE * args.gap))
    limit = int(args.part_mb * 1e6)
    want_s = args.minutes * 60
    only = set(args.only.split(",")) if args.only else None
    out_root = Path(args.out)
    summary = []
    with tempfile.TemporaryDirectory() as tmp:
        for spec in args.group:
            name, _, mods = spec.partition("=")
            mod_dirs = [Path(args.mods_root) / m for m in mods.split("|")]
            for d in mod_dirs:
                if not d.is_dir():
                    sys.exit(f"no such mod folder: {d}")
            voices = collect_group(mod_dirs)
            # characters: voice types the vanilla game lacks, plus (with --vanilla-min) a vanilla type the mod
            # gives many lines to, as long as it is only one to three of them (not a mod of generic lines)
            heavy = {v for v, f in voices.items() if v in vanilla and args.vanilla_min and len(f) >= args.vanilla_min}
            voices = {v: f for v, f in voices.items()
                      if (v not in vanilla and len(f) >= args.min_lines) or (v in heavy and len(heavy) <= 3)}
            text = plugin_text(mod_dirs)
            print(f"{name}: {len(voices)} custom voice types: "
                  + ", ".join(f"{v} ({len(f)})" for v, f in sorted(voices.items(), key=lambda kv: -len(kv[1]))))
            for voice, files in sorted(voices.items()):
                if only and voice not in only:
                    continue
                keys = sorted(files)
                chosen, total = [], 0.0
                for i in van_der_corput(len(keys)):
                    if total >= want_s:
                        break
                    getter, ext = files[keys[i]]
                    try:
                        pcm = ex.decode(getter(), ext, args.ffmpeg, tmp)
                    except Exception:
                        continue
                    secs = len(pcm) / 2 / RATE
                    if not args.min_line <= secs <= args.max_line:
                        continue
                    chosen.append((i, pcm))
                    total += secs
                chosen.sort(key=lambda c: c[0])            # back in original order
                folder = out_root / name / voice
                folder.mkdir(parents=True, exist_ok=True)
                part, w, size, stamps, t = 0, None, 0, [], 0.0

                def close():
                    nonlocal w, stamps
                    if w:
                        w.close()
                        (folder / f"{voice}_{part:02d}.txt").write_text("\n".join(
                            f"{n:3d}  {int(s // 60):2d}:{s % 60:06.3f}  {d:5.2f}s  {k[1]}  {line_text(text, k)}"
                            for n, s, d, k in stamps) + "\n", encoding="utf-8")
                        w, stamps = None, []

                for i, pcm in chosen:
                    if w is None or size + len(pcm) + len(gap) > limit:
                        close()
                        part += 1
                        w = wave.open(str(folder / f"{voice}_{part:02d}.wav"), "wb")
                        w.setnchannels(1)
                        w.setsampwidth(2)
                        w.setframerate(RATE)
                        size, t = 44, 0.0
                    stamps.append((len(stamps) + 1, t, len(pcm) / 2 / RATE, keys[i]))
                    w.writeframes(pcm + gap)
                    size += len(pcm) + len(gap)
                    t += (len(pcm) + len(gap)) / 2 / RATE
                close()
                summary.append((name, voice, len(keys), len(chosen), total, part))
                print(f"  {voice}: {len(chosen)} of {len(keys)} lines, {total / 60:.1f} min speech, {part} part(s)")
    print("\ndone")


if __name__ == "__main__":
    main()
