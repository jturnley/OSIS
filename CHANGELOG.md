# Changelog

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
