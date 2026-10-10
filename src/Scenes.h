// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

#include "OStimData.h"

// OStim thread tracking. Each running OStim thread gets a Thread; its actors get Slots.
// The fields mirror the runtime state of OSExpressionFaces.psc, which only tracked one
// thread (the player's). All of this is touched on the game's main thread only; the UI
// reads it through Snapshot(), which takes the lock.
namespace Scenes
{
	enum Dom : int
	{
		kAnticipation = 0,
		kPleasure = 1,
		kPlateau = 2,
		kDistress = 3,
		kClimax = 4,
		kAfterglow = 5,
	};

	struct Slot
	{
		RE::ActorHandle handle;
		RE::FormID id = 0;
		std::string name;
		int pos = -1;
		bool player = false;
		bool painted = false;        // human and (player or NPCs included)
		bool female = false;

		bool exprOverride = false;   // OActor.HasExpressionOverride, polled
		bool tongueOut = false;      // OActor.IsObjectEquipped("tongue"), polled: ours or an ahegao mod's
		bool takenOver = false;      // we switched OStim's face writer off for this actor
		float takeoverRecheck = 0.0f;  // when to say it again; OStim can drop the first one
		std::string voiceName;
		bool voiceRequested = false;
		float excitementFactor = 1.0f;  // consent: our scale on OStim's excitement rate (1 = untouched)
		// Personality control (Engine.cpp UpdateClimaxControl): this actor's climax is stalled by us, when we last said so, when to say
		// permit a second time, and since when they have been held at the edge.
		bool stallActive = false;
		float stallIssuedAt = -100.0f;
		float permitRepeatAt = 0.0f;
		float holdSince = 0.0f;
		float forcedAt = -100.0f;  // when we last made this actor climax (a partner's orgasm, or a dominant's): not again for a few seconds
		bool broken = false;            // a victim who climaxed: vacant face for the rest of the thread
		bool faced = false;             // OSED painted this face (handed back if faces are switched off)
		bool victim = false;            // identified victim of a non-consensual scene (set by RefreshDerived)
		float shockUntil = 0.0f;        // shocked face after the breaking climax, until then

		// v2 faceflow
		int lastPulseDom = -1;
		int lastPhrase = -1;
		float overwhelm = 0.0f;

		// anime / tongue prototype layer
		bool animeActive = false;
		int animeVariant = -1;
		bool tongueOn = false;
		float tongueClearUntil = 0.0f;  // asked OStim to take its own tongue back; wait before asking again
		float tongueLifeUntil = 0.0f;
		float tongueLifeNext = 0.0f;
		float blinkNextAt = 0.0f;  // a crazed actor's next blink (their lids are held open between)
		bool tilting = false;  // a crazed actor's head tilt is on (put back when it stops)
		float tonguePrimeUntil = 0.0f;
		float tongueHoldUntil = 0.0f;
		float tongueCooldownUntil = 0.0f;
		int lastClimax = 0;
		bool climaxing = false;  // this actor's own orgasm is in progress (until climaxUntil)
		// This actor's own orgasms in the scene, for the face. Per actor and timed by the clock: the thread-wide window they used
		// to share was re-armed by every orgasm of anyone, so in a scene of repeated orgasms it never ended.
		int orgasms = 0;
		int rapidRun = 0;           // orgasms in a row that came within the rapid window of the one before
		float lastOrgasmAt = 0.0f;
		float orgasmGap = 0.0f;      // seconds between the last two orgasms; 0 for the first
		float climaxStart = 0.0f;
		float climaxUntil = 0.0f;
		float afterglowUntil = 0.0f;  // this actor's own afterglow; a partner's orgasm does not start one
		// The climax face the Director picked from its pool for this orgasm, held through it; the next orgasm picks another.
		int climaxVariant = -1;
		int climaxKind = -1;      // 0 long, 1 short, 2 rapid long, 3 rapid short; -1 when the built-in template is used
		int lastClimaxVariant = -1;
		int lastClimaxVariant2 = -1;  // the one before that
		float climaxVariantAt = -1.0f;  // the climaxStart the pick was made for
		bool climaxFromPool = false;    // this beat's climax face came from the pool, not the built-in template
		std::string climaxName;

		// The Director's own build-up faces (Face/Buildup): the stage the last pick was made for, the option, and the last few options,
		// which are not picked again.
		int buildStage = -1;
		int buildOption = -1;
		std::array<int, 8> buildRecent{ -1, -1, -1, -1, -1, -1, -1, -1 };
		int buildRecentPos = 0;
		std::string jsonEvent;
		float jsonUntil = 0.0f;

		// The Director playing OStim's expression pools: the face built up from the picks so far, which
		// pool it came from, and when the next pick is due.
		std::array<float, 32> libState = [] {
			std::array<float, 32> st{};
			st[30] = -1.0f;
			return st;
		}();
		bool libHave = false;
		bool libFallback = false;  // playing the stimulation pool because the scene names no act for an aroused actor
		const void* libPool = nullptr;
		const void* libLast = nullptr;
		std::string libLastName;
		float libNextPick = 0.0f;

