#!/usr/bin/env python3
# UNSUPPORTED helper, not part of the OSIS mod. Read tools/README.md first: what you make with it, and every consequence, is yours alone.
"""Generate every sound an ideal OStim female voice pack needs from one ElevenLabs voice.

Each sound is requested as  ". [sound description] ..."  (a full stop, a bracketed audio tag, a trailing ellipsis; the
eleven_v4 model reads the tag as a direction, not words).  The catalog below covers the four OSSO
speed stages, the climax, spank reactions and the muffled (mouth-occupied) sounds.

    set ELEVENLABS_API_KEY=...                       (never put the key in the script or on a command line)
    python tools/elevenlabs_voice_batch.py --voice-id <id> --out "E:/Sounds/MyVoice" --dry-run
    python tools/elevenlabs_voice_batch.py --voice-id <id> --out "E:/Sounds/MyVoice" --takes 2

Files land in <out>/<category>/NN_slug[_tN].mp3.  Already-generated files are skipped, so a run that
stops (quota, network) can simply be started again.  Standard library only.
"""
import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

API = "https://api.elevenlabs.io/v1/text-to-speech/{voice}?output_format={fmt}"

# category -> sound descriptions.  stage0..stage3 are OSSO speed levels, slowest to fastest.
CATALOG = {
    "stage0": [
        "during sex, soft breathy sigh of pleasure",
        "during sex, quiet gentle moan",
        "during sex, low contented hum of pleasure",
        "during sex, soft surprised inhale, then a gentle moan",
        "during sex, slow warm exhale with a faint moan",
        "during sex, shy moan with lips pressed together",
        "during sex, relaxed sensual sigh",
        "during sex, tender breathy whimper",
        "during sex, soft 'mmm' of enjoyment",
        "during sex, light happy moan followed by a quiet breath",
    ],
    "stage1": [
        "during sex, steady pleasured moan",
        "during sex, moan rising slightly in pitch",
        "during sex, two rhythmic moans in a row",
        "during sex, breathy moan with a catch in the throat",
        "during sex, deeper throaty moan",
        "during sex, pleasured gasp followed by a moan",
        "during sex, moan with a short shaky exhale",
        "during sex, soft rhythmic moans keeping time",
        "during sex, stifled moan, biting her lip",
        "during sex, long drawn-out moan of pleasure",
    ],
    "stage2": [
        "during sex, loud breathless moaning",
        "during sex, rhythmic moans getting louder",
        "during sex, gasping moans with rapid breath",
        "during sex, sharp cry of pleasure",
        "during sex, trembling voiced moan",
        "during sex, panting and moaning",
        "during sex, desperate needy moan",
        "during sex, high-pitched moaning cry",
        "during sex, ragged breathless moan",
        "during sex, rhythmic gasping 'ah, ah, ah'",
    ],
    "stage3": [
        "during sex, fast rhythmic loud moaning, close to the edge",
        "during sex, frantic panting moans",
        "during sex, rapid cries of pleasure building",
        "during sex, loud breathless sobbing moans",
        "during sex, intense shuddering moan with a high cry",
        "during sex, wild uninhibited moaning",
        "during sex, long trembling moan at the edge of climax",
        "during sex, rapid gasping moans, voice breaking",
        "during sex, urgent desperate cries",
        "during sex, pleading moan on the brink of orgasm",
    ],
    "climax": [
        "during sex, explosive loud orgasm cry",
        "during sex, long shuddering orgasm moan",
        "during sex, high-pitched orgasmic scream that breaks into gasps",
        "during sex, deep throaty orgasmic groan",
        "during sex, orgasm with rhythmic pulsing gasps",
        "during sex, silent gasp, then a trembling orgasmic moan",
        "during sex, loud sustained orgasm with a final sigh",
        "during sex, overwhelmingly powerful orgasm, a sex cry, heavy panting",
    ],
    "spank": [
        "during sex, startled yelp of surprise",
        "during sex, surprised gasp, then a moan",
        "during sex, sharp inhale and a playful squeal",
        "during sex, pleasured yelp",
    ],
    "muffled": [
        "during sex, muffled moan through closed lips",
        "during sex, muffled hum of pleasure, mouth occupied",
        "during sex, muffled breathy moan through the nose",
        "during sex, muffled rising moan, mouth occupied",
        "during sex, muffled whimper, mouth occupied",
        "during sex, muffled contented humming",
    ],
}


