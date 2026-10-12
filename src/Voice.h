// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

#include "Scenes.h"

// The victim's voice in a non-consensual scene. OStim's moans and climax sounds are muted on the
// victim (volume zeroed as they start; a per-tick sweep catches any that slip through). Instead the
// victim cries for help when the scene starts, and guards or allies in range come for the
// aggressor (with the assault on the player's bounty when a guard saw it); the victim herself
// stays in the scene.
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
	[[nodiscard]] bool OwnClimax();                // the takeover also owns the climax sounds, so those are muted on the same actors
	[[nodiscard]] bool IsPlainMuted(RE::Actor* a); // only OStim's plain moans on this actor are muted (the Director takeover plays its own); climax and reactions are not
	[[nodiscard]] std::string Status();

	// Menu tests on the crosshair actor
	void TestHelp(RE::Actor* a);
	void TestLine(RE::Actor* a);
	void TestScream(RE::Actor* a);
	void TestBreath(RE::Actor* a);
}
