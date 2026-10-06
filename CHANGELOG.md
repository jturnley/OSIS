# Changelog

## 2.0 beta 2 (plugin version 2.0.5) - in progress

### Fixed
- **Repeated orgasms held one actor's climax face for minutes.** In the 2.0 beta 1 probe Auri orgasmed every
  7-14 s and her face stayed on the climax pose with the head thrown back from 14:08:05 to the end of the
  recording, two minutes. The climax window was the thread's, not the actor's: five ticks (about 14 s) from the
  latest orgasm of anyone, re-armed by each one, so a run of orgasms closer together than that never let it end.
  The same shared window gave a partner the climax face at 91 excitement 11 s before their own orgasm (Test 6),
  and the shared afterglow gave a partner who had not climaxed an afterglow face (Test 6 at 23 excitement).
  Climax and afterglow are now **per actor and timed by the clock**: an actor's own orgasm holds their climax
  face for its length and starts their own afterglow, and nothing a partner does starts either.
- Simulated on the gaps from that recording (32, 16, 14, 10, 15 s, then every 7 s): a rapid clip lasts 9.1, 8.0,
  7.0, 5.2, 7.7 s and then 3.5 s for the 7 s cadence (long form), or 4.9, 4.9, 4.9, 3.6, 4.9 and 2.5 s (short form),
  never more than half the gap, so the face always has the other half to come back down,
  where it used to run through.

### Added
- **Faces shaped by the time between orgasms and their number.** An orgasm that comes within the rapid window
  (default 40 s) of the actor's last is "rapid", by how close it is: its climax face is shortened by up to 60%
  (and never to more than half the gap), it skips the squeeze at the start (the face is still
  tight), the afterglow is skipped or cut short (and any afterglow ends as soon as excitement is back above 45),
  and the face between orgasms carries a tension, squint and brows drawn in, that grows with each orgasm in the
  run (0.12 each, up to 0.35) and fades over the window. An orgasm outside the window is a fresh one.
- **A pool of fifteen climax faces, each a timed clip in four forms.** The climax used to be one template (the
  same open mouth, squint and brows) held for the whole orgasm. Each orgasm now picks one of fifteen faces, never
  the same as the one before: gasp, cry out, clenched, lip bite, silent, wide-eyed, eyes rolled, smiling, intense,
  long moan, overwhelmed, pained, breathless, snarl, laughing. They differ in eyes (shut tight, narrowed, rolled
  up, wide), mouth (stretched, round, parted, clenched, pressed) and brows (knitted, lifted, lowered), with a mood
  to match. The pick is weighted by the actor's personality (a stoic actor is most often silent, clenched or
  breathless; a bold one long moan, cry out, smiling or eyes rolled; a shy one lip bite, wide-eyed or pained; a
  fierce one intense, snarl or clenched), and a rapid run favours the overwhelmed ones (gasp, clenched,
  overwhelmed, pained) over the calm ones.
- **Long and short, standard and rapid.** The orgasm is also long or short, and standard or rapid, which gives
  each face four clips (60 in all). The length is the setting for a long standard orgasm (14 s), 45% of it for a
  short one, 65% of it for a rapid long one and 35% for a rapid short one, the rapid ones never more than half of
  the gap since the last orgasm (a 7 s cadence spent about 60% of the time on the climax with the earlier cap of
  60-75%). Long is likelier for a bold actor, after a long hold at the edge, and for faces
  that suit it (long moan, smiling, eyes rolled); a rapid orgasm is likelier short the closer it comes to the last.
- **What a clip does over its length.** A standard orgasm starts with a squeeze (the eyes screwed up, brows in),
  reaches the full pose, holds it, then ripples through the aftershocks (three ripples in the long form, none in
  the short, the depth of the ripple a trait of the face: a laugh or an overwhelmed face shakes, a silent one
  hardly moves) and eases to where that face settles: loosening (gasp, cry out, lip bite, long moan, pained),
  smiling (wide-eyed, smiling, laughing), slack (silent, eyes rolled, breathless) or staying tight (clenched,
  intense, overwhelmed, snarl). A rapid orgasm skips the squeeze, since the face is still tight, and eases only
  to a held tension, because the next orgasm is already coming.
- **Eyes that close, from a languid blink to squeezed hard shut.** Skyrim's face has two eyelid channels: Squint
  narrows the eyes, but only Blink closes the lids, and OSIS had never written Blink (so that actors keep blinking
  on their own). Each of the fifteen faces now has an eye style, and Blink is written only while a face holds the
  lids shut and handed back once they are at rest, so natural blinking carries on the rest of the time. The
  styles, in order of how far they close: **open** (wide-eyed, intense, snarl), **languid** (silent, breathless:
  slow blinks, down over a second, held, up again, about every 3.4 s), **heavy** (cry out, eyes rolled: the lids
  held half down and drifting), **closed** (smiling, long moan: shut and still), **flutter** (overwhelmed, laughing:
  shut with the lids trembling), **tight** (lip bite, pained: shut with a squeeze) and **hard** (gasp, clenched:
  squeezed as hard as it goes, brows in and down). The lids stay as they are until the face starts to settle and
  open with it, and a rapid clip never opens them fully, since the next orgasm is already coming. The lids are
  not scaled by the Strength setting or by personality: a face that shuts its eyes shuts them. The lids ease
  to their target in 0.35 s, not the pose's 0.8 s: in the 2.0.3 test a hard squeeze wrote 1.00 and rendered
  only 0.79 before a short clip began to open it again. The probe line shows `blink ours / rendered`.
- **A face update every 0.8 s during an orgasm,** not every three seconds, so a clip of 4-14 s is played in
  steps and not as two or three jumps. The tick after an orgasm is pulled forward to match. Back to the normal
  cadence when the climax ends. The face label in the probe names the clip, `Climax/lip_bite/rapid-short`, and
  the personality and orgasm count, with the time since the last, are on the probe's beat line.
- Settings (Face page, Director): **Vary the climax face** (`bClimaxPool`, on; off gives the one built-in
  template back), **Climax face length** (`fClimaxSeconds`, 14 s, for an orgasm on its own) and **Rapid orgasm
  window** (`fRapidOrgasmSeconds`, 40 s).

### Notes
- Consensual scenes only: the victim's reaction in a non-consensual scene keeps its own template.
- The eye channels are scaled by Eye strength (0.45 by default) and capped at the realistic style, so how
  different the eyes look between variants depends on those settings; the mouth, brows and mood carry most of
  the difference at the defaults.
- The values are authored here from how an orgasm face is described anatomically, not copied from OStim's
  expression files; the fifteen faces are a table at the top of `Face/Director.cpp` (`kClimaxVariants`: the peak
  pose, how it settles, how much it ripples and how likely it is to run long), and the four timelines are in
  `ShapeOf`.
- The face is updated about every 0.8 s during an orgasm, so a languid blink is played in steps eased over that time,
  not frame by frame, and the flutter is a slow tremor rather than a fast one. The squint of several faces was
  lowered now that the lids themselves close.
- Not yet run in the game.

## 2.0 beta 1 (plugin version 2.0.0)