# A short spoken sound after the tag, so the model has something to voice that matches the direction.
INTERJECTION = {
    # stage0
    "during sex, soft breathy sigh of pleasure": "Haahh...",
    "during sex, quiet gentle moan": "Mmm...",
    "during sex, low contented hum of pleasure": "Mmmmm...",
    "during sex, soft surprised inhale, then a gentle moan": "Oh... mmm...",
    "during sex, slow warm exhale with a faint moan": "Hahh... mmm...",
    "during sex, shy moan with lips pressed together": "Mm... mm...",
    "during sex, relaxed sensual sigh": "Ahhh...",
    "during sex, tender breathy whimper": "Hnn... ah...",
    "during sex, soft 'mmm' of enjoyment": "Mmm... mm-hm...",
    "during sex, light happy moan followed by a quiet breath": "Mmm... ahh...",
    # stage1
    "during sex, steady pleasured moan": "Mmm... ahh...",
    "during sex, moan rising slightly in pitch": "Mmm... mmmh... ah!",
    "during sex, two rhythmic moans in a row": "Ahh... ahh...",
    "during sex, breathy moan with a catch in the throat": "Ah... hh... ahh...",
    "during sex, deeper throaty moan": "Mmmhh...",
    "during sex, pleasured gasp followed by a moan": "Ah! ...mmm...",
    "during sex, moan with a short shaky exhale": "Mmm... hahh...",
    "during sex, soft rhythmic moans keeping time": "Mm... mm... mm...",
    "during sex, stifled moan, biting her lip": "Mmph... mm...",
    "during sex, long drawn-out moan of pleasure": "Mmmmmmm... ahhh...",
    # stage2
    "during sex, loud breathless moaning": "Ahh... ahh... oh...",
    "during sex, rhythmic moans getting louder": "Ah... ah... ah! Ah!",
    "during sex, gasping moans with rapid breath": "Ah! Hah... ah! Hah...",
    "during sex, sharp cry of pleasure": "Ah! Oh!",
    "during sex, trembling voiced moan": "Oh... ohhh...",
    "during sex, panting and moaning": "Hah... hah... mmm... ah...",
    "during sex, desperate needy moan": "Mmmh... ahh... please...",
    "during sex, high-pitched moaning cry": "Aaah! Ahh!",
    "during sex, ragged breathless moan": "Hah... ahhh... hah...",
    "during sex, rhythmic gasping 'ah, ah, ah'": "Ah, ah, ah, ah!",
    # stage3
    "during sex, fast rhythmic loud moaning, close to the edge": "Ah! Ah! Ah! Ahh!",
    "during sex, frantic panting moans": "Hah! Hah! Ah! Hah!",
    "during sex, rapid cries of pleasure building": "Ah! Oh! Ah! Ohh!",
    "during sex, loud breathless sobbing moans": "Ahh... hah... ahh!",
    "during sex, intense shuddering moan with a high cry": "Ohhh... Aaah!",
    "during sex, wild uninhibited moaning": "Ahhh! Ohh! Mmm! Ahh!",
    "during sex, long trembling moan at the edge of climax": "Ohhhh... ahhhh...",
    "during sex, rapid gasping moans, voice breaking": "Ah! Ah! Ahh! Ah...",
    "during sex, urgent desperate cries": "Ah! Oh! Ahh!",
    "during sex, pleading moan on the brink of orgasm": "Ohhh... ahh... ahhh!",
    # climax
    "during sex, explosive loud orgasm cry": "Aaahhh!",
    "during sex, long shuddering orgasm moan": "Ohhhh... ahhhh...",
    "during sex, high-pitched orgasmic scream that breaks into gasps": "Aaaah! Ah! Ah!",
    "during sex, deep throaty orgasmic groan": "Ohhhh... mmmhh...",
    "during sex, orgasm with rhythmic pulsing gasps": "Ah! Ah! Ah! Ahhh...",
    "during sex, silent gasp, then a trembling orgasmic moan": "Hhh... ahhhh...",
    "during sex, loud sustained orgasm with a final sigh": "Aaaahhh... haahh...",
    "during sex, overwhelmingly powerful orgasm, a sex cry, heavy panting": "Aaah! ...hah... hah...",
    # spank
    "during sex, startled yelp of surprise": "Yip!",
    "during sex, surprised gasp, then a moan": "Ah! ...mmm...",
    "during sex, sharp inhale and a playful squeal": "Ooh! Hee!",
    "during sex, pleasured yelp": "Ah! Mmm!",
    # muffled
    "during sex, muffled moan through closed lips": "Mmmph...",
    "during sex, muffled hum of pleasure, mouth occupied": "Mmmm... mm...",
    "during sex, muffled breathy moan through the nose": "Mmh... hnn...",
    "during sex, muffled rising moan, mouth occupied": "Mm... mmm... mmmh!",
    "during sex, muffled whimper, mouth occupied": "Mmh... hnn...",
    "during sex, muffled contented humming": "Mmm-mmm... mmm...",
}
assert {d for c in CATALOG.values() for d in c} == set(INTERJECTION), "interjection list out of step with CATALOG"


