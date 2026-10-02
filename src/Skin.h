// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

#include "Pulse.h"

// Port of OSED_LivingSkin.psc: face overlays (blush, tears, saliva) through
// RaceMenu's "Face [Ovl#]" slots, plus the brow/squint "welling eyes" fallback when no
// tear texture is installed. Tears belong to non-consensual scenes only: the actor judged
// the victim wells up when distress starts and again at a forced climax (20 s apart at
// most). Consensual scenes get blush and saliva, never tears. Overlay opacities were 0.03-0.2 in the original and read as
// nothing in game; they are now scaled to be visible. The sweat overlay was dropped: a
// face-only texture never lined up with the body.
namespace Skin
{
	void OnDataLoaded();

	void OnPaint(const Pulse::Beat& a_beat);
	void OnClimaxPeak(RE::Actor* a_actor);
	void OnAfterglow(RE::Actor* a_actor);
	void OnDistress(RE::Actor* a_actor, bool a_victim);
	void ClearActor(RE::Actor* a_actor);
	void ClearAll();
	void Tick();  // expire timed overlays

	void TestTear(RE::Actor* a_actor);
	void TestSaliva(RE::Actor* a_actor);
	void TestBlush(RE::Actor* a_actor);

	// Emotional Tears Effect: its tear ability goes on a crying victim until the distress ends.
	// Abilities persist in saves, so the actors carrying one are kept in the cosave and stripped
	// on the next load.
	[[nodiscard]] bool EmoTearsFound();
	[[nodiscard]] std::vector<RE::FormID> EmoTearIDs();
	void SetEmoTearIDs(std::vector<RE::FormID> a_ids);

	[[nodiscard]] int FaceOverlaySlots();
	// How many slots the enabled effects with a texture actually need.
	[[nodiscard]] int FaceSlotsNeeded();   // skee64.ini [Overlays/Face] iNumOverlays
	[[nodiscard]] std::string Status();
	[[nodiscard]] std::string ResolvedPath(int a_effect);  // 0 blush, 1 saliva, 2 tear
}