The 1.9.7 code level, released as a beta. Everything in the 1.x entries below is in it; this is what the beta
is about.

### What it is
- **A face Director that plays OStim's own expressions.** OStim's expression files are read at startup (every
  installed pack applies), and the Director picks from the pool OStim would use for what each actor is doing -
  including the oral and kiss override pools and the stimulation pool for scenes that name no act - and blends
  from pick to pick instead of snapping. Build-up, plateau, climax and afterglow all come from the pools. OStim's
  own face writer is switched off for the actors OSIS paints (1.8.0-1.9.3).
- **Personalities that stay put.** Each actor's personality is worked out the first time a scene needs it and
  kept in the save, and it weights which expression is picked (1.9.3, 1.9.4). Settable per NPC and for the
  player on the Personality page.
- **PPA integration.** The mouth, and where the head points, are handed to PPA while it plays its preset on a
  blowjob, with a check that it really is doing so; OSIS reads PPA's override configs and, where PPA's preset
  would zero the eyes and brows, can write and remove an override of its own from the menu (1.9.5-1.9.7).
- **A face that does not flicker or fight.** The double mouth, inverted climax mouth, mood leaks, OStim/OSIS
  write fights and mid-run resets of 1.7.x are fixed, found with the face probe (1.7.1-1.7.11).
- **Nothing to reset.** `OSIS.ini` and `morphs.json` are created by the plugin on first run and completed on
  update, so installing a new version keeps what you chose (1.7.10).

### Known issues
- Orgasms in quick succession keep the climax face on until they stop (Auri's face stayed on the climax pose for
  two minutes at an orgasm every 7-14 s). Planned for beta 2: a timed per-actor climax, recovery faces between
  orgasms, and faces shaped by orgasm count and the time since the last one.
- A one-frame blink of eyes, brows and mood can show when a scene moves to a new animation node. Not caused by
  PPA (it also zeroes the mood, which PPA's preset does not touch); the source has not been identified.
- PPA logs `Actor X isn't managed by havok?` and stops moving that penis for a few seconds now and then (about
  40 s, 0 s and 12 s in three test runs). Nothing in the OSIS or OStim logs lines up with it; the cause is not
  known.
- An actor at 90+ excitement can show the climax face a few seconds early while a partner's orgasm is still in
  progress.

### Not yet in
- Faces shaped by circumstances: first time, exhibition, relationship level, experience, time since last sex
  (the Director's goals 3 and the OVirginity first-time face).
- Handling for repeated orgasms (above).

### Testing it
- Install over a previous OSIS build or fresh; start a new save for testing. Send
  `Documents/My Games/Skyrim Special Edition/SKSE/OSIS.log` with a report. For a face problem press "Probe faces"
  on the Face page first and play for a minute: the log then says what OSIS wrote against what the game rendered,
  second by second.
- With PPA installed, add `AccuratePenetration.log` from the same session.

## 1.9.7

### Added
- **OSIS can take the eyes and brows back from PPA, and give PPA back as it was, from its own menu.** When the
  mouth preset PPA plays on a blowjob zeroes the eyes, brows or mood (`OverrideModifiers` /
  `OverrideExpressions` - see 1.9.6), Face > Mouth now offers **Keep OSIS's eyes and brows**. It writes
  `Data/SKSE/Plugins/ppa-override-configs/00_OSIS_PPA_Face.toml`: `Inherits = "Any"` and a copy of the winning
  preset, effects and all, with those two switches off and a `Priority` one higher. PPA appends the presets of an
  `Inherits = "Any"` override to its pool and the highest `Priority` wins, so it keeps the mouth phonemes and
  any morphs of the original preset and OSIS keeps the face. **Restore PPA's own setting** deletes that file,
  and PPA is back exactly as the other mods configure it.
- It never writes to another mod's file. That is deliberate: under Mod Organizer a write to a mod's file made
  from inside the game lands as a copy in the overwrite folder, which shadows the mod from then on (including
  after the mod is updated) and cannot be cleanly undone from in-game. A file OSIS created itself can be
  deleted. OSIS also refuses to overwrite or delete a file of that name that does not carry its marker line
  (`# OSIS-PPA-FACE v1`).
- The copy keeps the original's `Targets`, `Contexts`, size and smoothing settings as written (including a
  `Targets` array spread over several lines); it only changes the two switches and the `Priority`.
- PPA reads its configs at startup and on its reload key (F5 by default), so the menu says to restart or press
  it. The status line says when OSIS changed its override this session.

### Notes
- The generated file was checked against a real TOML parser on two sample presets (the SMP Head one and a
  base-config one with a multi-line `Targets` and no `Priority`): valid TOML, effects and targets identical to
  the original, `Priority` set once, both switches off, nothing from the neighbouring sections copied in.
  It has not been run in the game yet.
- The preset's source file (`0SMP_Head_PPA.toml`) is untouched, so a mod update, or removing OSIS's file, leaves
  nothing behind.

## 1.9.6

### Found in the 1.9.5 test
- **The mouth hand-over works** (three hand-overs on blowjob nodes, each confirmed within 0.5-1.1 s; PPA held
  phoneme 0 at 0.95 and OSIS wrote nothing to the mouth). **The eyes and brows did not survive it**: during a
  blowjob OSIS wrote brow-up 0.5-0.97 and squint 0.3-0.5 and the rendered values were 0.00 on all four
  channels for the whole 75 s, with all of them dropping to zero in a single frame when the hand-over began.
  The cause is `OverrideModifiers = true` in the mouth preset that ships with `[Predator] SMP Head`
  (`ppa-override-configs/0SMP_Head_PPA.toml`, priority 999999): PPA zeroes every eye and brow channel its preset
  does not set. The measured phoneme change (3.69 both times) matches that preset's phonemes (sum 3.7), not
  the base config's (5.18). OSIS only read the base config, so it could not see any of this.

### Changed
- **OSIS now reads PPA's override configs too** (`Data/SKSE/Plugins/ppa-override-configs/*.toml`, as well as
  `accurate-penetration.toml`) and works out which mouth preset wins - the highest `Priority`, as PPA does,
  with a preset that has no `Targets` counting as matching the mouth. The hand-over check and the status line
  now describe the preset that really plays: `PPA: installed, 2 facial preset(s) for the mouth across 1 override
  file(s); the one that plays is in 0SMP_Head_PPA.toml (priority 999999), it sets the mouth phonemes`. The
  files are watched, so a change to any of them is picked up at the next scene start.
- **A warning when that preset takes the eyes and brows.** If the winning mouth preset has `OverrideModifiers`
  (eyes and brows) or `OverrideExpressions` (mood) on, OSIS logs a warning at startup naming the file and the
  setting, and the Face > Mouth page shows it under the PPA line. Setting it to `false` in that file keeps
  OSIS's eyes and brows during a blowjob; the preset's phonemes and morphs are unaffected. OSIS does not try to
  out-write PPA: the zeroing won every frame in the test, so the setting is the way to keep the face.

### Notes
- Only the highest-priority mouth preset is considered. Overrides that apply to particular races or bodies
  are not told apart, so a lower-priority preset that would win for some other actor is not reported.
