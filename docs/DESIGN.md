# OStim Standalone Immersive Sex (OSIS): DLL design

One SKSE plugin, `OSIS.dll`, replaces all four OSED Papyrus mods (Core, Body, Living
Skin, Lip-Sync) and Softbody Arousal. The plugin has no ESP, no Papyrus scripts and no MCM.
Settings live in the SKSE Menu Framework (section "OSIS") and in
`SKSE/Plugins/OSIS.ini`. The softbody morph and body-blush tables are in
`SKSE/Plugins/OSIS/morphs.json`.

## Threads of execution
- **Game main thread (SKSE tasks):** all engine logic. A ticker thread queues `Scenes::Tick`
  every 100 ms and `Arousal::Tick` every `fInterval`. OStim mod events and Papyrus callback
  results are re-queued as tasks too, so engine state is only touched here.
- **Animation update hook** (`Character`/`PlayerCharacter::UpdateAnimation`, vfunc 0x7D):
  runs every frame per actor, possibly on worker threads. It calls `Face::Output::Update`,
  which eases faces toward their targets and writes `BSFaceGenAnimationData`, and
  `Body::Update`, which curls toes and fingers after the animation pose. Both use their own
  locks.
- **Render thread:** UI pages, which take `Settings::lock`, plus snapshot getters that take
  module locks.

## Modules
| File | Replaces | Notes |
|---|---|---|
| `Settings` | all MCM properties + Softbody INI | table-driven INI binding |
| `Papyrus` | direct Papyrus calls | VM-dispatched calls into NiOverride, OStim (OThread/OActor/OData), arousal mods; results are async |
| `OStimData` | `OMetadata.*` | parses OStim scene/action JSON (tags, actions, speeds) |
| `Scenes` | OStim event handlers, thread tracking | one `Thread` per OStim thread, not just the player's |
| `Face/Engine` | `OSExpressionFaces.psc` | Director grammar (the old dormant mode 2), Assist/Enhanced layer, normal state, anime/tongue, gaze/headflow, watcher |
| `Face/Output` | MfgConsoleFuncExt | per-frame writer; lip-sync mouth override layer |
| `Body` | `OSED_Body.psc` | bone rotations applied after the animation pose |
| `Skin` | `OSED_LivingSkin.psc` | face overlays via NiOverride: blush, tears, saliva. Sweat was removed (a face-only texture never matched the body) |
| `LipSync` | `OSED_LipSync.psc` + bake toolkit | follows the moan OStim is playing, frame by frame; no bake, no FaceFX, no second voice |
| `Arousal` | Softbody Arousal `Engine.cpp` | scene factors feed the tissue target (see below) |
| `Compat` | none | detects the old OSED plugins or SoftbodyArousal.dll and switches the overlapping module off |
| `Pulse` | SLED_* mod events | calls Body/Skin/Arousal directly, still emits SLED_* for third-party listeners |
| `Serialization` | StorageUtil keys | cosave: NPC personality overrides, actors whose OStim face writer we disabled |
| `UI` | 4 MCMs + Softbody pages | SKSE Menu Framework |

## Data sources that replaced Papyrus queries
- Excitement: `OStimExcitementFaction` rank (OStim.esp 0xD93). Times climaxed:
  `OStimTimesClimaxedFaction` (0xE49).
- Thread actors (position order), scene id and speed: `OThread.GetActors/GetScene/GetSpeed`
  called asynchronously on start, scene change, speed change and each tick.
- `OActor.HasExpressionOverride`: polled asynchronously per actor per tick and cached.
- Scene metadata: parsed from JSON by `OStimData`.
- Voice set name (voice archetype): `OData.GetVoiceSetName`, cached per actor.
- `ostim_event` (annotation beats) is sent with custom Papyrus arguments and never reaches
  SKSE's C++ mod-event source, so event beats are unavailable in the DLL.

## Moans by personality

`MoanVolume.h/.cpp`, both editions, `[Audio]`. `Audio::rules[personality][phase]` is a `MoanRule { kind, level }`: kind 0 as they come, 1 none, 2/3/4 the quiet/normal/loud third of the voice
files; level 0..3 a volume tier (`fVolumeQuiet`, 1, `fVolumeLoud`, `fVolumeHigh`) times `fMoanMaster`. Phases: 0 stages 1-3, 1 stage 4, 2 the climax. `StageOfEntry` puts a staged list's last entry at
stage 4; with no staged set the stage is the scene's speed in quarters (the excitement when there is one speed). The thirds are `LipSync::BuildLevels`: when the background decode finishes, every
voice-set file (`Envelope::levelDb`, the 95th percentile of the 10 ms RMS frames, kept before the envelope is normalised) is sorted and cut in thirds, the middle file of each third is that class's typical
level, and a frozen `Key -> dB` map is read from the audio thread without a lock (`LevelOf`).