		// The override pool (OStim's oral and kiss sets) the Director plays for this actor: the face it has built up,
		// which parts of the face it owns while it lasts, and whether it has put the tongue out.
		std::array<float, 32> libOvr = [] {
			std::array<float, 32> st{};
			st[30] = -1.0f;
			return st;
		}();
		int libOvrMask = 0;
		const void* libOvrPool = nullptr;
		const void* libOvrLast = nullptr;
		std::string libOvrName;
		float libOvrNext = 0.0f;
		bool libTongue = false;
		bool noOverride = false;  // OStim's override expressions were switched off for this actor as well

		// A blowjob's mouth handed to PPA: whether it is handed over now, whether PPA has been seen driving it, what
		// the phonemes looked like when it was handed over (a change from that is PPA at work), when the hand-over
		// began, and, after one that came to nothing, when to try again.
		bool ppaYield = false;
		bool asleepShown = false;  // the lids were shut for a sleeping actor (Assist and Enhanced), so they are opened again when that stops
		bool ppaSeen = false;
		bool ppaGaveUp = false;
		bool ppaBaseSet = false;
		float ppaSince = 0.0f;
		float ppaRetryAt = 0.0f;
		std::array<float, 16> ppaBase{};

		// normal (pre-animation) layer
		std::string normalMood = "Idle";
		std::string hintKind;
		float hintStrength = 0.0f;
		float hintUntil = 0.0f;

		// external lip-sync mouth claim (SLED_LipSyncMouth)
		float externalMouthUntil = 0.0f;

		// head look marker (XMarker placed once per slot)
		RE::ObjectRefHandle marker;

		// status for the UI
		int enj = 0;
		int raw = 0;
		int dom = kAnticipation;
		int phrase = 0;
		int arch = 0;
		std::string archSource;
		std::string faceOwner = "Idle";
		std::string mouthOwner = "Idle";
		std::string eyeOwner = "Idle";
		std::string headOwner = "Idle";

		[[nodiscard]] RE::Actor* Get() const { return handle.get().get(); }
	};

	struct Thread
	{
		int id = -1;
		bool active = false;
		bool hasPlayer = false;
		bool actorsKnown = false;

		std::string sceneID;
		OStimData::ScenePtr meta;
		std::vector<Slot> slots;

		float start = 0.0f;
		int speed = 0;
		int maxSpeed = 4;
		int stageSeq = 0;
		bool consent = true;
		bool leadin = false;
		bool orgasm = false;
		int orgCount = 0;
		int afterglow = 0;
		float afterglowUntil = 0.0f;  // wall-clock end, so repeated climaxes cannot pin it
		int plateau = 0;
		bool toneForced = false;  // forced/rape/aggressive tags: non-consent when Aggressor grammar is on
		bool toneRough = false;   // consensual rough play / BDSM (never set together with toneForced)
		bool victimKnown = false; // non-consent and at least one actor identified as the victim
		std::vector<RE::FormID> spellVictims;  // started by the player's spell (SpellCast): the NPCs it hit, latched when the actors are first known
		bool spellNonConsent = false;          // bSpellNonConsent and a spell victim is in the thread: they are the victims, everyone else an aggressor
		bool acceptedBySubmissive = false;     // would be non-consent, but every victim is a submissive close to everyone else in it: plays as consensual
		bool toneLoving = false;
		bool sceneOral = false;
		bool gasp = false;
		int tick = 0;
		int lastVariant = -1;
		bool dialogueMenuOpen = false;

		bool normalActive = false;
		bool normalPreWindow = false;
		bool normalAnimStarted = false;
		std::string normalProbe = "idle";

		float nextTick = 0.0f;

		[[nodiscard]] Slot* Find(RE::Actor* a);
		[[nodiscard]] Slot* FindPos(int pos);
		[[nodiscard]] int PaintedCount() const;
		[[nodiscard]] bool SpellVictim(const Slot& s) const;
	};

	// Seconds since the plugin loaded (Utility.GetCurrentRealTime equivalent).
	[[nodiscard]] float Now();

	void Init();          // kDataLoaded: event sink
	void Tick();          // main thread, ~20 Hz from the scheduler
	void OnGameLoad();    // forget threads, restore OStim face writers
	void OnDisabled();    // master switch turned off

	// Watcher trial actor (a nearby non-scene NPC); owned by the engine.
	[[nodiscard]] Thread* PlayerThread();
	[[nodiscard]] Thread* ThreadOf(RE::Actor* a_actor);
	[[nodiscard]] bool InAnyScene(RE::Actor* a_actor);
	[[nodiscard]] bool AnyActive();  // an OStim scene is running
	// Every actor of every active thread, with its slot. The caller holds Scenes::Lock().
	void ForEachActor(const std::function<void(RE::Actor*, const Slot&)>& a_fn);

	// UI snapshot
	struct SlotStatus
	{
		std::string name, face, mouth, eye, head, arch, archSource, mood;
		int enj, raw, dom, phrase;
		bool painted, takenOver, exprOverride;
	};
	struct ThreadStatus
	{
		int id;
		std::string scene;
		bool player, consent, rough, normal, orgasm, oral, spell, accepted;
		int speed, maxSpeed, afterglow, plateau;
		float time;
		std::string probe;
		std::vector<SlotStatus> slots;
	};
	[[nodiscard]] std::vector<ThreadStatus> Snapshot();

	std::recursive_mutex& Lock();
}