- The same run showed PPA tracking the penis through the kneeling blowjob, facefuck and 69 nodes (tip on the
  path, offset at most 0.62). The earlier stretches where PPA logged `Actor Test 6 isn't managed by havok?` and
  stopped tracking did not recur.

## 1.9.5

### Added
- **A blowjob's mouth is handed to PPA while PPA is playing its mouth preset** (Procedural Penis
  Animations / Penetration Physics). With a penis in an actor's mouth PPA plays its own facial preset on
  them - in the default config the phonemes that open the mouth (0, 1, 5, 6, 7, 9), with every other
  phoneme zeroed (`OverridePhonemes = true`, priority 99999). Since 1.9.0 the Director played OStim's
  open-mouth pool on that same mouth and, with the mouth no longer counted as "yielded", turned the head
  and aimed the gaze as well, so two writers shared the mouth and something was moving the head the
  animation had placed. For whoever is giving a blowjob or deepthroat, in Director mode with the
  expression library on, the Director now stops writing the phonemes, stops playing the open-mouth
  override, stops the look-at and the head flow, and leaves the mouth to PPA. Eyes, brows and mood are
  still ours. New setting `bYieldMouthToPPA` (default on), with a checkbox and a PPA status line under
  Face > Mouth.
- **It checks that PPA is really doing it.** PPA does not say which actors it is working on, so it is
  seen from the face: when the mouth is handed over the phonemes stop moving, and a change in them
  (0.6 summed over the 16) is PPA at work. If nothing changes in 6 seconds, PPA is not driving this
  mouth (a scene it does not recognise, or the penis out of its range) and the Director takes the mouth
  back and plays the open-mouth pool as before, offering it to PPA again after 15 seconds and at every
  new scene node. One of our own moan clips finishing is not mistaken for PPA. The log says which:
  `PPA: X gives a blowjob; the mouth is PPA's ...`, then either `PPA: is driving X's mouth (...)` or
  `PPA: nothing moved X's mouth in 6 s; it is not driving it here ...`.
- PPA is found by its module name and its `accurate-penetration.toml` is read as text (reread when it
  changes, at each scene start): the hand-over only happens when PPA's expression system is on and a
  `[[FacialPreset]]` targets the Mouth. If you delete that preset PPA stops opening mouths and OSIS keeps
  the mouth. The startup log line `PPA: ...` says what was found.

### Notes
- This hands over what PPA writes to the face and where the head points. It cannot align a scene PPA
  does not know: `AccuratePenetration.log` says `Scene for X (OStim) has no tags/context. Waiting...` for
  those, and PPA does nothing on them (no alignment, no mouth). If a blowjob is still misaligned with
  this version, that log from the same run shows whether PPA recognised the scene.
- Only the phonemes are handed over. If a PPA preset also writes expressions or modifiers (a custom
  deep-throat preset), the startup line says so and OSIS still writes those channels.
- Assist and Enhanced modes are unchanged: they already left oral mouths to OStim.

## 1.9.4

### Fixed
- **An actor's personality no longer changes between runs.** In the 1.9.3 test the same actresses had
  different personalities from one run to the next (Camilla bold in one, shy in the next; Serana bold,
  then fierce), and with them different faces, because the personality was recomputed from the rules
  every time it was asked for and the first rule is the SPID distribution - a roll the game makes when
  it loads, not a property of the actor. Each actor's personality is now **pinned the first time a
  scene needs it** and kept in the save game, so it is the same in every scene and after every load.
  The pin records what the rules gave at that moment (SPID keyword, keyword, voice, vanilla AI, or the
  form-id fallback) and logs it: `Personality: Camilla (xxxxxxxx) pinned as Bold (SPID)`.
- **The player now has one too.** The player's personality was whatever the fallback rules happened to
  give each time (Player set -> NPC override -> SPID -> keywords -> voice -> vanilla AI -> form-id
  seed), and with no personality set in the menu the picks were not shaped by one. The player's is now
  worked out once and pinned like everyone else's. A personality chosen in the menu still wins: the
  Player personality setting while it is anything but Auto, and a personality set on an NPC from the
  crosshair.

### Added
- **Work every personality out again** (Personality page): forgets everything OSIS pinned by itself,
  the player's included, so each is settled again from the current rules at its next scene. Use it
  after changing the SPID file or the personality sources. Personalities you set yourself are kept.
- Setting an NPC's personality to Auto from the crosshair now works it out afresh and pins that.

### Notes
- Pins are stored in the cosave beside the personalities you set (a flag on the same entry), capped at
  4096 so a long run of generated NPCs cannot bloat it. Children and creatures are not pinned.
- A save made before this version has only the personalities you set; the rest are pinned as each
  actor is next seen.

## 1.9.3

### Changed
- **Which expression an actor plays now depends on their personality.** In the 1.9.2 test four women
  had different personalities (read from the face strength multipliers and head behaviour: two bold,
  one fierce, one shy), and it showed in how *strong* their faces were - mid-excitement mood 0.88 and
  0.80 for the bold, 0.66 for the shy - but not in *which* faces they played. The pick from the act's
  pool was random: a bold actor spent 59% of her scene on Puzzled and 40% on dSad, a fierce one 56% on
  dFear and 37% on dSad. A pick is now weighted by how well its mood suits the actor's personality:
  bold and fierce actors lean to Happy and Surprise (and Anger, where a pool has it) and away from
  Sad, Fear and Puzzled; a stoic actor leans to Neutral, Happy and Puzzled and away from the
  extremes; a shy actor keeps close to the pool's own mix and favours the vulnerable ones (Happy,
  Puzzled, Fear, Sad, Surprise) over Anger and Disgust. An expression with no mood (brows only,
  mouth only) is weighted 1. It is still OStim's pool - nothing outside it is played - only the
  odds change. On a penetration pool, simulated: Sad 33% for no personality, 11% bold, 8% fierce,
  32% shy; Surprise 34%, 51%, 57%, 32%.
- No personality (the default when nothing is known about the actor) is unchanged: equal odds.

### Notes
- The weights are a first pass and live in `MoodAffinity` in `Face/Director.cpp`.
- The same pick still avoids repeating the last expression.

## 1.9.2

### Fixed
- **The face dropped away just before the peak.** In the 1.9.1 test, Camilla's build-up ran at mood
  0.57-0.96 and mouth 0.57-0.98 (pool faces at 75-90 excitement), then the three plateau beats
  rendered at **mood 0.28, an Anger mood, and mouth 0.09-0.15**: the plateau was still the built-in
  template (Anger 0.4, mouth 0.2), about a third of the strength of what led into it, so "I'm almost
  there" turned into "this is nice". The plateau is now played from the same pool as the build-up,
  with the tension on top (squint +0.18, brows in +0.15).
- **The climax face lasted one beat.** The climax phase was chosen only while `raw >= 90`, and in
  current OStim an actor's excitement resets at the climax (Camilla: 100, then 5 on the next beat).
  That test comes from the Papyrus original, where excitement stayed high through the orgasm. So the
  multi-beat climax choreography - tension, eyes rolling up at the peak, the aftershocks - was
  cut to one beat, and the next beats fell through to the *low-excitement pool* (`penetrated13` at
  excitement 5) for about 25 s until the afterglow began. An actor's own orgasm (the event names
  who climaxed) now holds the climax phase for the whole orgasm window, five beats; an actor who
  did not climax keeps their own phase.

