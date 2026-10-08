#!/usr/bin/env python3
# UNSUPPORTED helper, not part of the OSIS mod. Read tools/README.md first: what you make with it, and every consequence, is yours alone.
"""Pull the dialogue lines every Skyrim voice type has in common into one WAV per voice.

A "line" is a voice file name (quest_topic_infoid_n) under sound/voice/<plugin>/<voicetype>/.
Generic lines (combat barks, greetings, ...) are recorded once per voice type under the same
file name, so the lines common to every voice type are the file names present in all of them.

Reads the voice BSAs (Skyrim SE, version 105) directly, plus any loose sound/voice folders, so
nothing is unpacked to disk.  Needs: python 3, `pip install lz4`, and ffmpeg (decodes the xWMA).

    python tools/extract_common_voice_lines.py --out voices_common
    python tools/extract_common_voice_lines.py --list           # just report the counts
    python tools/extract_common_voice_lines.py --min-coverage 0.9 --out voices_common
"""
import argparse
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
import wave
from collections import defaultdict
from pathlib import Path

try:
    import lz4.frame
except ImportError:
    sys.exit("missing dependency: pip install lz4")

DEFAULT_DATA = r"D:\SteamLibrary\steamapps\common\Skyrim Special Edition\Data"
# Not generic adult NPCs: named characters, creatures, daedra, children, vampires, bandits, ghosts, ...
# and the three Dragonborn dark elf variants, which were recorded separately and share no lines.
DEFAULT_EXCLUDE = (r"unique|^cr|^dlc\d?cr|special|seranavoice|riekling|vampire|warlock|forsworn|"
                   r"bandit|ghost|ld_|child|^dlc2")
DEFAULT_FFMPEG = r"C:\ffmpeg\bin\ffmpeg.exe"
RATE = 44100
AUDIO_EXT = (".fuz", ".xwm", ".wav")


class Bsa:
    """Minimal Skyrim LE/SE BSA reader (versions 104/105)."""

    def __init__(self, path):
        self.path = Path(path)
        self.f = open(path, "rb")
        (magic, ver, off, flags, nfold, nfile, flen, nlen, _ftypes, _pad) = struct.unpack(
            "<4sIIIIIIIHH", self.f.read(36))
        if magic != b"BSA\0" or ver not in (104, 105):
            raise ValueError(f"{path}: not a Skyrim BSA (version {ver})")
        self.compressed = bool(flags & 0x4)
        self.embedded = bool(flags & 0x100) and ver == 105
        self.lz4 = ver == 105
        fold_size = 24 if ver == 105 else 16
        self.f.seek(off)
        folders = []
        for _ in range(nfold):
            rec = self.f.read(fold_size)
            folders.append(struct.unpack_from("<QI", rec)[1])
        blocks = []
        for count in folders:
            n = self.f.read(1)[0]
            name = self.f.read(n).rstrip(b"\0").decode("latin-1").replace("\\", "/").lower()
            files = [struct.unpack("<QII", self.f.read(16)) for _ in range(count)]
            blocks.append((name, files))
        names = self.f.read(nlen).split(b"\0")
        self.entries = {}
        i = 0
        for folder, files in blocks:
            for _h, size, offset in files:
                fname = names[i].decode("latin-1").lower()
                i += 1
                self.entries[f"{folder}/{fname}"] = (size, offset)

    def read(self, key):
        size, offset = self.entries[key]
        invert = bool(size & 0x40000000)
        size &= 0x3FFFFFFF
        self.f.seek(offset)
        data = self.f.read(size)
        if self.embedded:
            data = data[1 + data[0]:]
        if self.compressed != invert:
            orig = struct.unpack_from("<I", data)[0]
            data = data[4:]
            if self.lz4:
                data = lz4.frame.decompress(data)
            else:
                import zlib
                data = zlib.decompress(data)
            assert len(data) == orig
        return data


def collect(data_dir, extra_dirs):
    """-> {voicetype: {(plugin, stem): reader}}; reader() returns the file bytes + its extension."""
    voices = defaultdict(dict)

    def add(key, getter):
        parts = key.split("/")
        if len(parts) != 5 or parts[:2] != ["sound", "voice"]:
            return
        ext = os.path.splitext(parts[4])[1]
        if ext not in AUDIO_EXT:
            return
        plugin, vtype, stem = parts[2], parts[3], os.path.splitext(parts[4])[0]
        voices[vtype][(plugin, stem)] = (getter, ext)

    for bsa in sorted(Path(data_dir).glob("*Voices*.bsa")):
        b = Bsa(bsa)
        for key in b.entries:
            add(key, lambda b=b, key=key: b.read(key))
    for root in [Path(data_dir)] + [Path(d) for d in extra_dirs]:
        v = root / "sound" / "voice"
        if v.is_dir():
            for p in v.rglob("*"):
                if p.is_file():
                    add("/".join(("sound", "voice") + p.relative_to(v).parts).lower(),
                        lambda p=p: p.read_bytes())
    return voices


def decode(raw, ext, ffmpeg, tmp):
    """Bytes of a voice file -> mono 16-bit PCM at RATE."""
    if ext == ".fuz":                       # FUZE, version, lip size, lip data, audio
        assert raw[:4] == b"FUZE"
        lip = struct.unpack_from("<I", raw, 8)[0]
        raw = raw[12 + lip:]
        ext = ".xwm" if raw[8:12] == b"XWMA" else ".wav"
    src = Path(tmp) / ("line" + ext)
    src.write_bytes(raw)
    r = subprocess.run([ffmpeg, "-v", "error", "-i", str(src), "-f", "s16le", "-ac", "1",
                        "-ar", str(RATE), "-"], capture_output=True)
    if r.returncode:
        raise RuntimeError(r.stderr.decode(errors="replace").strip())
    return r.stdout


