#!/usr/bin/env python3
# UNSUPPORTED helper, not part of the OSIS mod. Read tools/README.md first: what you make with it, and every consequence, is yours alone.
"""Text transcript of the common voice lines that extract_common_voice_lines.py writes.

The spoken text is not in the audio; it is in the INFO records of the plugins (NAM1 responses,
looked up in the .ilstrings tables).  A voice file is named <quest>_<topic>_<infoid>_<response>.

    python tools/voice_line_transcript.py --out voices_common/transcript.txt

Lists the lines in the same order as the WAVs, with the same voice-type selection options.
"""
import argparse
import math
import re
import struct
import sys
import zlib
from collections import defaultdict
from pathlib import Path

import extract_common_voice_lines as ex

PLUGINS = ["Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"]


def subrecords(data):
    i, big = 0, None
    while i + 6 <= len(data):
        t, n = struct.unpack_from("<4sH", data, i)
        i += 6
        if t == b"XXXX":
            big = struct.unpack_from("<I", data, i)[0]
            i += 4
            continue
        if big is not None:
            n, big = big, None
        yield t, data[i:i + n]
        i += n


def records(buf, start, end, want_group=None):
    """Yield (type, formid, flags, data) for records, descending into every GRUP (or only `want_group`)."""
    i = start
    while i + 24 <= end:
        t, size, flags, fid = struct.unpack_from("<4sIII", buf, i)
        if t == b"GRUP":
            label = buf[i + 8:i + 12]
            if want_group is None or label == want_group or struct.unpack_from("<I", buf, i + 12)[0] != 0:
                yield from records(buf, i + 24, i + size, None)
            i += size
            continue
        data = buf[i + 24:i + 24 + size]
        if flags & 0x00040000:
            data = zlib.decompress(data[4:])
        yield t, fid, flags, data
        i += 24 + size


def load_strings(data_dir, name):
    """{id: text} from <name>_english.ilstrings, from a loose file or any BSA."""
    key = f"strings/{name.lower()}_english.ilstrings"
    loose = Path(data_dir) / key
    if loose.exists():
        raw = loose.read_bytes()
    else:
        for bsa in sorted(Path(data_dir).glob("*.bsa")):
            try:
                b = ex.Bsa(bsa)
            except ValueError:
                continue
            if key in b.entries:
                raw = b.read(key)
                break
        else:
            return {}
    count, _ = struct.unpack_from("<II", raw)
    base = 8 + count * 8
    out = {}
    for n in range(count):
        sid, off = struct.unpack_from("<II", raw, 8 + n * 8)
        ln = struct.unpack_from("<I", raw, base + off)[0]
        out[sid] = raw[base + off + 4:base + off + 4 + ln].rstrip(b"\0").decode("utf-8", "replace")
    return out


def dialogue_text(data_dir):
    """{(owner plugin, formid & 0xFFFFFF, response number): text}, later plugins overriding."""
    text = {}
    for plugin in PLUGINS:
        path = Path(data_dir) / plugin
        if not path.exists():
            continue
        buf = path.read_bytes()
        _t, _s, flags = struct.unpack_from("<4sII", buf, 0)
        hsize = struct.unpack_from("<I", buf, 4)[0]
        masters = [d[:-1].decode().lower() for t, d in subrecords(buf[24:24 + hsize]) if t == b"MAST"]
        strings = load_strings(data_dir, plugin[:-4]) if flags & 0x80 else None
        me = plugin.lower()
        for t, fid, _f, data in records(buf, 24 + hsize, len(buf), b"DIAL"):
            if t != b"INFO":
                continue
            idx = fid >> 24
            owner = masters[idx] if idx < len(masters) else me
            num = 0
            for st, sd in subrecords(data):
                if st == b"TRDT":
                    num = sd[12]
                elif st == b"NAM1":
                    s = (strings or {}).get(struct.unpack_from("<I", sd)[0], "") if strings is not None \
                        else sd.rstrip(b"\0").decode("utf-8", "replace")
                    text[(owner, fid & 0xFFFFFF, num)] = s
    return text


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=ex.DEFAULT_DATA)
    ap.add_argument("--extra", action="append", default=[])
    ap.add_argument("--out", default="voices_common/transcript.txt")
    ap.add_argument("--exclude", default=ex.DEFAULT_EXCLUDE)
    ap.add_argument("--min-coverage", type=float, default=1.0)
    args = ap.parse_args()

    voices = ex.collect(args.data, args.extra)
    if args.exclude:
        skip = re.compile(args.exclude)
        voices = {vt: f for vt, f in voices.items() if not skip.search(vt)}
    need = max(1, math.ceil(args.min_coverage * len(voices) - 1e-9))
    count = defaultdict(int)
    for files in voices.values():
        for k in files:
            count[k] += 1
    common = sorted(k for k, n in count.items() if n >= need)

    text = dialogue_text(args.data)
    rows, missing = [], 0
    for n, (plugin, stem) in enumerate(common, 1):
        m = re.search(r"_([0-9a-f]{8})_(\d+)$", stem)
        s = text.get((plugin, int(m.group(1), 16) & 0xFFFFFF, int(m.group(2)))) if m else None
        if s is None:
            missing += 1
        rows.append(f"{n:3d}  {stem}\n     {s if s is not None else '(text not found)'}")
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"{len(common)} lines, {missing} without text -> {out}")


if __name__ == "__main__":
    main()