### Notes
- Both are from the first release. The plateau only became visible as a drop once the build-up
  before it was played from the pool at full strength.

## 1.9.1

### Fixed
- **Faces were muted, or absent, in scenes whose data does not say what the actor is doing.** The
  1.9.0 test spent a lot of time in idle, approach and transition nodes (`OARE_Sitting`,
  `OStim2PSittingMF`, `OStim2PStandingApartMF`...) and in Anub's solo scenes, which define no
  actions at all. By OStim's rules those get its `default` pool, the mild idle one, many of whose
  files set a single part (squint only, brows only, mouth only). Measured in that run: rendered mood
  averaged 0.25-0.33 and the mouth 0.25-0.29 on it, against 0.43-0.55 and 0.57-0.80 on an action
  pool, and Serana, whose whole scene was a solo one, never passed a mood of 0.34.
  When the pool would be `default` but the actor is clearly aroused (22 excitement to start, below
  12 to stop) in a node that is neither an idle nor a transition, the Director now borrows the pool
  OStim has for being stimulated - the `femalemasturbation` or `malemasturbation` target pool, by
  sex - instead. The face label says when it has (`Library/stimulated6 (no act in the scene:
  stimulation pool)`). Idle and transition nodes, and anything below that excitement, still get
  `default`.

### Notes
- This is a limit of OStim's own data, not of the player: OStim itself would show those scenes the
  `default` pool too. A scene author who defines the action (and an expression pool exists for it)
  gets that pool, as before.
- Personality still scales the result, so a shy actor is about 30% quieter than a bold one.

## 1.9.0

### Changed
- **In Director, OStim no longer overrides anything.** Until now Director switched OStim's
  underlying expressions off but left its override expressions on, so for an oral or kiss action
  OStim still wrote the mouth (and sometimes the eyes, mood and tongue) over the Director's face and
  the Director stepped aside for the mouth. With the library on, OStim's override expressions are
  switched off as well, and the Director plays them itself:
  - The override pool is the one OStim's own rules give an actor in an oral or kiss action
    (`openmouth` for a blowjob or cunnilingus, `tongue` for licking and French kissing - the receiver
    of cunnilingus has none). A pick is made every 2.5-5 s and applied part by part to a face kept for
    the override, and overlaid on the Director's face for whatever it owns, in any phase. The parts
    the override has are not set by the underlying pool, as in OStim.
  - The tongue: a pick with a `tongue` object (OStim's `phonemeObjects`) puts the tongue out through
    OSIS's own tongue handling, and a pick without one takes it back, so the jaw clearance and the
    lip-sync pause follow it. The anime-tongue and stranded-tongue clean-up leave a tongue that
    belongs to the override alone.
  - The mouth is no longer handed to the animation for an oral act, so lip-sync and the slower oral
    tick no longer step aside either. The probe's mouth label reads `Library override/<file>`.
  - Switching the setting off mid-scene undoes both of OStim's flags (`SetExpressionsEnabled` never
    lifts the override one on its own).
- Our tongue state now follows our own equip and unequip at once, instead of waiting for the next
  poll, which could read as an ahegao mod's tongue for a second and stand the whole face down.

### Notes
- The first second or two of an oral scene starts with a closed mouth until the Director's first
  beat; OStim used to open it at the node change.
- Needs OStim's `tongue` equip object (Halo's HDT Tongues provides one) for the tongue itself.
- Unchanged with the library off: OStim's overrides stay on and the mouth is yielded as before.

## 1.8.3

### Fixed
- **The face was zeroed for a frame at every scene node change.** Found in the 1.8.2 probe: at
  21:55:59, 21:56:02.4 and 21:56:02.7 every channel of one actor - mood, brows, mouth, the keyframe
  as well as the rendered value - went to zero in the same frame, each time 0.35-0.8 s after the
  player moved to a new node (OStim logged `thread 0 changed to node ...` just before each). OStim's
  node change re-applies expressions but does not reset the face, so the reset comes from elsewhere
  (Conditional Expressions, which runs periodic and equip-triggered scripts on the player and
  followers, is installed in two variants; not proven). The 1.7.9 re-assert from the face node's
  update, which puts our values back just before the game reads them, only ran while OStim's face
  was on. It now also runs for a face OSIS owns, putting back whatever we last wrote - phonemes,
  eyes and brows, and the mood - over anything that changed it since, so a reset never reaches the
  screen. The probe counts these, and logs `face reset by something else, N channels ... put back
  before it rendered` for a real reset (four or more channels), which also tells which scene change
  or moment it follows.
- An owned channel is only restored if OSIS wrote it in the latest frame; a mouth yielded to the
  animation is not touched.

### Notes
- The first Director pool test (1.8.2) came out well: Camilla's face moved through 11 of
  `penetrated*` and `stimulated*` expressions as the scene changed from oral to penetration, with
  no flutter (eyes and brows 0.1 jumps per second, no reversals; mouth 0.7, no reversals).
- OStim's own override expressions are still allowed in Director, and one showed in that probe: a
  look-down to 1.0 dropping to zero in one frame on an oral partner. Switching them off, and playing
  the `openmouth` and `tongue` pools ourselves, is the next step.

## 1.8.2

### Changed
- **Director plays OStim's own expression pool for what each actor is doing** in the pleasure and
  anticipation phases of consensual scenes (setting `bDirectorLibrary`, on by default; the Face
  page has a checkbox). The pool is the one OStim's rules give the actor (1.8.0): for the receiver of
  `vulvaleating`, its ten expressions. A pick is made when the pool changes and then every 2.5-5 s.
  Each pick is applied the way OStim applies it - every part it has (mood, lids, brows, eyeballs,
  mouth) is set in full and a part it lacks is left as it was - to a face kept per actor, so what
  shows is the accumulation of recent picks. The output eases to each new target from wherever the
  face is (0.9 s or the Transition setting, whichever is longer). Climax, plateau, afterglow and
  distress keep the built-in grammar, and the act-, relationship- and scenario-specific flavours
  (which the pool makes redundant) are skipped for pool faces. Strength is relative to its default,
  so the default plays the pool at OStim's authored size.
- The beat line and Status page name the pick (`Library/stimulated6`).

### Fixed
- **A lover's face never escalated.** `PleasureTone` returned "tender" for anyone whose partner was a
  lover or ally (rank 3 or more), for the whole scene, and "tender" is a fixed template that does
  not depend on excitement: Happy 0.5, squint 0.3, brow 0.3 at excitement 5 or 95. Camilla, the
  player's partner, sat on it for the entire 1.8.1 test. The same is true of the lust, playful,
  surrender and detached tones. The pool path does not use them; the built-in templates still do
  for the phases that keep them, and need the same rework (a relationship should colour the face,
  not replace it).

