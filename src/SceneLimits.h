// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

#include "Scenes.h"

// Scene limits by personality (both editions). In a consensual scene, until an actor's own first orgasm in the thread:
//  - a passive personality (shy, timid, submissive) plays only scenes that do not put it on top or in charge;
//  - an aggressive one (dominant, crazed) plays only scenes that do.
// A scene gives each actor a role: active (on top, penetrating, dominating), passive (underneath, receiving) or neutral. The roles are read from OStim's scene data (Classify):
// the scene's actor tags (ontop / onbottom), its scene tags (cowgirl, missionary, doggy...), the postures, and the actions (who penetrates whom). Foreplay, kissing, oral, a handjob,
// transitions to scenes that are one of those, and anything the data does not settle are neutral, and always allowed. Your own corrections go in
// Data/SKSE/Plugins/OSIS/scene_roles_user.txt.
// It is enforced the way the non-consensual scene lock is (SceneLock.h, full edition), but for a limit that lifts:
//  - the player's scene menu is trimmed in place to the options that fit (never to nothing);
//  - anything that lands on a scene that does not fit (OStim's auto mode, the search menu, another mod) is navigated to the nearest that does, with a line on screen for the player;
//  - OStim's auto mode can be taken over to pick among scenes that fit (bTakeOverAuto, off by default: it loses OStim's pacing, and a scene with one actor is never limited).
// Two actors that want the same role (two aggressive, or two passive) cannot both have it, and limiting either to the scenes that suit them leaves very few, so neither is limited.
// The player's own personality limits nothing unless bPlayer is on.
namespace SceneLimits
{
	enum class Role : std::uint8_t
	{
		kNeutral = 0,
		kActive = 1,   // on top, penetrating, dominating
		kPassive = 2,  // underneath, receiving
	};

	// Face engine callbacks (Scenes lock and Settings::lock held)
	void OnThreadReady(Scenes::Thread& t);
	void OnSceneChanged(Scenes::Thread& t);
	void OnTick(Scenes::Thread& t);  // personalities change and orgasms lift limits
	void OnSceneEnd(Scenes::Thread& t);

	void Tick();   // main thread
	void Clear();  // game load

	// The role a scene gives each actor position (a transition gives its destination's). Empty when the scene cannot be read.
	[[nodiscard]] std::vector<Role> RolesOf(std::string_view a_scene);
	// Why the roles are what they are, for the log and the debug trace.
	[[nodiscard]] std::string ExplainRoles(std::string_view a_scene);

	// Whether a scene fits the limits the thread has now (true when it has none).
	[[nodiscard]] bool Allowed(int a_thread, std::string_view a_scene);
	[[nodiscard]] std::string Describe(RE::Actor* a_actor);  // for the debug snapshot: this actor's limit, or why it has none
	[[nodiscard]] std::string Status();
}
