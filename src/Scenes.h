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
		std::string voiceName;
		bool voiceRequested = false;
		float excitementFactor = 1.0f;  // consent: our scale on OStim's excitement rate (1 = untouched)
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
		float tongueLifeUntil = 0.0f;
		float tongueLifeNext = 0.0f;
		float tonguePrimeUntil = 0.0f;
		float tongueHoldUntil = 0.0f;
		float tongueCooldownUntil = 0.0f;
		int lastClimax = 0;
		std::string jsonEvent;
		float jsonUntil = 0.0f;

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
		int orgTicks = 0;
		int orgCount = 0;
		int afterglow = 0;
		int plateau = 0;
		bool toneForced = false;  // forced/rape/aggressive tags: non-consent when Aggressor grammar is on
		bool toneRough = false;   // consensual rough play / BDSM (never set together with toneForced)
		bool victimKnown = false; // non-consent and at least one actor identified as the victim
		std::vector<RE::FormID> spellVictims;  // started by the player's spell (SpellCast): the NPCs it hit, latched when the actors are first known
		bool spellNonConsent = false;          // bSpellNonConsent and a spell victim is in the thread: they are the victims, everyone else an aggressor
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
		bool player, consent, rough, normal, orgasm, oral, spell;
		int speed, maxSpeed, afterglow, plateau;
		float time;
		std::string probe;
		std::vector<SlotStatus> slots;
	};
	[[nodiscard]] std::vector<ThreadStatus> Snapshot();

	std::recursive_mutex& Lock();
}