### Notes
- Not yet: OStim's override pools (`openmouth`, `tongue`) and OStim's own overrides being switched
  off for Director, the personality and circumstance inputs, and the seamless event envelopes.
- Pools include Fear, Puzzled and the dialogue moods alongside Happy, as OStim's do.

## 1.8.1

### Fixed
- **The receiver of an oral act was treated as the one performing it.** In a two-person scene with
  oral sex, Director gave *both* actors' mouths away to the animation and gave both the face of
  someone concentrating on the act (Neutral mood, squint, no mouth of its own). The person being
  pleasured - Camilla in the 1.8.0 test, receiving cunnilingus from the player - showed Neutral at
  about 0.55 for the whole scene while the actor next door moved through Happy and Surprise. Two
  separate defects, both from the first release:
  1. A fallback meant for scenes whose data says nothing about who does what ("an oral scene with
     two people: assume both mouths are busy") fired even when the scene says exactly who does
     what. It now applies only to an actor the scene's data does not place in any action. The
     scene here has actor 0 performing `cunnilingus` on actor 1, and actor 1 carries no oral tag.
  2. OStim renames some actions through its alias table (`cunnilingus` is `vulvaleating`,
     `lickingvagina` `vulvallicking`, `lickingpenis` `penilelicking`, `deepthroat`
     `deepthroating`, `anilingus` `rimjob`, `rubbingclitoris` `vulvalrubbing`), and OSIS reads a
     scene's actions by the new name. The Director's action lists were written with the old names,
     so for those acts no list lookup ever matched and the fallback above had been covering for
     it. Each list now carries both names.

### Notes
- Found from the 1.8.0 probe: the beat line gave Camilla `mouth 'Oral action', head 'Animation
  head'` while its library line said she is the *target* of `vulvaleating`.

## 1.8.0

Director is being rebuilt to own the whole face: OStim never overrides it, and it plays OStim's
own expression library through OSIS's seamless output instead of OStim's writer. This is the first
step. It reads and resolves the library and changes no behaviour yet.

### Added
- **OStim's expression library, read and resolved by OStim's own rules.** Every file in
  `Data/SKSE/Plugins/OStim/facial expressions` (134 in a typical large install, from OStim itself,
  OStim Community Resource, Night-blooming Violets, Devious Devices NG and others, so installed
  expression packs apply) is parsed into sets, events and per-action pools, with OStim's value
  formula (`base + variance + speed and excitement terms`) kept intact. For each actor in a scene
  node it picks the underlying pool (the scene's named set, else the named action, else the first
  action the actor performs or receives, else `default`) and the override pool (the oral and kiss
  sets, `openmouth` and `tongue`), in OStim's order. The startup log lists what it loaded.
- OStim scene metadata now carries a node's `underlyingExpression`, `expressionOverride` and
  `expressionAction`, and an action definition's per-role `expressionOverride`. Action names are
  resolved through OStim's alias table, which the expression files rely on (`vulvallicking`,
  `penilelicking` and others only match through it).
- The face probe's beat line says which pools OStim's rules give each actor, so the resolution can
  be checked against what is happening in the scene.

### Notes
- Checked against every scene in one install (5,509 scenes, 12,002 actor positions): 79% resolve
  to an action's pool, 21% fall to `default` (idle, transitions, solo), and the oral override sets
  map to the right actions.
- Next: the seamless player, with OStim's overrides switched off for Director and the oral mouth
  and tongue handled by OSIS; then the personality, orgasm-timing and circumstance inputs.

## 1.7.11

