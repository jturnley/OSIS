# Changelog

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
