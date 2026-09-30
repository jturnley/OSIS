#pragma once

#include "Scenes.h"

// The victim's voice in a non-consensual scene. OStim's moans and climax sounds are muted on the
// victim (volume zeroed as they start; a per-tick sweep catches any that slip through). Instead the
// victim cries for help when the scene starts, and guards or allies in range answer (an assault
// alarm when the player is the aggressor, combat against an NPC aggressor, as ODragonSeed does).
// Then come lines that fit their personality, from curses to pleading, a scream and a shocked
// face at the climax that breaks them, and after that only hard breathing. Every line is vanilla
// Skyrim.esm dialogue spoken in the actor's own voice type through SpeakSound; nothing is shipped.
namespace Voice
{
	void InstallHooks();   // sound start hook (SKSE load)
	void OnDataLoaded();
	void Tick();           // main thread, from the scheduler
	void Clear();          // game load

	// Face engine callbacks (Scenes lock and Settings::lock held)
	void Sync(Scenes::Thread& t);                  // track the thread's victims
	void OnBreak(Scenes::Thread& t, RE::Actor* a);  // the victim climaxed and broke: scream
	void OnSceneEnd(Scenes::Thread& t);

	[[nodiscard]] bool IsSilenced(RE::Actor* a);   // OStim's moans on this actor are muted
	[[nodiscard]] std::string Status();

	// Menu tests on the crosshair actor
	void TestHelp(RE::Actor* a);
	void TestLine(RE::Actor* a);
	void TestScream(RE::Actor* a);
	void TestBreath(RE::Actor* a);
}