### Fixed
- **Director's pleasure faces were about half as strong as OStim's.** Measured: OStim's own
  expression files run mood 0.7-1.0, brows 0.4-1.0 and mouth 0.5-1.0 at 60-100 excitement
  (`value = base + variance + excitement factor`, from OStim's source); Director's pleasure
  presets, as the face probe saw them in 1.7.3, came out at mood 0.4-0.5, brows 0.2-0.4 and mouth
  up to 0.45. The eyelids already matched. The cause is how the grammar was built: the original
  OSED ran on top of OStim's face, with its takeover optional and off by default, so its presets
  were tuned as additions to OStim's face. As the only writer there is nothing under them.
  A new **Pleasure intensity (Director)** setting (`fDirectorGain`, default 1.0) scales mood by
  x1.4, mouth by x1.6 and brows by x1.5 during the pleasure phase, which brings those channels to
  OStim's level at the same excitement. 0 gives the old faces, 2 goes well past OStim. Squint is
  not scaled, and climax, plateau, afterglow and distress are left as authored (the climax already
  exceeds OStim's).

### Notes
- The new key is added to an existing `OSIS.ini` by the startup completion in 1.7.10; the log
  says so.

## 1.7.10

### Changed
- **Updating the mod no longer resets your settings.** The release used to ship `OSIS.ini` and
  `OSIS/morphs.json`, so installing an update over an old one replaced both with the defaults: a
  face mode or a morph table you had set in the menu went back to the default every time. The
  plugin now makes them itself on startup. A missing `OSIS.ini` is written with the defaults and
  the explanatory comments; one from an older version gets any setting the new version introduced
  added to it, with your values, your comments and any key it does not know left alone. A missing
  `morphs.json` is written from the built-in tables (which are identical to the file that used to
  ship: 22 morphs, 6 blush regions, 9 race multipliers); an existing one is never touched.
  `OSIS_Personality_DISTR.ini` still ships, since the plugin does not write it.
- The next install over a copy that still has the old shipped files will replace them one last time.
  Use **Merge**, not Replace, in the mod manager to keep your current settings through it.

## 1.7.9

### Fixed
- **The 1.7.8 re-assert was in the wrong place and did nothing.** It ran just before the game's
  animation update, on the assumption that the game reads the face for rendering inside that
  update. The probe's own check said otherwise: the game's final face values changed inside it in
  0 of 5,440 frames. The snap lines had the signature too - OSIS's value intact in the keyframe
  (0.31) while the rendered value had dropped to 0.10 - meaning the game read OStim's low value
  somewhere between OSIS's write and the next frame's. The game turns the keyframes into the
  values it renders in the face node's own update (`BSFaceGenNiNode::UpdateDownwardPass`), which
  runs after the animation update. OSIS now puts its values back over OStim's from a hook on that
  call, immediately before the read, and the probe measures whether the final values change
  inside it. Only applies while OStim's writer is on; not installed on VR.

### Notes
- Installing a release overwrites `OSIS.ini`, including a face mode changed in the menu. A test
  of 1.7.8 ran in Director mode for that reason and said nothing about the fix.

## 1.7.8

### Fixed
- **The jaw and eyelids flickering about 20 times a second while OStim's face is on.** Found
  with the 1.7.7 probe's frame-by-frame detector: a channel OSIS holds above OStim's value (the
  jaw during a moan, the squint under it) dropped to OStim's much lower value about every 50 ms,
  for one frame, and came back. OStim's updater runs on its own thread at that rate and
  overwrites the face data; OSIS wrote only *after* the game's animation update, so the game read
  the face for the next frame with a whole frame in which OStim could write over it. OSIS now
  also puts its last values back **immediately before** the animation update, over whatever OStim
  wrote in between, so the window is the gap between that write and the game's read.
  Measured before the fix (v1.7.7, Enhanced): Camilla's eyes and brows made 3.8 large jumps and
  2.7 direction reversals per second, her mouth 1.1 and 0.5; the player and Serana were nearly
  still. Only applies while OStim's writer is on; Director mode owns the face and does not need it.

### Added
- The face probe reports how often the game's final face values change *inside* its animation
  update. That is the assumption this fix rests on: if it reads close to zero, the game reads the
  face somewhere else and the fix will not help, and the log says so.

## 1.7.7

### Added
- **The face probe now watches the rendered face frame by frame.** One sample a second could not
  show a flicker or a snap to zero, and 1.7.6 had cut them without removing them. The probe now
  detects, per actor, every frame in which a channel of the rendered face (mouth, eyes and brows,
  mood) falls by more than 0.2 and logs it as a SNAP with the keyframe value and OSIS's own last
  write beside it, so it can be told whether OStim, OSIS or the game did it. Each per-second line
  also gets a flutter summary: how many big frame-to-frame jumps the group's total made and how
  many times it reversed direction.

### Fixed
- The probe's per-second "ours" mouth value could be stale: it reported a channel from an earlier
  frame as if it had just been written. It now only counts channels written in the previous frame.

## 1.7.6

### Fixed
- **Faces snapping back to zero between animation passes, and the mouth stuttering after a
  moan, whenever OStim's own face was on (Enhanced and Assist mode).** OSIS wrote every channel
  it had ever touched on every animation update, zeros included, and OStim's writer was setting
  the same brows, lids and phonemes. Whoever wrote last that frame won, so the face flickered
  between the two. Measured with the face probe: OStim asking for inner brow 0.56-1.00, mouth
  0.35-0.75 and squint 0.5-0.66 while OSIS put 0.15, 0.00 and 0.11 over them. Worse, OStim stops
  rewriting a value once it reaches its goal, so a zero of ours was never corrected.
  Writing is now **layered** while OStim's writer is on: OSIS tracks what the other writer
  last put in each channel and writes the larger of the two, never a bare zero over it, and gives
  a channel back as OStim had it when it stops using it. Where the takeover holds (Director
  mode) OSIS still owns the face outright.

### Notes
- The "never loses a fight with OStim's face updater" design from the first port is right when
  OSIS owns the face and wrong when it shares it. It had been applied to both.
- In Enhanced mode OStim's own moan expression still opens the mouth alongside the lip-sync
  track, since both are running; they overlap rather than alternate now.

## 1.7.5

### Fixed
- **A mood set by OSIS kept overwriting OStim's own for the rest of the scene.** Once any mood
  channel had been used, every animation update wrote all 17 of them. Switching from Director to
  Enhanced or Assist part way through a scene, or a non-consensual shock or break outside
  Director mode, left the last Director mood being rewritten over OStim's expression every frame.
  Found in the face probe's output: a Neutral mood OSIS never chose, held at 0.25-0.43, in a scene
  running in Enhanced mode. The mood is now eased out and left alone whenever the Director is not
  the one driving the face.

### Added
- **Probe faces (3 min)** next to the 30-second one, long enough to take in a climax; the 30 s
  window missed the peak both times. The beat line now says which face mode is running.

## 1.7.4

### Fixed
- **The mouth moved twice for nearly every moan.** Measured with the face probe: 17 of 27 moans
  restarted the same lip-sync clip at almost exactly its own length. A moan that has just
  finished reports its play position back near zero before the audio manager drops it, and
  lip-sync read that as "the audio has fallen behind" and started the envelope again from the
  top. A position jump is now only followed while the sound is genuinely partway through.
  Introduced by the 1.5.3 resync rule; before that, re-deriving the start every tick did the
  same thing.
- **The Director's climax mouth was never shown with default settings.** The preset leaves the
  mouth alone when the breath clock is holding it, but the test was inverted (`!bBreathing`), so
  it left the mouth alone exactly when nothing else was holding it. Its own open mouth at climax,
  and a ring gag's, were thrown away. Present since the first release.

### Notes
- What the probe settled about flat faces: OSIS's writes do reach the rendered face, nothing
  overwrites them, and OStim's face writer is off. The Director's own faces are mild - Happy at
  about 0.4, brows 0.2-0.4, little mouth. The expressive faces from the original release match
  OStim's own climax expression (climax1.json: jaw wide open, squint zero, eyes rolled up,
  partial blinks), which OSIS cannot produce: OStim's writer was running alongside OSIS then. Why
  the takeover did not hold in that build is not established.

## 1.7.3

### Added
- **Probe faces (30 s)** on the Status page. For 30 seconds after the menu closes, OSIS.log gets
  a line per actor per second: the preset the Director chose, what OSIS wrote, what the game
  actually rendered (its final expression, modifier and phoneme values), what the game's own
  dialogue lip-sync contributed, how often anything else changed the face between two of our
  writes, and every lip-sync clip start. It exists because flat faces since 1.6.x have now been
  "fixed" four times on reasoning alone, and none of those fixes was the cause.

### Fixed
- A setting tooltip showed its example texture path with the backslashes stripped out.

## 1.7.2

### Changed
- The OStim face-writer takeover is repeated every two seconds while the actor is ours, instead
  of being sent once. OActor.SetExpressionsEnabled silently does nothing when OStim has not yet
  registered the actor, and the actor used to be marked taken over regardless. The log now says
  when the takeover first happens.

### Notes
- This was shipped as the fix for flat faces and two mouth movements per moan. It was not: with
  1.7.2 the log confirms the takeover for every actor and both symptoms are unchanged. The retry
  stays because the silent-drop window is real, but the cause of those reports is still open.

## 1.7.1

### Added
- Two more afterglow faces: **relief smiling into it** (brows letting go and the mouth going
  with them) and **too tired to do much with it, but still smiling** (heavy lids, a small
  smile). Seven in the pool now, still equally weighted.

## 1.7.0

### Added
- **Afterglow has a pool of faces instead of one.** The settled half-smile it always had, a
  broader smile, relief (brows letting go, a long breath out), tired (heavy lids, gaze down) and
  spent (barely holding the eyes open, no expression left). One per beat and per actor, with
  equal weight.
- **An eye-roll beat in the pleasure arc**: the eyes drift up, lids follow, brows lift with
  them. An ordinary beat, no more likely than its neighbours, held back below excitement 55 so
  it reads as pleasure rather than boredom.
- **Wide eyes at the climax peak**, as if caught out by it: squint released, brows up hard and
  the surprise mood in place of the usual climax mood. Shy actors get this often, everyone else
  now and then.

### Notes
- Skyrim's FaceGen has no eyelid morph: Blink closes the lids and Squint narrows them, and
  nothing opens them past neutral. "Wide" is therefore squint at zero, brows up and the surprise
  mood, whose own morph lifts the lids. Blink is deliberately never written, so actors keep
  blinking naturally through all of this.

## 1.6.2

### Fixed
- **Moans no longer take the mouth away from the expression.** An active lip-sync clip counted
  as the mouth being yielded, so the grammar wrote no mouth at all for its duration. That was
  harmless while lip-sync silently failed on most files; once 1.5.2 made archived moans decode
  and a dense voice pack was installed, a clip was playing on essentially every poll and the
  mouth belonged to the moan for the whole scene - faces went flat. The moan now rides on top of
  whatever face is being worn: it opens the mouth further than the pose, and leaves it alone
  where the pose is already wider.

## 1.6.1

### Fixed
- **A long scene could get stuck in afterglow, and the faces stopped changing.** Afterglow was
  counted in beats with no time limit, and every climax event re-armed it to five. In a scene
  with repeated climaxes it never drained - and because afterglow outranks every other state
  except distress and climax, it pinned the whole thread: on a 125-second scene both actors were
  showing the afterglow face, including one at excitement 9. It now also ends on the clock, 18
  seconds after the climax that set it, whatever the beats are doing.

### Changed
- The build stamps its own version correctly again. The generated plugin-info file was not
  regenerated after a version bump, so 1.6.0 reported itself as 1.5.3 in the log and in SKSE's
  plugin list - which sent one piece of crash triage down the wrong path.

## 1.6.0

### Fixed
- **Director mode stranded OStim's own tongue** (reported by GSVJinx, with the diagnosis and the
  fix). OStim equips a tongue for a licking action's expression override and takes it back when
  the override ends - through the *underlying* expression path. Director mode switches that path
  off, so the tongue was never taken back: it hung there for the rest of the scene, was then
  mistaken for an ahegao mod's, and the mouth closed over it. Unequipping it directly would have
  been worse, because OStim's own list would still hold "tongue" and the next licking action
  would skip the equip for the rest of the scene. Instead the face is handed back for a moment
  and an event expression owning no phoneme objects is played, which is how OStim cleans up after
  itself; then the face is taken back. Needs the shipped `osis_tongue_clear` expression.
