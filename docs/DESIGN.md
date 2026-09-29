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

    Everyone but a defiant victim gets welling eyes, averted gaze, the brace head-turn and tears.
  - **Aggressor** (everyone else, once a victim is identified): anger, lowered brows, narrowed
    eyes, gaze held on the victim, no fear or averted eyes, no head-turn.
  - If nobody can be identified as the victim, every actor keeps the victim treatment for their
    own personality rather than guessing an aggressor.
- **Tears are reserved for non-consent.** The victim wells up when distress starts and at a
  forced climax, at most once every 20 s. Consensual scenes never get tears. When a thread
  moves on to a consensual scene, the victim marking is cleared.
- **Excitement rates** (`bConsentExcitement`): in a non-consensual scene with an identified
  victim, OStim's per-actor excitement multiplier (`OActor.SetExcitementMultiplier`) is scaled
  relative to OStim's own rate. Defaults: victim ×0.5 (climax takes about twice as long), every other
  actor ×1.5. OStim keeps the multiplier on its per-scene actor record, so it resets when the scene
  ends; the DLL restores it earlier if the thread turns consensual. With no identified victim,
  nobody's rate changes. The body-response rates (morphs, body blush) do not depend on consent.
