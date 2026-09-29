# OSED Reborn: DLL design

One SKSE plugin, `OSEDReborn.dll`, replaces all four OSED Papyrus mods (Core, Body, Living
Skin, Lip-Sync) and Softbody Arousal. The plugin has no ESP, no Papyrus scripts and no MCM.
Settings live in the SKSE Menu Framework (section "OSED Reborn") and in
`SKSE/Plugins/OSEDReborn.ini`. The softbody morph and body-blush tables are in
`SKSE/Plugins/OSEDReborn/morphs.json`.

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
   them in the background into 10 ms loudness (RMS, normalized per file) and brightness
   (zero-crossing rate) envelopes.
3. At 20 Hz, read `BSAudioManager::movingSounds` (sound id -> followed node) and
   `activeSounds` (sound -> resource id, playback position) inside an SEH guard, since the
   audio thread owns those maps. When a known moan file is following a scene actor's 3D,
   start that actor's mouth track at the reported playback position.
4. `Face::Output` samples the envelope every frame and blends it in and out of the base
   mouth. Loudness sets the Aah/BigAah opening, brightness shifts it toward Eee versus Oh,
   and the squint follows the loudness.

## Arousal factors
The target is the highest of:
- the arousal mod value;
- OStim excitement, while the actor is in a scene;
- 0.9 while edging (plateau);
- 0.7 during afterglow;
- 1.0 for `fClimaxHold` seconds after an orgasm.

Personality scales the response rates (stoic slower; vocal and dominant faster) and the
flush strength (shy 1.25, stoic 0.8). After a climax the level eases back to the reported
arousal and never snaps to zero.

Two fixes carried over from Softbody Arousal 2.0, which reset actors to zero around
orgasm:
- An actor missing from one update keeps its state for 15 s. Orgasm effects often force a
  3D reload, and that used to erase the state.
- New states are seeded from the first real arousal reading. A `None` VM result is ignored
  rather than read as 0.

Living Skin's face blush uses `max(scene excitement ramp, Arousal::Flush)`, so the face and
body flush together.

## Consent
- Decided per thread from the scene's tags, recomputed on every scene change (`RefreshDerived`):
  - **Non-consent** (`toneForced`): `forced`, `forceful`, `rape`, `fbrape`, `nonconsensual`,
    `noncon`, `aggressive`, `aggressivedefault`, `aggressor`. `aggressive` is OStim's own marker
    for aggressive threads.
  - **Consensual rough play / BDSM** (`toneRough`): `rough`, `dom`, `femdom`, `maledom`,
    `domination`, `dominant`, `bdsm`, `bondage`, `spank`, `spanking`, `choking`, `slave`, and
    only when no non-consent tag is present. These scenes stay consensual and only add rough
    intensity (brow tension, squint) to faces and bystanders.
- `consent = !(toneForced && bAggressorGrammar)`. The victim is an actor tagged
  `victim`/`submissive`/…, or, failing that, the receiving partner by position.
- A non-consensual scene gets distress faces (hard gate: even at climax), guardrails, no
  anime/tongue, and no gaze from the victim. Nobody in the scene gets blush or saliva.
- **Tears are reserved for non-consent.** The victim wells up when distress starts and at a
  forced climax, at most once every 20 s. Consensual scenes never get tears. When a thread
  moves on to a consensual scene, the victim marking is cleared.