- **The owner column says "External tongue"** rather than "Ahegao mod" when no ahegao mod is
  installed. The old label sent at least one person hunting for a conflict that did not exist.
- **Changing the face mode mid-scene gives the face back immediately.** An actor taken over in
  Director mode stayed taken over until the scene ended, even after switching to Assist or
  Enhanced - which also stranded any tongue OStim had out.

## 1.5.3

### Fixed
- **The mouth played some moans twice.** The clip was re-anchored every tick from the sound's
  reported playback position, so a single wobble in that position - notably it reading near zero
  as a sound finishes - restarted the envelope and ran the whole mouth movement again in
  silence. A clip is now anchored once when the sound first appears and left alone: a new sound
  re-anchors, a large forward jump still resyncs, and a clip that has played through never
  drives the mouth again. The ten-second poll summary counts those.

## 1.5.2

### Fixed
- **Moan files inside a BSA are decoded now.** Lip-sync read them with a plain file handle, which
  only ever sees loose files, so a voice pack shipped as an archive decoded almost nothing and
  the mouth never moved for it. They are read through the game's own resource system instead, so
  loose files and archives both work.
- **The log says why a file was skipped**, instead of counting everything as "not loose": not
  installed, not a WAV container, or an encoding that cannot be read. The last is xWMA, which is
  what a .wav from a compressed voice pack usually contains - those moans play, the mouth just
  cannot follow them, and there is no fix short of decoding WMA.

## 1.5.1

### Changed
- **Arousal waits six seconds after a save loads before touching overlays.** Loading is when the
  engine installs 3D for a whole cell of actors and every overlay mod queues work onto RaceMenu
  at once. Nothing here is urgent in that window, so it stays out of it. Precautionary: a crash
  inside RaceMenu's own overlay install task was seen seconds after a load, with nothing of ours
  on the stack, and adding to that queue is the one thing we were doing at the time.

## 1.5.0

### Added
- **Arousal shows on a male body.** XPMSSE gives male skeletons a six-bone genital chain
  (Gen01..Gen06); the arousal level now bends it, the way the softbody morphs move a female body.
  Bones rather than mesh morphs, so it needs no assets and works with any schlong weighted to the
  standard chain. The bend is spread along the chain with the base taking most of it, eased over
  a couple of seconds so it follows arousal rather than twitching, and written over the rest pose
  every frame so the animation cannot flatten it.
- Off by default, on the Body page: "Bend the genital bones with arousal", with total degrees, a
  bone-local axis (which way the chain bends depends on the rig) and a six-second test button to
  find the right axis.

### Notes
- BodySlide sliders on a genital mesh already worked: morph rows drive every morphable mesh on
  the actor, armour included, so a row named after a slider in that mesh's .tri reaches it. This
  is for rigs with no morphs of their own.

## 1.4.1

### Fixed
- **A tongue could be left out for good.** The flag saying "we put this tongue out" lived on the
  scene slot, and an animation change rebuilds the slot. The flag reset while the tongue object
  stayed equipped, so nothing would ever take it back - and worse, the stray then looked like an
  ahegao mod's tongue to the detector, which stood the face down and reported "Ahegao mod" on the
  Status page to people who have no such mod installed. Whose tongue is whose is now recorded per
  actor, outliving slots and save loads; strays are taken back on sight, on game load, and from
  "Release all faces now". Reported with Halo's HDT Tongues, which is where it shows plainest.
- **The toe and finger test acts on everyone in the scene** when you are not aiming at anyone.
  It preferred the partner, so a test fired by a male player always landed on the female and
  looked as though it did not work on male bodies.

### Changed
- **Skyrim VR no longer installs the animation hooks.** Their addresses are Special Edition ones:
  the animation update is at a different vtable index in VR and the NPC job's call site has no VR
  address at all, so hooking patched the wrong thing. VR now falls back to the 20 Hz main-thread
  face path and logs it. Toe and finger curl need the per-frame hook and stay off in VR; the
  moan-muting hook is skipped too, with the per-tick sweep covering it.

## 1.4.0

### Added
- **Overlay slots are claimed from what is actually free on the actor.** Face overlays and body
  blush used to count off from a configured first slot and hope. They now read each RaceMenu
  overlay node back off the actor's own 3D, skip the ones another mod is already using, and take
  the free ones above the configured slot. This sees every mod's work - Overlay Distribution
  Framework, an ahegao mod, an overlay painted by hand in RaceMenu - because it reads the result
  rather than anyone's configuration.
- Face slots are claimed per actor rather than globally, since who holds what differs by NPC.

### Notes
- The limit is timing, not authorship: an overlay applied after we look is invisible until the
  next rebuild. The body blush re-reads every ten ticks; face slots re-read when the actor's 3D
  changes. Nothing is claimed below the configured first slot, which stays a reservation for
  other mods. When too few slots are free, fewer rows are painted and the log says so.

## 1.3.5

### Fixed
- **The face blush yields to Ahegao Expressions as well.** It paints a blush overlay of its own
  and takes a RaceMenu face slot for it. RaceMenu ships three slots, so two mods writing blush
  into that pool is how an overlay ends up black or missing - the cause of a black-overlay
  report. The yield already handed it the face; the face's overlays go with it. The body blush,
  arousal morphs and climax work are unaffected.