OStim's own moans: a hook on `BSXAudio2GameSound::Play` (vtable index 0x15, the one `Voice` hooks for the victim mute; `MoanVolume::InstallHook` runs first in `Hooks::Install`, so Voice's hook is the
outer one and its mute still has the last word) decides the volume the sound starts at (`Decide`): no sound where the rule says none (`volume = 1e-5` before `Play`, `SetVolume(0)` after, like Voice's mute),
else the master times the tier times a leveling gain `10^(strength * (classMedian - fileDb) / 20)` (0.2..4, only with `bLevelMoans` and a class asked for), clamped to 1. The owner is found by walking
the nodes the sound follows (`FollowChain`, SEH-guarded like Voice's) against a table of the 3D roots of the actors in scenes, which `Tick` keeps with each actor's `now` mode (the rule for the stage
the scene is at) and `climax` mode; `NoteOrgasm`, called from the event sink on the sender's thread, makes the climax mode apply for 8 s from the orgasm event, before the main thread has seen it. The
sound is recorded in `g_applied` before `Play` so the per-tick correction never takes a sound that is starting for one it missed; `Tick` corrects, once per sound, one it finds unshaped (no hook on VR;
started before the table) or whose volume changed after the hook. Muted sounds (volume <= 0.001) are skipped by `LipSync::CollectPlaying`, so a silent moan moves no mouth and sets no "voice busy".

The Director takeover plays its own sounds and does choose. `Moans` takes `PlanFor` (silent / class mask / volume) for the stage it knows (`StageOfEntry` of the natural entry) or for the climax, plays no
moan where the rule says none, and passes the class mask to `LipSync::PlayDescriptor`, which makes up to six tries: the hook (`StartOwn`) mutes a try whose file is of another class (`t_wantClasses`,
`NoteRejected`), `PlayDescriptor` stops it (`BSSoundHandle::Stop` on a sound that was played, silently, so nothing unplayed is left in the audio manager) and rolls another file from the same descriptor;
the last try is played whatever it is. Descriptors hold many files (10 to 33 in the test install), so a try is of the class asked for about one time in three. `VoiceModel::Shift` and `iStageShift*`
remain, at 0.

## The debug trace

`Debug.h/.cpp`. `OSIS.log` takes info and above, always (a sink at level info). The logger's second sink is a `TraceSink` that holds no file until `Debug::SetEnabled(true)`; then it
takes every level, the info lines included, so the trace is a superset of `OSIS.log`, and the logger's own level goes to debug. The sink is always in the logger so switching it on
and off changes no sink list while other threads log. The first `Open()` of a launch moves an existing non-empty `OSIS-Debug.log` to `OSIS-Debug.previous.log`, so a crashed
session's trace is not wiped by the next start; each line is flushed as it is written. `InitLogger` runs before `SKSE::Init`, which is called with `.log = false`: left on, it replaces the default logger with its own (one file sink, no trace sink), and the trace then ends up in `OSIS.log` with `OSIS-Debug.log` empty.

`Debug::Log(cat, fmt, args...)` formats only when `On()` (a relaxed atomic); `Debug::Label(actor)` is a small `ActorRef` that a `std::formatter` turns into `Name(0000ABCD)` at write
time, so an argument costs nothing while it is off. Callers whose arguments are expensive (`SceneText`, `ExplainRules`, `Preset`) check `On()` first.

`Debug::Tick` (scheduler, 20 Hz) writes the `[snap]` block every `fDebugSnapshotSeconds`: the thread's line and each slot's line are copied under `Scenes::Lock()`, which is then
released before `Output::Describe`, `Body::Describe`, `Arousal::Describe`, `Skin::Describe`, `LipSync::Describe` and `Moans::Describe` take their own locks, so the snapshot never
holds the scene's lock and a module's together. `Output::Describe` gives what the writer was asked for next to what the game is rendering (`phoneme3`, `modifier3`, `expression3`),
which is the line that separates "the Director asked for a flat face" from "something overwrote it". The personality in a `[snap]` is `Archetype()`'s, not the slot's copy, which
is only what the Director last used.

Decisions are logged where they are made: `ApplyArc` (the face and its inputs), `ApplyPersonalityFace` (which personality rule fired), `Breathe`, `LibraryPose` and `PickClimaxClip`
(picks), `UpdateExcitementRates` and `UpdateClimaxControl` (the factors and the reason), `Archetype`/`PinPersonality` (`ExplainRules`), `Scenes` (threads, nodes, events), the `Papyrus`
wrappers (every call out), and the setters in `Output`, `Body`, `Skin`, `Arousal`, `LipSync` and `Moans`. State changes are logged when they change (a lip-sync wait, a moan gate,
a tone flag), not every tick.

## Face ownership
- **Director** (default): OSED computes the full grammar and writes every frame.
  `OActor.SetExpressionsEnabled(actor, false, AllowOverride=true)` switches OStim's face
  writer off for painted actors. OStim still applies action overrides (e.g. the open mouth
  for oral); OSED then yields the mouth. The writer is re-enabled on scene end, on disable,
  and on load (a cosave list).
- **Assist / Enhanced:** OStim paints. OSED plays its `osed_*` expression events and adds
  micro eye/brow layers, gaze and headflow. `bTakeOverFace` optionally switches OStim's
  writer off, as in the original.
- **Mouth arbitration:** lip-sync override > dialogue/OStim override/oral action yield >
  engine mouth.

## Lip-sync pipeline
1. At data load, parse `SKSE/Plugins/OStim/voice sets/*.json`. For moan, climax, comment and
   event reactions, take the `sound` entries (muffled sounds are skipped). Resolve each SNDR,
   then its `BGSStandardSoundDef::soundFiles` gives the `BSResource::ID` of every moan file.
2. Walk `Data/Sound` for loose `.wav` files, hash each path with the engine's
   `ID::GenerateFromPath` (on the main thread), and keep the files whose ID matches. Decode
   them in the background into a 10 ms loudness envelope (RMS, normalized per file) and, from
   `AudioFeatures::Analyse`, three 20 ms tracks: the tone (spectral centroid, log scale), the pitch
   against the file's own median, and how voiced it is (see "What the moan sounds like" below).
3. At 20 Hz, read `BSAudioManager::movingSounds` (sound id -> followed node) and
   `activeSounds` (sound -> resource id, playback position) inside an SEH guard, since the
   audio thread owns those maps. When a known moan file is following a scene actor's 3D,
   start that actor's mouth track at the reported playback position.
4. `Face::Output` samples the envelope every frame and blends it in and out of the base
   mouth. Loudness sets how far the jaw opens; the tone and breathiness pick the mouth's shape
   (`PostureFor`); the pitch and loudness move the brows and the squint (see below).

### What the moan sounds like (`AudioFeatures.h/.cpp`, `Face/Output.cpp`)
`Analyse` takes the mono samples and averages them down to about 11 kHz (a box filter of D samples,
D = rate / 11025 rounded: 4 for 44.1 and 48 kHz, 17 for the 192 kHz files some OSSO sets ship). Every 20 ms
it takes a 256-sample Hann window, two frames per complex FFT (one in the real part, one in the imaginary,
separated by the symmetry of a real spectrum), and keeps the centroid of 150 Hz - 4.5 kHz on a log scale
(300 Hz = 0, 3.5 kHz = 1). Pitch is a normalised cross-correlation (RAPT-style) over 20 ms of the signal averaged
down once more (about 5.5 kHz), lags for 90-800 Hz, taking the shortest lag whose peak is within 15% of the
strongest (octave errors), parabolic interpolation, and a three-frame median; the voicing is the peak value.
Pitch is stored against the file's own median (the median of voiced frames above 0.60 and above 15% of the
file's 90th-percentile level), +-8 semitones over one byte. All three tracks are bytes; frames below the
noise gate hold the previous tone and read unvoiced. It costs about 0.2 ms per second of audio (about three
seconds for the 4,700 files of a big install), and its test harness lives outside the repo (it only needs the
standard library).

`Output::Update` smooths the three values (60 ms) and:
- **mouth**: `PostureFor(tone, breath)` interpolates a six-row table (a hum, dark, round "oh", an open vowel, mid-bright, spread)
  of jaw / BigAah / Aah / Oh / Eee / BMP multipliers and blends toward a breath posture by `1 - smoothstep(0.30, 0.55, voiced)`.
  A hum's BMP does not depend on the loudness (lips together however quiet); a held-open mouth (a tongue is out, `minOpen > 0`)
  keeps the plain open vowel so the floor still holds.
- **brows**: `lift = clamp(pitchAboveMedian / 5 st, 0, 1)` (zero in breath); brow up = gain * open * (0.10 + 0.70 * lift), brow in = gain * open^2 * (0.55 - 0.25 * lift);
  squint gets `open * (0.25 + 0.15 * lift)`. These add to the pose's value (`min(1, pose + add)`) and, on a channel no pose drives, are written alone and
  handed back once (`Channel::boostOwned`), in a layered face as the larger of ours and OStim's.

## Arousal factors
The target is the highest of:
- the arousal mod value;
- OStim excitement, while the actor is in a scene;
- 0.9 while edging (plateau);
- 0.7 during afterglow;
- 1.0 for `fClimaxHold` seconds after an orgasm.

Personality scales the response rates (stoic slower; vocal, dominant, crazed and wild faster, wild also
slower to fall back; a submissive faster in a rough scene; a stoic slower still) and the flush strength (shy and timid 1.25, stoic 0.8,
submissive 1.1). After a climax the level eases back to the reported
arousal and never snaps to zero.

Two fixes carried over from Softbody Arousal 2.0, which reset actors to zero around
orgasm:
- An actor missing from one update keeps its state for 15 s. Orgasm effects often force a
  3D reload, and that used to erase the state.
- New states are seeded from the first real arousal reading. A `None` VM result is ignored
  rather than read as 0.

Living Skin's face blush uses `max(scene excitement ramp, Arousal::Flush)`, so the face and
body flush together.

## How a personality is placed
`ResolvePersonality` (Engine.cpp), first match wins: a SPID `OSIS_Personality_*` keyword, a keyword another mod adds, the voice type's name,
`VanillaAIPersonality`, the 20% roll for the newer types (`NewPersonalityRoll`), then `FallbackPersonality`. The AI rule and the fallback were
retuned in 2.0.11 after measuring them on the base game's NPCs (`kAIDominantPercent` 25, `kFallbackWeights` 54/8/14/20/4): the earlier
rules and the shipped SPID file made about a quarter of everyone dominant. `Percent(actor, salt)` is a form-id hash, so a roll is the
same for the same actor every time and independent of another salt's. The shipped `OSIS_Personality_DISTR.ini` distributes nothing.

## Sleeping actors
`IsSleeping` (Engine.cpp): the actor a `sleeping` action names, or, in a scene only tagged `sleeping`, the one most actions target. The Director's
`SleepFace` replaces the composed face (lids shut via the blink channel, the rest slack) and skips gaze and headflow; the layer path sets the lids
directly and remembers it in `Slot::asleepShown` to open them again.

## Cost of the arousal module
`Arousal::Tick` (every `fInterval` s) follows the player and up to `iMaxNPCs` NPCs, scene or not. What it may not do is work for nothing: texture
existence is cached (`FsUtil::TextureExistsCached`), body-blush regions are built only once one has a non-zero alpha and taken down after 10 s
without, a repaint happens only when the rows or the 3D change or `Overlays::NodeHolds` finds a painted node no longer showing our texture, the
followed NPC set is sticky (`Gather`), and an arousal mod is queried every 5th tick outside a scene. `bOutsideScenes` (off by default; the page warns about its cost when on) confines it to scenes; with no arousal mod it is confined to those regardless.
`bFadeAfterScene` (off by default; "Arousal features slowly fade after scene (may cause increased GPU load)") extends that by `fFallHalfLife` x 3 so the
morphs and the blush ease back after a scene; without it `Tick` calls `ClearAllNow` the tick the last scene ends, and `ClearActor` also sets a male's
genital response back to rest, since nothing else would.

`Arousal::Tick` also leaves alone a female-bodied actor (`st.female`) for the rest of the scene once `LeaveAloneReason` finds one (`penisSeen`, `pauseWhy`), while `bPauseOnOral` is on: she has a penis in the
scene (`OStimData::HasPenisRole`, any role whose action definition requires "penis", so it needs neither a schlong mod nor the sex of the actor), her mouth is on one
(`OStimData::HasMouthOnPenis`: a role requiring "mouth" in an act whose other role requires "penis"), or the scene gives her slot a man's role (`intendedSex` "male": a futa under
OStim's "futas use the male role", known from the first node). No morph writes then, so no `UpdateModelWeight`, and no overlay work. A male body gets no morph rows and so no
refresh to begin with. This is a guess at why an SMP head's lips stop wrapping around a penis (reported, not reproduced: hotfix 2's pause, only while a blowjob targeted the
futa, and hotfix 3's, once a penis role was in play, did not cure it, and the reporter's confirmation was that switching the module off does); `penisPaused` logs when it applies and
the debug snapshot's `arousal` line says why.

The Faces module stands down for Ahegao Expressions the way it does for an old OSED core: `Compat::Disabled(kFace)` is true while
`Compat::AhegaoYield()` (installed and `bAhegaoAutoYield`, or `bAhegaoModYield`), and `ApplyAll` paints nothing (the scene is still tracked and
pulsed). Before this only the Assist and Enhanced layers checked the ahegao yield, so the Director wrote faces beside Ahegao Expressions.
`Face::Engine::FaceYielded` (Living Skin's face blush, Lip-Sync) is the same `Compat::AhegaoYield()`. Dynamic Dialogue Framework only takes
Lip-Sync down (`bYieldToDDF`); a Faces stand-down for it was tried and taken out again.

Vocal (2) is loud by frequency: `Breathe` gives it the shortest breath cycle (2 beats) at any excitement and makes every cycle a moan, with the mouth
kept partly open on the rest beat. The moan sound itself is OStim's (one global interval, `OStimMoanIntervalMin/Max`), so it is not per actor.

**Director moans (`Moans.cpp`, `VoiceModel.cpp`, full edition only, off by default).** With `bDirectorOwnMoans` and the Director, OSIS plays each painted actor's moans
and climax itself and mutes OStim's plain-moan files on them (`Voice.cpp`'s start hook and sweep; comments and reactions stay OStim's). `VoiceModel` parses OStim's voice
sets and chooses as OStim does (the set from the name OStim reports, the entry whose perk condition holds, the muffled list while an action says muffled). The time
between moans and their volume are OStim's own MCM values, three globals in `OStim.esp` (local IDs 0xE0A min and 0xE0B max, in milliseconds, 0xE0C volume), read through
`ReadOStimSettings` when `bUseOStimMoanSettings` is on; `fOwnMoanVolume` / `fOwnMoanIntervalMin` / `Max` are the fallback. A voice-set entry's own interval overrides
the random one, and a vocal actor's wait is a fixed 0.05 s (`kVocalGap`) whatever either says: the vocal personality is the loud one by never stopping.

Crazed (8) is the stare: `SetGaze` keeps the look on the partner (and `ApplyHeadflow` the head), `UpdateCrazedBlink` (from `ApplyAll`, Director mode) holds the blink
channels open with `Output::SetBlinkHold` and fires `Output::PulseBlink` about every `fCrazedBlinkSeconds`, and `ApplyPersonalityFace` writes the yandere face: a held
smile (mood happy, Eee) under three slowly turning brow looks (menace, manic, uneven). The blink hold relies on the engine's blink living in the same modifier channels
Output writes (`Output.cpp` already hands them back at rest for that reason); where OStim's writer is on, the layered `max` lets the engine's blink through.

Head tilt (`Body::SetHeadTilt`, from `Face::Engine::UpdateCrazedTilt`): `ApplyTilt` rolls the head bone, and the neck bone by `kNeckShare`, about a world axis (the line to the
partner), taken into each bone's parent frame with `ToLocal` before either changes, so the two add up to one rotation at the head. It runs after the animation update like the
curl and keeps the animation's pose (`FreshPose`) to avoid stacking if the animation did not rewrite the bone. Local axes of Skyrim's head bone are not relied on.

`Face::Engine::UpdatePPAMouth` hands a blowjob giver's mouth to PPA and watches whether PPA is really driving it: a summed phoneme change of 0.6 since the hand-over, or (since
hotfix 3) the phonemes already matching PPA's winning mouth preset (`PPA::MouthMatchesPreset`, from the preset's `Phoneme` effects: each constant target within 0.30, the
phonemes it does not set at most 0.20 when `OverridePhonemes`, a summed excess of at most 0.15; depth-scaled phonemes only bound the top). If neither shows within 6 s the mouth
goes back to the Director and is offered again after 15 s.

