// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

#include "Scenes.h"

// Port of OSExpressionFaces.psc: the mood/timing grammar, the Assist/Enhanced OStim layer,
// the normal-state (pre-animation) layer, anime accents and tongue, gaze/headflow,
// personality archetypes and the watcher trial. Called by Scenes on the main thread.
namespace Face::Engine
{
	void OnDataLoaded();

	// lifecycle (Scenes calls these)
	void OnThreadReady(Scenes::Thread& t);                    // actors known for the first time
	void OnSceneChanged(Scenes::Thread& t);                   // scene id changed
	void OnOrgasm(Scenes::Thread& t, RE::Actor* a_actor);
	void OnTick(Scenes::Thread& t);                           // OnUpdate equivalent
	void EndScene(Scenes::Thread& t);
	void RefreshDerived(Scenes::Thread& t, bool a_sceneChanged);  // metadata-derived flags
	[[nodiscard]] float TickInterval(const Scenes::Thread& t);

	// OStim face writer ownership
	void ReleaseOStimFace(Scenes::Slot& s, RE::Actor* a);
	void RestoreOStimFace(Scenes::Slot& s, RE::Actor* a);
	void RestorePersistedTakeovers();   // on load / disable: re-enable everyone we ever switched off
	[[nodiscard]] std::vector<RE::FormID> TakenOverIDs();
	void SetTakenOverIDs(std::vector<RE::FormID> a_ids);

	// personality
	// 0-4 are the original five. The newer four are built on them: a new type uses `BasePersonality` - the original one it is closest to -
	// wherever it has no rule of its own, so timid acts as a shy, wild as a vocal, crazed as a dominant and submissive as a balanced, and
	// the code that is specific to the new type (eyes kept shut, a fixed stare, a faster build, accepting a rough scene) comes on top.
	// Wild is built on vocal (the strength and tempo of someone who shows everything), crazed on dominant, timid and submissive on balanced.
	namespace Pers
	{
		inline constexpr int kBalanced = 0, kStoic = 1, kVocal = 2, kShy = 3, kDominant = 4;
		inline constexpr int kTimid = 5, kSubmissive = 6, kWild = 7, kCrazed = 8;
		inline constexpr int kCount = 9;
	}
	[[nodiscard]] constexpr int BasePersonality(int a_arch)
	{
		switch (a_arch) {
		case Pers::kTimid: return Pers::kBalanced;
		case Pers::kWild: return Pers::kVocal;
		case Pers::kCrazed: return Pers::kDominant;
		case Pers::kSubmissive: return Pers::kBalanced;
		default: return a_arch >= 0 && a_arch <= Pers::kDominant ? a_arch : Pers::kBalanced;
		}
	}
	// Submissive is in the full edition only: the lite edition has no non-consent, which is what it is about.
	[[nodiscard]] constexpr bool PersonalityAvailable(int a_arch)
	{
#if OSIS_LITE
		if (a_arch == Pers::kSubmissive) return false;
#endif
		return a_arch >= 0 && a_arch < Pers::kCount;
	}
	// What the lite edition plays someone who is, or would be, a submissive as: a timid, the nearest thing it has (it enjoys it, but is
	// uneasy with closeness). Everywhere a submissive could come from - a keyword, a voice, the roll, a save, an INI - goes through this.
	[[nodiscard]] constexpr int EditionPersonality(int a_arch)
	{
#if OSIS_LITE
		if (a_arch == Pers::kSubmissive) return Pers::kTimid;
#endif
		return a_arch;
	}
	[[nodiscard]] int Archetype(RE::Actor* a, std::string* a_source = nullptr);
	// This actor's climax is being held by a dominant (UpdateClimaxControl): they wait at the edge, and the arousal response does not treat
	// that as an orgasm about to come.
	[[nodiscard]] bool IsClimaxHeld(RE::Actor* a);
	[[nodiscard]] const char* PersonalityName(int a_arch);

	// Consent: how the victim of a non-consensual scene reacts, from their personality.
	// Dominant resists with anger, shy freezes in fear, vocal panics, stoic endures numbly,
	// balanced moves from sadness to fear as OStim excitement rises.
	enum class Reaction { kBalanced, kFear, kPanic, kNumb, kDefiance };
	[[nodiscard]] Reaction VictimReaction(int a_arch);
	[[nodiscard]] const char* ReactionName(Reaction a_reaction);
	[[nodiscard]] bool VictimCries(RE::Actor* a);  // tears belong to every reaction but defiance
	[[nodiscard]] int GetNpcPersonality(RE::Actor* a);   // what the player set for this NPC, -1 if nothing
	[[nodiscard]] int StoredPersonality(RE::Actor* a);    // the cosave entry as stored (low byte + pin flag), -1 if none
	void SetNpcPersonality(RE::Actor* a, int a_arch);   // -1 clears; the next scene works it out again and pins that
	std::size_t ForgetPinnedPersonalities();            // drop what OSIS pinned by itself, keep what the player set
	[[nodiscard]] std::unordered_map<RE::FormID, int> NpcPersonalities();
	void SetNpcPersonalities(std::unordered_map<RE::FormID, int> a_map);

	// helpers shared with add-ons
	[[nodiscard]] bool IsHuman(RE::Actor* a);
	[[nodiscard]] int Excitement(RE::Actor* a);        // OStimExcitementFaction rank, 0 outside scenes
	[[nodiscard]] int TimesClimaxed(RE::Actor* a);
	[[nodiscard]] bool MouthYielded(Scenes::Thread& t, Scenes::Slot& s, RE::Actor* a);
	[[nodiscard]] float StyleValue();

	// external hints (SLED_Hint / SLED_LipSyncMouth mod events from other mods)
	void StoreHint(RE::Actor* a, std::string_view a_kind, int a_strength);
	void SetExternalMouth(RE::Actor* a, float a_untilRealTime);

	// MCM "Test on crosshair"
	void TestOnActor(RE::Actor* a);

	// status for the UI
	[[nodiscard]] std::string WatcherStatus();
	[[nodiscard]] bool OStimPresent();
	[[nodiscard]] bool OBlushPresent();
	[[nodiscard]] bool DevicesPresent();
	[[nodiscard]] std::string AhegaoStatus();
	// Ahegao Expressions is installed. Separate from the yield, which can be forced on without it.
	// Take back any tongue this mod put out. Safe at any time; a tongue someone else equipped
	// is left alone.
	void ClearStrayTongues();
	[[nodiscard]] bool AhegaoPresent();
	// No module may write a face: an ahegao mod owns them all for this session.
	[[nodiscard]] bool FaceYielded();
}