## 1.3.4

### Added
- **Overlay Distribution Framework is reported on the Status page.** It hands RaceMenu overlays
  to NPCs on its own, from the same numbered slots this mod writes to, so it is worth seeing in
  a screenshot when overlays misbehave.
- The Living Skin page warns when the face effects do not fit in the slots RaceMenu has, the way
  the body blush page already did, and says what to set iNumOverlays to.

## 1.3.3

### Fixed
- **An overlay is never pointed at a texture that is not installed.** NiOverride accepts a
  missing path without complaint and the slot then renders as a black patch over the body, which
  looks like a shader bug rather than a missing file. Face overlays and body blush rows now check
  the path through the resource system first, skip the ones that are not there, and name them in
  the log once. This is a candidate cause of the black-squares reports; it is not the only one.
- Female Makeup Suite's cheek blush is detected inside a BSA as well. The check was for a loose
  file, so a BSA-packed install of it read as "not found".

## 1.3.2

### Added
- **Every setting has a rollover explanation now.** 96 of the 143 controls had no tooltip, which
  left most of the Director page unreadable unless you had the source open. Each one says what it
  actually does, and what it depends on where that is not obvious - the anime accents needing
  Style above 1.5, act-type awareness needing scene metadata, the cry for help being what brings
  the responders.

### Removed
- `bEventBeats`, a Face setting that nothing has ever read. It was in the first commit and was
  never wired to anything. Leaving the key in an existing ini is harmless; it is ignored.

## 1.3.1

### Fixed
- **Lip-sync kept writing the mouth during the ahegao yield.** It is gated on its own switch and
  the old-mod conflicts, never on the face engine standing down, so handing the faces over left
  it fighting on its own. It now stands down with the rest of the face engine and says so on the
  Status and Lip-Sync pages.

### Changed
- Ahegao Expressions is a row in the Requirements table like everything else, instead of a
  sentence underneath it.

## 1.3.0

### Changed
- **Ahegao Expressions now gets the face to itself.** If `AhegaoExpressions.esp` is installed,
  this mod writes no faces at all and keeps to the body: arousal, blush, tears, climax, toe curl.
  Ahegao Expressions drives faces on its own schedule - its tongue can come out at half arousal,
  with its own expression behind it - so sharing a face with it only produced a fight. New
  setting `bAhegaoAutoYield` (on); untick "Leave faces to Ahegao Expressions if it is installed"
  on the Faces page to drive faces anyway.
- The per-actor hand-over no longer holds the jaw open around another mod's tongue. Their tongue,
  their mouth; the clearance only ever arrived at the wrong moment.

- The Status page says whether the yield is in effect and why, and the log records the
  detection at startup.

### Fixed
- The custom blush texture tooltip printed its example path with the backslashes eaten.

## 1.2.5

### Changed
- **A wider jaw clearance while a tongue is out.** Holding BigAah alone dropped the jaw without
  parting the lips much, so the tongue still rested on the bottom lip. The clearance now uses the
  same BigAah + Aah shape the lip-sync track uses to open a mouth, and gives way on the lip
  shapes that purse or stretch the mouth shut again. The default hold is now 0.75 (was 0.45).

## 1.2.4

### Fixed
- **The jaw clearance now survives the ahegao hand-over.** Handing an actor's face to an ahegao
  mod also dropped the jaw floor, so a mod that puts the tongue out without opening the mouth
  itself left the tongue clipping through closed lips. The clearance can only open the mouth
  wider than the other mod asked for, never close it, so it is safe to keep on while they drive.

## 1.2.3

### Changed
- **The test buttons no longer need the crosshair.** During a scene the crosshair picks up
  nothing, because OStim hides the HUD and takes the camera, which made every test button
  unusable exactly when you wanted it. They now fall back to the player's own scene, preferring
  the partner over the player. Aiming at someone, or selecting them in the console, still wins.

## 1.2.2

### Added
- A **"Test: tongue out for 8s"** button on the Faces page. It equips OStim's tongue on the
  crosshair actor exactly the way an ahegao mod does, so the hand-over can be seen on demand
  instead of waiting for one to trigger.

## 1.2.1

### Fixed
- The tongue was only polled while lip-sync was enabled and its tongue handling was not set to
  Ignore. That was fine when the flag only drove lip-sync, but it now also decides when to hand
  the face to an ahegao mod, so with lip-sync off the hand-over never happened. Always polled now.

## 1.2.0

### Added
- **Ahegao mods are handed the whole face, per actor, while they are actually running.** Ahegao
  Expressions and anything like it put the tongue out with OStim's `tongue` object and then write
  their own phonemes. That tongue is now detected live, and for as long as it is out this mod
  stops writing that actor's face entirely, then takes it back when the tongue goes in. Previously
  only the mouth was yielded, so the two fought over the eyes and brows, and the alternative was a
  blanket switch that stood down for the whole session. That switch remains as a fallback for
  ahegao mods that work some other way.

## 1.1.1

### Fixed
- **A tongue out no longer clips through the lips.** 1.1.0 stopped lip-sync while a tongue was
  out, but stopping it let the mouth fall back to closed, which is exactly the pose that pushes
  the tongue through. The jaw is now held open and the lips kept apart at the point the face is
  written, so it holds whether lip-sync, the expression grammar, or nothing at all is driving the
  mouth. The setting now chooses only whether the moan still moves the mouth above that floor.

## 1.1.0

### Added
- **Per-race body blush strength.** The overlays are one grey texture tinted per race, so a single
  opacity looked overpowering on pale skin and barely there on dark skin. Each race now scales the
  alpha, tuned to even it out, and editable on the Body Blush page.
- **Toes work during foot actions.** Previously the toes only moved at climax. A lower flex is now
  held for as long as a footjob, foot grinding, foot kissing or tickling runs, building with
  excitement. Off by default: it moves the same bones OStim measures to time a footjob's peak.
- **Open blush and morph tables.** Every blush row takes its own texture path, which body it
  applies to (any / female / male) and its own colour; morph rows take the same body field. Male
  overlays can be authored without a code change, and a race that normally never blushes can opt
  in by giving a row its own colour.
- **Lip-sync stands down while a tongue is out**, so closing lips no longer push an ahegao tongue
  through them. Detected live through OStim's tongue object, so it covers any mod that uses it,
  not only this one. Choose between stopping the mouth, holding it open past a floor, or ignoring.
- **A switch for the matte overlay treatment**, for setups where RaceMenu overlays render as solid
  black squares.
- More headroom on the curl angles: toes to 90 degrees, fingers to 120, per-toe to 3x.

### Changed
- The download without the non-consent features is now the default, named plainly; the full build
  is tagged `(LoversLab)`. Nothing user-facing refers to a "Nexus version" any more.
- The shipped readme and build manifest are `README-OSIS.md` and `BUILD-OSIS.txt`, so they no
  longer collide with other mods' files in a mod manager.

### Fixed
- Body-blush overlays addressed the female skin even on a male actor.
- Curl angles had no bounds check, so a hand-edited INI fed any angle straight to the bones.
- The default INI was written with a C++ enumerator name in place of a number.