The player's personality is `Settings::Face::iPlayerPersonality`, Balanced (0) by default; Auto (-1) hands the player to `ResolvePersonality` like an NPC. `Settings::Load` resets an INI that
has no `[General] iSettingsRevision` to Balanced once (`migratedRevision`) and sets `General::bResetPlayerPin`, so `SetNpcPersonalities` also drops the player's pinned (automatic) entry
from the cosave that run. Revisions are the place for any future one-time reset of a setting.

## Body types
Arousal morph rows and blush regions carry a `type` (`Settings::Arousal::BodyType`: any, CBBE/3BA, UBE). `IsUBE` in `Arousal.cpp` reads the
race: an editor ID starting `00ube_`, or a name ending ` UBE` (UBE_AllRace.esp's 18 races), and `BodyMatch` keeps each row to its body. UBE
has its own sliders and UV map but the stock skeleton, so Body (toe curl, grip) is untouched. `UbeMorphs()` and `UbeBlushes()` are the stock UBE
rows; the textures are the user's (UBE-UV conversions of Body Blushing).

## Personalities
Nine ids (`Face::Engine::Pers`): 0 balanced, 1 stoic, 2 vocal, 3 shy, 4 dominant, 5 timid, 6 submissive (full edition only), 7 wild,
8 crazed. The lite edition has no submissive: `EditionPersonality` turns a 6 into a timid (5) wherever one could come in (SPID keyword, voice
token, the new-type roll, a stored or INI value, the UI combo), so the same actor rolls the same in both editions bar that. `BasePersonality` maps a newer type to the original it is built on (wild to vocal, crazed to dominant, timid and
submissive to balanced), which the original rules in the Director see (`arch`); the newer rules and the picks see the full id
(`pers`). Stoic (1) has its own faces, never OStim's pool (`stoicOwn` in `LibraryPose`): calm ones for the first three build-up stages (`Buildup::kStoicOptions`, indexes from `kOptionCount`), shock and surprise for the edge and half of the heightened stage (`kStoicShockOptions`, after them), climax faces weighted to *wide_eyed*, *stunned*, *startled* and *disbelief* (`kClimaxVariants`), and a dazed afterglow (`StoicAfterglowPreset`); `ApplyPersonalityFace` mutes it (x0.65, lids a little low) only in the calm stages. It is the opposite of wild: much slower excitement (`fStoicExcitementMult`, 0.20), a muted face (`ApplyPersonalityFace`, 0.65 / 0.45), neutral moods, a quiet climax, and a muted body (`Arousal::Shape`: `fStoicRest` / `fStoicPeak`, face and body blush gated to the last seconds before and during the orgasm - `Arousal::Flush` returns the gate for Skin, which ignores the excitement ramp for a stoic - which fades with a 1.5 s half-life). Where each shows: `MoodAffinity`, `Buildup::Pick` (`ExtraWeight`), the climax pool's `suits[9]`, `ApplyPersonalityFace`
(shy's conflict, timid's lids and peeks, vocal's surprise, the dominant's smirk, wild's smile, crazed's stare and creepy look,
submissive's brows), `SetGaze`, `ApplyHeadflow`, `Breathe`, and the excitement rates in `UpdateExcitementRates`.

**Control of the climax** (`UpdateClimaxControl`, `OnOrgasm`, consensual scenes with `bPersonalityControl`): a dominant's partners
get `OActor.StallClimax` (OStim holds a stalled actor at 100, `awaitingOrgasm`, and climaxes it when it is permitted), said again
every 4 s because OStim ignores it for an actor it has not put in a thread yet. They are permitted while a dominant is climaxing,
and `OnOrgasm` of the dominant forces (`OActor.Climax` with IgnoreStall) any partner at 80 or more, so they climax together; after
`fControlMaxHold` seconds at the edge (`holdSince`, from 97) the dominant lets them go. A crazed actor is stalled the other way
round: its own climax waits for a partner's, and the partner's `OnOrgasm` forces it (`forcedAt` stops a force from re-forcing); a crazed *man* is forced only while he is
`Engaged` (stimulated) and at 60 excitement or more (`kMaleWithPartnerMin`), a woman always.
**Recovery after an orgasm** (`fRepeatOrgasmGap`, any thread, everyone but a wild or crazed actor: `Recovers`): after OStim's reset an actor sits at about half excitement, so a fast scene gave a wild actor
an orgasm every 6.5 s (18 in 270 s, the first game test). A first version stalled them at the edge (`OActor.StallClimax`) until the gap was over; in play that was constipated, and it also started too late (after the climax
face) and let bursts through. Now `OnOrgasm` sets `Slot::recoveryDrop`, and `UpdateExcitementRates` (every tick) brings the excitement down to `kRecoveryRaw` (10) once `kRecoveryDropDelay` (1.5 s) has passed (OStim sets
its own post-orgasm value first; `OActor.ModifyExcitement`), and multiplies the actor's rate factor by `kRecoveryFloor + (1 - kRecoveryFloor) * x^2`, x being the time since the orgasm over the setting, in tenths so a
change is one call to OStim, not one per tick. Nothing is stalled and nothing is permitted: an actor who is stimulated enough can still get there early. The log's "Consent: ... excitement rate" line reports the
factor before the recovery (`Slot::excitementUsual`), so a recovery does not write a line per step. A broken woman is not touched (her rate is hers).

## Scene limits by personality

`SceneLimits.h/.cpp`, both editions, `[SceneLimits]`. Hooks from `Face::Engine`: `OnThreadReady`, `OnSceneChanged`, `OnTick` (after `RefreshDerived`, so `Thread::consent` is current) and `EndScene`; `Tick` and `Clear` from the plugin's main-thread
tick and game load. `Refresh` rebuilds a thread's `Limit` per actor: `Archetype` gives the personality (shy, timid, submissive passive; dominant, crazed aggressive), `Slot::orgasms > 0` lifts it, the player's own is `Dir::kNone` unless `bPlayer`,
and with two live actors who want the same role both `yield` (limiting one to the scenes that suit it leaves few: first the stronger personality kept its limit, crazed over dominant, which left a crazed woman with a dominant
man only the handful of scenes that put her on top, so now neither is limited). A thread is `Active` when the module is on, the scene is
consensual and a limit binds. `Classify` turns a scene node into a `Role` per actor position (cached; `ExplainRoles` says why): your `scene_roles_user.txt` first (a scene line, then the first tag line), a transition is its
destination, otherwise every action that `KindOf` calls positional (penetration and toys, grinding, mounting, smothering, spanking, choking, hair pulling, breast slapping; giver = the action's actor, receiver = its target) is
decided by `GiverOnTop`: the actor tags `ontop` / `onbottom`; the scene tag labels (`kFemLabels`: the role whose definition requires a vagina or anus is on top, `kMaleLabels`: the one requiring a penis, only if exactly one family
is present, via `OStimData::ActionRequirements`); the postures (lying versus not); an explicit `performer` that is the receiver; else the giver. Oral on a vagina or anus (`kHole`) counts only on a tag or actor tag (face-sitting,
femdom), so ordinary oral stays neutral. An actor on top in some pairs and underneath in others is neutral. `AllowedLocked` is `Breaks == nullptr`: no binding limit sees its forbidden role (a passive one `kActive`, an aggressive one
`kPassive`). The steering is `SceneLock`'s (see that section) with `Choose` filtering by `Breaks` and ranking by `Fit` (limited actors given their own role), `Move` looking within 3 steps, then 6, and `Check` (every second and on every
scene change) redirecting with `NavigateTo`. The auto-mode takeover (`bTakeOverAuto`, off by default) hands OStim's auto mode back (`Release`) when no limit binds any more; it is off because stopping OStim's auto mode
also stops its pacing (the first game test: two solo NPC scenes with a shy actor, auto mode taken over, hopped between unrelated scenes for 250 s without an orgasm, where unlimited ones ended in 76 s). `Refresh` gives
no limit to a thread with fewer than two live actors, since a role needs a pair. The menu trim (`TrimMenu`, UI thread) is SceneLock's, keeping at least one option. Lock order:
Scenes lock, `Settings::lock`, `g_lock`, `g_roleLock` (a leaf).

## First times

`Virginity.h/.cpp`, both editions (the broken woman and the non-consensual table are `#if !OSIS_LITE`), `[Virginity]`. Hooks from `Face::Engine`: `OnThreadReady` (marks each woman's orgasm count), `OnTick` (every thread tick), `OnOrgasm`
(first, so a broken woman's orgasm can return early), `EndScene` (before `EvolveAfterScene`). A woman's virginity comes from OVirginity Reflowered (`OVirginity.esp`, quest 0x800 found at the first use): `OUtils.GetNPCDataInt(actor, "IsVirgin")`
asynchronously (`Ask`), 1 or 0 used as it is, -1 (not worked out yet) settled from `ovirginity_config.json`'s `alwaysvirgin` / `nevervirgin` name lists, else by calling `OVirginityScript.isVirgin(actor)` on its quest through
`Papyrus::CallMethod`, which works it out and keeps it. The answer is kept per scene (`g_answers`, dropped at the scene's end), so a woman is asked again, and a first time ends it for good. When the answer is 1 and the woman's
`Record::state` is 0, her first scene chooses a personality (`ChooseFirst`, `ChooseTrauma` for a thread that is not consensual: both write it with `Face::Engine::SetAutomaticPersonality`, which refuses a hand-set one and the player's),
state 1. `Rt` tracks the scene: the penetration is the thread's node having a `vaginalsex` action with her as the target (the same test OVirginity makes), noted with her orgasm count and whether the thread was rough then;
`fast` is set when the thread's speed index goes above `iSpeedLimit` after that and before her first orgasm. At the scene's end `Resolve` gives the bad / extraordinary / ordinary outcomes and sets state 2, or, for a woman who
was a virgin and still is, rolls the balanced chance if she had an orgasm. Religion: `FaithOf` reads `npc_religion.txt` and the `_user` file by name, then the Dibella faction (0x656EA, 0x656ED) and the temple and priest factions of
Skyrim.esm. A broken woman is a `Record::broken` flag on a stoic: `IsBroken` is asked by `UpdateExcitementRates` (rate = `BrokenRate`, clamped 0.01..1, replacing every other factor), `ApplyAll` (nothing painted, face
released), `Arousal` (`Target` and `Shape` return 0), `Skin::Paintable`, `Body` (`OnPaint`, `OnClimaxPeak`), `MoanVolume` (every phase silent, and the takeover's plan), `Voice::Sync` (not a victim who calls for help), and
`OnOrgasm`, where her "orgasm" only counts a cycle, adds 0.01 to her rate and puts her excitement back to zero (`OActor.ModifyExcitement -100`); ten cycles in a session with nothing rough or non-consensual in it make her
stoic at the session's end. `Experience::BecomeObsessed` makes the 5% a crazed woman about the first partner to give her an orgasm. The state is the cosave record `VIRG` (version 1).

## NPC bios

**Not in the build for now.** The code (`BioPersonality.h/.cpp`, `Face::bBioPersonality`, its menu check, the `Bio` source in `ResolvePersonality`, and the read of the shipped `npc_religion.txt`) is behind `OSIS_BIO_TABLES`, which is 0 (`xmake f --bio_tables=y` builds it again), and the two shipped tables are not in `dist/`: the bios they were read from are under SkyrimNet's Plugin License, which does not clearly allow it. The tables are on the 3.0 branch. `npc_religion_user.txt` still works.

`BioPersonality.h/.cpp`, both editions. `ResolvePersonality` asks `BioPersonality::Lookup` after the SPID keyword and the OSIS keywords and before the voice set (source "Bio"; `Face::bBioPersonality`). The tables are
`Data/SKSE/Plugins/OSIS/npc_personalities.txt` (shipped from `dist/`, built from the bio spreadsheet by a scratchpad script: adult NPCs only, `name|personality|suffix`) and `npc_personalities_user.txt` (the player's, wins),
read once by `EnsureLoaded` (`std::call_once`) into two multimaps keyed by the name with everything but letters and digits as one space. Where several lines share a name `Find` takes the one whose suffix
(three hex digits) is the actor's base form id's last three, then the reference's, then a line with no suffix, then the only line there is; with several and no match, none (better no personality than a namesake's).
The suffix is a guess at SkyrimNet's file keys (`name_ABC`), not confirmed against the game's records. The player is never looked up. The result goes through `EditionPersonality` and is pinned like any rule's.

## Personalities grow, and obsession

`Experience.h/.cpp`, both editions, `[Evolution]`. `Face::Engine::EndScene` calls `EvolveAfterScene` for each actor of the thread: the orgasms the slot has had since the last call (`Slot::orgasmsSettled`; a thread that is left and
come back to is two sessions) and the other actors in the thread go to `Experience::OnSessionEnd`, which keeps a `Record` per actor (`pers`, `qualified`, `sessions`, `orgasms`, `obsessedWith`, `partners`; at most 4096 actors).
Two things come of it. A session with at least the rule's orgasms for the actor's current personality counts toward that rule (`RuleFor`: stoic to balanced, shy to timid, timid to balanced, vocal to wild), and when the
sessions are reached the personality is written back into `g_npcPersonality` with `kPinnedFlag`. A session with at least the obsession rule's orgasms (`ObsessionRule`, 20 by default) counts toward every other actor who was in
it (`Record::partners`, up to eight); the first to reach the rule's sessions becomes `obsessedWith`, for good, and the counts are dropped. A record whose `pers` is not the actor's current personality starts the rule's count
again (a hand-set change, or "work out again"); an obsession stays. Only an entry with `kPinnedFlag` (a personality OSIS settled on by itself) is considered, never the player's or one set by hand.

Obsession shows at scene time: `RefreshDerived` runs for every thread refresh and sets `Experience::SetObsessedNow` for each actor whose `obsessedWith` is in the thread (cleared in `EndScene`), and `Archetype` returns crazed
for such an actor (source "Obsessed") before anything else but the player, so every module that asks sees crazed for exactly the scenes the two are in together. `Serialization` keeps the records in the cosave as `EXPR`
(version 2: the obsession and the per-partner counts; version 1 held a single crazed counter and is skipped). Locking: `OnSessionEnd` and `Describe(..., true)` run under `Settings::lock` (the caller holds it) and take only
the module's own lock.
## Consent
- Decided per thread from the scene's tags, recomputed on every scene change (`RefreshDerived`):
  - **Non-consent** (`toneForced`): `forced`, `forceful`, `rape`, `fbrape`, `nonconsensual`,
    `noncon`, `aggressive`, `aggressivedefault`, `aggressor`. `aggressive` is OStim's own marker
    for aggressive threads.
  - **Consensual rough play / BDSM** (`toneRough`): `rough`, `dom`, `femdom`, `maledom`,
    `domination`, `dominant`, `bdsm`, `bondage`, `spank`, `spanking`, `choking`, `slave`, and
    only when no non-consent tag is present. These scenes stay consensual and only add rough
    intensity (brow tension, squint) to faces and bystanders.
  - **Started by the player's spell** (`spellVictims`, `SpellCast`): OStim NPCs' Matchmaker and
    similar spells run a script effect on their targets, and the script starts the scene a few
    seconds later. The DLL records the player's real casts (`TESSpellCastEvent`: spells, powers,
    shouts, scrolls, staffs; not abilities, cloaks or potions) that carry a Script-archetype
    effect, and where those effects land on NPCs (`TESMagicEffectApplyEvent`, caster = player). When
    a new thread's actors are first known, the thread is spell-started if one of its NPCs took such
    an effect within the last 30 s of unpaused play (the clock Papyrus updates run on; menus don't
    count). Those NPCs are the spell's victims. The victims are latched for the life of the thread
    and logged with the spell; every other NPC in the thread is logged as there of their own accord.
    - **Completed casts:** Matchmaker tags its targets one cast at a time and only starts the scene
      once the player has cast on everyone in it, the player included (a separate Self spell). A hit
      up to 10 minutes old still counts when the player cast another spell from the same plugin
      after it, within the last 30 s. That later cast is the scene's trigger time. Hits are used up
      by the scene they start, so a later scene with the same NPCs needs a new spell.
    - **Restarts:** when a spell-started thread ends, its victims are remembered for 10 s. A new
      thread with one of them continues the same scene, and they are still its victims unless they
      talked with the player since the end. Followers Ask To Join stops the thread and starts a
      bigger one after the follower asks; the follower was never cast on and is no victim.
    - **Attacks don't count:** hostile effects, and effects on an enemy who is fighting the player,
      are not recorded. A defeat scene after a fight (Yamete) keeps the roles its mod gave it.
  - **Asking is not forcing:** a scene that came out of a conversation is not spell-started, even
    right after a spell. The DLL notes who the player is in dialogue with (the Dialogue Menu's
    speaker, every heartbeat while it is open). If the player's latest conversation with an NPC
    the spell hit is at or after that NPC's trigger time (the hit, or the cast that completed it),
    the NPC asked (ODragonSeed's NPCs walk up and ask; OFriends' "somewhere private") or was asked,
    and is no victim. With no victims left, only the tags decide consent. A spell cast after the
    conversation still counts.
- `consent = !(toneForced && bAggressorGrammar) && !spellNonConsent`, where `spellNonConsent` is
  `bSpellNonConsent` and one of the spell's victims still in the thread. The victim is an actor
  tagged `victim`/`submissive`/…, or, failing that, the receiving partner by position. In a
  spell-started scene the tags don't matter: the spell's victims are the victims (in an NPC-only
  scene the player cast on everyone, all of them), and everyone else is an aggressor: the player,
  and an NPC who joined without being cast on (a follower who asked to join). Scene changes can't
  make a spell-started thread consensual. In a consensual scene a joiner is consensual like
  everyone else.
- **A submissive accepts it** (full edition, `SubmissivesAccept`): a scene that would be non-consensual - tagged
  forced, rape or aggressive, or started by the player's spell - is played as consensual when every victim (whoever
  the scene's roles name, everyone if they name nobody) has the submissive personality and a relationship rank of 3
  (ally) or 4 (lover) with everyone else in the thread. `Thread::acceptedBySubmissive` is set, `consent` becomes true,
  `toneForced` stays as the tags say, and so no distress grammar, victim reaction, tears or scene lock follow. It is
  logged when it starts and when it stops, and shown on the Status page. One victim who is not such a person leaves
  the whole scene non-consensual.
- A non-consensual scene gets distress faces (hard gate: even at climax), guardrails, no
  anime/tongue. Nobody in the scene gets blush or saliva. Faces are role-aware:
  - **Victim**: reacts by personality (`VictimReaction`):

    | Personality | Reaction | Face |
    |---|---|---|
    | Dominant | defiance | anger, jaw set, brows down, eyes forward; may glare; no tears |
    | Shy | fear | fear, raised and pulled-in brows, small mouth, eyes down, head turned away |
    | Vocal | panic | fear, mouth open (crying out), eyes down, head turned away |
    | Stoic | numb | restrained sadness, closed mouth, eyes down, head turned away |
    | Balanced | sad → fear | sadness at low excitement, fear from 45, open-mouthed fear from 90 |
    | Timid | fear | as shy |
    | Wild, Crazed | defiance | as dominant: glaring, no tears |
    | Submissive (a scene it does not accept) | sad → fear | as balanced |

    Everyone but a defiant victim gets welling eyes, averted gaze, the brace head-turn and tears.
  - **Aggressor** (everyone else, once a victim is identified): anger, lowered brows, narrowed
    eyes, gaze held on the victim, no fear or averted eyes, no head-turn.
  - If nobody can be identified as the victim, every actor keeps the victim treatment for their
    own personality rather than guessing an aggressor.
- **Tears are reserved for non-consent.** The victim wells up when distress starts and at a
  forced climax, at most once every 20 s. Consensual scenes never get tears. When a thread
  moves on to a consensual scene, the victim marking is cleared.
- **Broken after climax** (`bBrokenAfterClimax`): when the identified victim climaxes, their
  mind checks out for the rest of the thread, even if it later moves on to a consensual scene.
  The face goes empty: a slightly slack jaw, heavy lids, a low unfocused stare, no mood, no
  gaze, head movement, breathing layer or tongue. Tears keep coming every 20 s (a defiant victim
  cries too once broken), and the welling-eye fallback no longer knits the brows. The body keeps
  responding: arousal, excitement rates, further climaxes and toe curl are unchanged. Lip-sync
  still moves the mouth with moans but no longer squeezes the eyes. A thread restarted within
  10 s (a follower joining) keeps the victim broken. With the Ahegao-mod yield on, the face is
  left to that mod.
- **Excitement rates** (`bConsentExcitement`): in a non-consensual scene with an identified
  victim, OStim's per-actor excitement multiplier (`OActor.SetExcitementMultiplier`) is scaled
  relative to OStim's own rate. Defaults: victim ×0.5 (climax takes about twice as long), every other
  actor ×1.5. OStim keeps the multiplier on its per-scene actor record, so it resets when the scene
  ends; the DLL restores it earlier if the thread turns consensual. With no identified victim,
  nobody's rate changes. The body-response rates (morphs, body blush) do not depend on consent.
