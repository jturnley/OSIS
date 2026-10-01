// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

#include "Scenes.h"

// Non-consensual threads play non-consensual scenes only (scene tags forced, rape, aggressive...).
// OStim has no tag filter for navigation (its furniture filter is keyed to furniture types), so:
//  - OStim's auto mode is replaced on these threads by one that picks among non-consensual
//    scenes, using OStim's own scene library (actor and furniture compatible);
//  - the player's scene menu is trimmed in place to the options that lead to one;
//  - anything else that lands on a consensual scene (the search menu, another mod) is
//    navigated back to the nearest non-consensual one.
// A thread is locked when it starts non-consensual: a forced-tagged starting scene, the
// player's spell, or non-consent thread metadata from the mod that started it.
namespace SceneLock
{
	// Face engine callbacks (Scenes lock and Settings::lock held)
	void OnThreadReady(Scenes::Thread& t);
	void OnSceneChanged(Scenes::Thread& t);
	void OnSceneEnd(Scenes::Thread& t);

	void Tick();   // main thread
	void Clear();  // game load

	[[nodiscard]] bool IsLocked(int a_thread);
	[[nodiscard]] bool Allowed(std::string_view a_scene);  // leads to a non-consensual scene (or can't be read)
	[[nodiscard]] std::string Status();
}