def line_text(text, key):
    m = re.search(r"_([0-9a-f]{8})_(\d+)$", key[1])
    return text.get((key[0], int(m.group(1), 16) & 0xFFFFFF, int(m.group(2))), "") if m else ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=DEFAULT_DATA, help="Skyrim Data folder (voice BSAs and loose files)")
    ap.add_argument("--extra", action="append", default=[], help="extra folder holding sound/voice (a mod)")
    ap.add_argument("--out", default="voices_common", help="output folder")
    ap.add_argument("--ffmpeg", default=DEFAULT_FFMPEG)
    ap.add_argument("--gap", type=float, default=1.5, help="seconds of silence between lines")
    ap.add_argument("--min-coverage", type=float, default=1.0,
                    help="keep lines present in at least this fraction of voice types (1.0 = every one)")
    ap.add_argument("--exclude", default=DEFAULT_EXCLUDE,
                    help="regex of voice types to leave out (default: everything but base-game adult NPCs); "
                         "'' keeps all")
    ap.add_argument("--split-mb", type=float,
                    help="split each voice into parts of about this many MB; every voice's part N holds the same lines")
    ap.add_argument("--list", action="store_true", help="report counts and stop")
    args = ap.parse_args()

    voices = collect(args.data, args.extra)
    if args.exclude:
        skip = re.compile(args.exclude)
        voices = {vt: f for vt, f in voices.items() if not skip.search(vt)}
    if not voices:
        sys.exit("no voice files found")
    need = max(1, math.ceil(args.min_coverage * len(voices) - 1e-9))
    count = defaultdict(int)
    for files in voices.values():
        for k in files:
            count[k] += 1
    common = sorted(k for k, n in count.items() if n >= need)
    print(f"{len(voices)} voice types, {len(count)} distinct lines, "
          f"{len(common)} present in at least {need} voice types")
    if args.list or not common:
        for vt in sorted(voices, key=lambda v: len(voices[v])):
            have = sum(1 for k in common if k in voices[vt])
            print(f"  {vt:34s} {len(voices[vt]):6d} files, {have:5d} of the common lines")
        if not common:
            print("no line is shared by every voice type; lower --min-coverage")
        return

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    gap = b"\0" * (2 * int(RATE * args.gap))
    try:                                     # the spoken text, for the timestamp lists
        import voice_line_transcript
        text = voice_line_transcript.dialogue_text(args.data)
    except Exception as e:
        print(f"no dialogue text for the timestamp lists: {e}")
        text = {}
    # Decode every voice once, spooling the PCM to disk so the split points can be shared.
    with tempfile.TemporaryDirectory() as tmp:
        sizes = {}                                     # voice -> {line number: bytes of clip + gap}
        for vt in sorted(voices):
            sizes[vt] = {}
            with open(Path(tmp) / f"{vt}.raw", "wb") as spool:
                for n, k in enumerate(common, 1):
                    if k not in voices[vt]:
                        continue
                    getter, ext = voices[vt][k]
                    try:
                        pcm = decode(getter(), ext, args.ffmpeg, tmp)
                    except Exception as e:
                        print(f"  skip {vt}/{k[1]}: {e}")
                        continue
                    spool.write(pcm)
                    sizes[vt][n] = len(pcm)
            print(f"decoded {vt}")
        # Same lines in every voice's part N: close a part when the biggest voice would pass the limit.
        limit = int(args.split_mb * 1e6) if args.split_mb else None
        parts, cur, used = [], [], defaultdict(int)
        for n in range(1, len(common) + 1):
            add = {vt: sizes[vt].get(n, 0) + len(gap) for vt in sizes}
            if limit and cur and max(used[vt] + add[vt] for vt in sizes) > limit:
                parts.append(cur)
                cur, used = [], defaultdict(int)
            cur.append(n)
            for vt in sizes:
                used[vt] += add[vt]
        parts.append(cur)
        digits = len(str(len(parts)))
        for vt in sorted(voices):
            offsets, pos = {}, 0
            for n, size in sizes[vt].items():
                offsets[n] = pos
                pos += size
            with open(Path(tmp) / f"{vt}.raw", "rb") as spool:
                for pi, nums in enumerate(parts, 1):
                    stem = f"{vt}_{pi:0{digits}d}" if limit else vt
                    stamps = []
                    with wave.open(str(out / f"{stem}.wav"), "wb") as w:
                        w.setnchannels(1)
                        w.setsampwidth(2)
                        w.setframerate(RATE)
                        for n in nums:
                            if n not in offsets:
                                continue
                            spool.seek(offsets[n])
                            pcm = spool.read(sizes[vt][n])
                            stamps.append((n, w.getnframes() / RATE, len(pcm) / 2 / RATE, common[n - 1]))
                            w.writeframes(pcm + gap)
                    (out / f"{stem}.txt").write_text("\n".join(
                        f"{n:3d}  {int(t // 60):2d}:{t % 60:06.3f}  {d:5.2f}s  {k[1]}  {line_text(text, k)}"
                        for n, t, d, k in stamps) + "\n", encoding="utf-8")
            print(f"{vt}: {len(parts)} part(s)")
    (out / "lines.txt").write_text("\n".join(f"{p}/{s}" for p, s in common), encoding="utf-8")
    if limit:
        (out / "parts.txt").write_text("\n".join(
            f"part {i}: lines {nums[0]}-{nums[-1]}" for i, nums in enumerate(parts, 1)) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