def prompt(desc, style="web"):
    """web: ". [description] ..."  (what gives complete clips on the ElevenLabs website)
    rich: adds "during sex" and a spoken interjection after the tag."""
    if style == "web":
        return f". [{desc}] ..."
    said = INTERJECTION[desc].rstrip()
    if not said.endswith("..."):
        said += "..."
    return f". [{desc} during sex] {said}"


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")[:48]


def request(url, key, body, retries=5):
    data = json.dumps(body).encode()
    for attempt in range(retries):
        req = urllib.request.Request(url, data, {"xi-api-key": key, "Content-Type": "application/json",
                                                 "Accept": "audio/mpeg"})
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            detail = e.read().decode(errors="replace")[:400]
            if e.code in (429, 500, 502, 503, 504) and attempt < retries - 1:
                time.sleep(2 ** attempt * 2)
                continue
            raise RuntimeError(f"HTTP {e.code}: {detail}")
        except urllib.error.URLError as e:
            if attempt < retries - 1:
                time.sleep(2 ** attempt * 2)
                continue
            raise RuntimeError(str(e))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--voice-id", required=True)
    ap.add_argument("--out", required=True, help="folder to fill (one subfolder per category)")
    ap.add_argument("--style", choices=("web", "rich"), default="web",
                    help='web: ". [description] ..." (default); rich: also "during sex" and a spoken interjection')
    ap.add_argument("--model", default="eleven_v4", help="needs audio-tag support (default eleven_v4, the website's current model)")
    ap.add_argument("--takes", type=int, default=1, help="generations per description")
    ap.add_argument("--only", help="comma list of categories to generate (default: all)")
    ap.add_argument("--stability", type=float, default=0.5)
    ap.add_argument("--similarity", type=float, default=0.75)
    ap.add_argument("--speed", type=float, default=1.0)
    ap.add_argument("--format", default="mp3_44100_128")
    ap.add_argument("--jobs", type=int, default=2, help="parallel requests (free plans allow 2-3)")
    ap.add_argument("--dry-run", action="store_true", help="list what would be generated and the characters used")
    args = ap.parse_args()

    cats = args.only.split(",") if args.only else list(CATALOG)
    bad = [c for c in cats if c not in CATALOG]
    if bad:
        sys.exit(f"unknown categories {bad}; choose from {list(CATALOG)}")
    out = Path(args.out)
    jobs = []
    for cat in cats:
        for i, desc in enumerate(CATALOG[cat], 1):
            for t in range(1, args.takes + 1):
                name = f"{i:02d}_{slug(desc)}" + (f"_t{t}" if args.takes > 1 else "") + ".mp3"
                jobs.append((cat, prompt(desc, args.style), out / cat / name))
    todo = [j for j in jobs if not j[2].exists()]
    chars = sum(len(j[1]) for j in todo)
    print(f"{len(jobs)} sounds in {len(cats)} categories, {len(jobs) - len(todo)} already done, "
          f"{len(todo)} to generate (~{chars} characters)")
    if args.dry_run:
        for cat, text, path in todo:
            print(f"  {cat:8s} {text}")
        return

    key = os.environ.get("ELEVENLABS_API_KEY")
    if not key:
        sys.exit("set the ELEVENLABS_API_KEY environment variable")
    url = API.format(voice=args.voice_id, fmt=args.format)
    settings = {"stability": args.stability, "similarity_boost": args.similarity, "speed": args.speed}

    def run(job):
        cat, text, path = job
        audio = request(url, key, {"text": text, "model_id": args.model, "voice_settings": settings})
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(audio)
        return job

    failed = 0
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [(j, pool.submit(run, j)) for j in todo]
        for n, (j, f) in enumerate(futures, 1):
            try:
                f.result()
                print(f"[{n}/{len(todo)}] {j[0]}/{j[2].name}")
            except Exception as e:
                failed += 1
                print(f"[{n}/{len(todo)}] FAILED {j[0]}/{j[2].name}: {e}")
                if "HTTP 401" in str(e) or "quota" in str(e).lower():
                    print("stopping: fix the key or quota, then run again to continue")
                    pool.shutdown(wait=False, cancel_futures=True)
                    break
    (out / "manifest.json").write_text(json.dumps(
        {"voice_id": args.voice_id, "model": args.model, "settings": settings,
         "sounds": {c: [prompt(d, args.style) for d in CATALOG[c]] for c in cats}}, indent=2), encoding="utf-8")
    print("done" if not failed else f"{failed} failed; run again to retry them")


if __name__ == "__main__":
    main()
