# Changelog

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
