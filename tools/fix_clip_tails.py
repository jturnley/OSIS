#!/usr/bin/env python3
# UNSUPPORTED helper, not part of the OSIS mod. Read tools/README.md first: what you make with it, and every consequence, is yours alone.
"""Find and remove the pop at the end of generated voice clips.

ElevenLabs sometimes starts one more sound right at the end of a clip and cuts it off mid-note.
This looks at the last stretch of every clip:
  * a quiet gap followed by sound that is still going at the final sample  -> fade out the end
    (or, with --cut-gaps, cut at the gap)
  * no gap, but the clip ends loud (abrupt cut)                            -> fade out the end
Clips that die away to silence on their own are left alone.

    python tools/fix_clip_tails.py "E:/Sounds/FemaleYoungEagerpack"            # report only
    python tools/fix_clip_tails.py "E:/Sounds/FemaleYoungEagerpack" --apply    # fix, keeping the originals

--apply moves each original into an _orig folder beside it and writes the fixed MP3 in its place.
"""
import argparse
import shutil
import struct
import subprocess
import sys
from pathlib import Path

RATE = 44100
WIN = RATE // 100                       # 10 ms windows
DEFAULT_FFMPEG = r"C:\ffmpeg\bin\ffmpeg.exe"


def decode(path, ffmpeg):
    raw = subprocess.run([ffmpeg, "-v", "error", "-i", str(path), "-f", "s16le", "-ac", "1", "-ar", str(RATE), "-"],
                         capture_output=True).stdout
    return struct.unpack(f"<{len(raw) // 2}h", raw)


def rms_windows(s):
    out = []
    for i in range(0, len(s) - WIN + 1, WIN):
        seg = s[i:i + WIN]
        out.append((sum(x * x for x in seg) / WIN) ** 0.5)
    return out


def analyse(s, tail_s, min_gap, still_going):
    """-> (verdict, cut sample or None, detail).  verdict: ok | gap | abrupt"""
    r = rms_windows(s)
    if len(r) < 20:
        return "ok", None, "too short"
    ranked = sorted(r)
    loud = ranked[int(len(ranked) * 0.9)]
    quiet = max(40.0, 0.04 * loud)
    start = max(0, len(r) - int(tail_s * 100))
    # last run of quiet windows inside the tail
    j = len(r) - 1
    while j >= start and r[j] >= quiet:
        j -= 1
    if j >= start:                       # found a quiet window; measure its run
        k = j
        while k >= start and r[k] < quiet:
            k -= 1
        run = j - k
        after = r[j + 1:]
        if run >= min_gap and len(after) >= 3:
            peak = max(after)
            if sum(after[-3:]) / 3 > still_going * peak and peak > 2 * quiet:
                cut = (k + 1 + j + 1) // 2 * WIN + WIN // 2
                return "gap", cut, f"sound after a {run * 10} ms gap, still at {sum(after[-3:]) / 3:.0f} rms at the end"
    end = sum(r[-3:]) / 3
    if end > 0.25 * loud:
        return "abrupt", None, f"ends at rms {end:.0f} (loud level {loud:.0f})"
    return "ok", None, ""


def write_fixed(src, dst, s, cut, fade_ms, ffmpeg):
    s = list(s[:cut] if cut else s)
    n = min(len(s), int(RATE * fade_ms / 1000))
    for i in range(n):
        s[len(s) - n + i] = int(s[len(s) - n + i] * (1 - (i + 1) / n))
    raw = struct.pack(f"<{len(s)}h", *s)
    r = subprocess.run([ffmpeg, "-v", "error", "-y", "-f", "s16le", "-ar", str(RATE), "-ac", "1", "-i", "-",
                        "-c:a", "libmp3lame", "-q:a", "2", str(dst)], input=raw, capture_output=True)
    if r.returncode:
        sys.exit(r.stderr.decode(errors="replace"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder")
    ap.add_argument("--apply", action="store_true", help="write the fixes (otherwise only report)")
    ap.add_argument("--tail", type=float, default=1.5, help="seconds from the end to look at")
    ap.add_argument("--min-gap", type=int, default=3, help="quiet windows (10 ms each) that count as a gap")
    ap.add_argument("--still-going", type=float, default=0.35,
                    help="end level, as a fraction of the late peak, above which the sound is cut off")
    ap.add_argument("--fade", type=float, default=40, help="fade-out in ms on fixed clips")
    ap.add_argument("--cut-gaps", action="store_true",
                    help="also remove a cut-off sound after a gap (default: keep the whole clip and only fade it)")
    ap.add_argument("--ffmpeg", default=DEFAULT_FFMPEG)
    args = ap.parse_args()

    files = sorted(p for p in Path(args.folder).rglob("*.mp3")
                   if "_orig" not in p.parts and not p.name.endswith(".fixed.mp3"))
    counts = {"ok": 0, "gap": 0, "abrupt": 0}
    locked = []
    for p in files:
        s = decode(p, args.ffmpeg)
        verdict, cut, detail = analyse(s, args.tail, args.min_gap, args.still_going)
        counts[verdict] += 1
        if verdict == "ok":
            continue
        rel = p.relative_to(args.folder)
        cut = cut if args.cut_gaps else None
        trimmed = f", trims {(len(s) - cut) / RATE * 1000:.0f} ms" if cut else ""
        print(f"{verdict:6s} {rel}: {detail}{trimmed}")
        if args.apply:
            orig = p.parent / "_orig"
            orig.mkdir(exist_ok=True)
            shutil.copy2(p, orig / p.name)
            tmp = p.with_suffix(".fixed.mp3")
            write_fixed(p, tmp, s, cut, args.fade, args.ffmpeg)
            try:
                tmp.replace(p)
            except PermissionError:           # open in a player or another program: leave it, say so
                tmp.unlink(missing_ok=True)
                locked.append(rel)
                print(f"       in use by another program, skipped: {rel}")
    print(f"{len(files)} clips: {counts['ok']} clean, {counts['gap']} cut-off sound, {counts['abrupt']} abrupt end"
          + ("" if args.apply else "  (report only; add --apply to fix)"))
    if locked:
        print(f"{len(locked)} clip(s) were in use; close the program playing them and run again")


if __name__ == "__main__":
    main()
