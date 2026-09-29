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
	[[nodiscard]] int Archetype(RE::Actor* a, std::string* a_source = nullptr);
	[[nodiscard]] const char* PersonalityName(int a_arch);
	[[nodiscard]] int GetNpcPersonality(RE::Actor* a);
	void SetNpcPersonality(RE::Actor* a, int a_arch);   // -1 clears
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
}
