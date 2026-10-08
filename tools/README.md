# Tools

## Build

`assemble_mod.py` builds the two mod folders and zips (default and LoversLab edition) from the xmake builds. It is part of
the project; see the comments at the top of the file.

## Voice tools: unsupported, use at your own risk

The five scripts below are side projects for people who make their own voice packs. **They are not part of OSIS, are not
in the mod you download, and are completely unsupported.** No help is offered with them, no issue about them will be
answered, nothing is promised to keep working (a game, mod, library, ffmpeg or web-service update can break any of them
without notice), and they come with no warranty of any kind (see the disclaimer in `LICENSE`).

### Everything you make with them is yours, and so is every consequence

Nothing in this repository gives you any right to any audio, and this project claims no rights in, and takes no part in,
anything the scripts read or write. Whatever files they create are your own, and you take **full and sole responsibility**
for making, keeping, sharing, publishing or selling them, including:

- **Where the audio comes from.** Game and mod voice lines belong to their publishers, voice actors and mod authors. This
  project has no licence to give you to reuse them. If you feed these tools copyrighted material, or material from any
  source you could not defend using, the consequences are yours alone.
- **Whose voice it is.** Do not copy, clone or imitate a real person's voice without their permission. The services that
  generate speech forbid it, and the law in many places does too. Do not use these tools to depict or imitate a minor, or
  anyone who has not agreed, in anything sexual or otherwise.
- **The service you send it to.** `elevenlabs_voice_batch.py` sends text to ElevenLabs under your own API key, account,
  quota and costs. What you ask it to make must be allowed by their terms and prohibited-use policy; read them.
- **The law and ethics of what you create,** wherever you live and wherever you share it.

If you use these tools on copyrighted or ethically questionable sources, or to make anything that does harm, that is
solely on you. The authors and contributors accept no liability for it, or for any loss or damage of any kind that comes
from running the scripts. If you do not accept that, do not use them.

Nothing extracted or generated is ever part of this repository: the output folders `voices_common/` and `voices_sample/`
are git-ignored. Do not add audio to the repository, and do not put it in a mod you release unless you hold the rights.

### What each one does

All are plain Python 3 scripts run from the repository root, for example `python tools/fix_clip_tails.py --help`.
Defaults for the game, mod and ffmpeg folders are the author's own paths; pass your own with the options.

| Script | What it does | Needs |
|---|---|---|
| `extract_common_voice_lines.py` | Reads the Skyrim SE voice archives (and loose `sound/voice` folders) directly, finds the dialogue lines that every adult voice type has in common, and writes one WAV per voice type with 1.5 s between lines, optionally split into parts of about 9 MB, plus a timestamp list for each. `--list` only reports the counts. | Python 3, `pip install lz4`, ffmpeg |
| `voice_line_transcript.py` | Looks up the spoken text of those common lines in the game plugins' dialogue records and writes a transcript in the same order as the WAVs. | The above |
| `extract_character_voice.py` | For a mod's custom voiced characters (voice types vanilla Skyrim lacks), takes about 15 minutes of lines spread evenly across each character's range and writes them back in their original order as WAV parts, each with a `.txt` of start times, lengths, file names and, where the plugin has it, the spoken text. | Python 3, `pip install lz4`, ffmpeg, and the two scripts above in the same folder |
| `elevenlabs_voice_batch.py` | Sends a catalogue of 58 short non-verbal sound descriptions (breaths, moans in four speed stages, climax, reactions, muffled sounds) to the ElevenLabs text-to-speech API with a voice id you supply, and saves one MP3 per sound under a folder per category. Skips files that already exist, so an interrupted run can be restarted; `--dry-run` lists what it would send and the characters it would use. The key is read from the `ELEVENLABS_API_KEY` environment variable, never from the command line. | Python 3 (standard library), an ElevenLabs account and API key, which you pay for |
| `fix_clip_tails.py` | Scans a folder of MP3 clips for a stray sound cut off at the end, or an abrupt loud end, and reports it. With `--apply` it fades the end out (or, with `--cut-gaps`, cuts at the gap), moving each original into an `_orig` folder beside it. | Python 3, ffmpeg |

None of them touches the game, the plugin or a mod unless you point it at one: they read the files you name and write to the
folder you name. The one that changes files in place is `fix_clip_tails.py --apply`, which rewrites the clips in the folder
you give it and keeps the originals in `_orig`.
