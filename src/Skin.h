#pragma once

#include "Pulse.h"

// Port of OSED_LivingSkin.psc: face overlays (blush, tears, saliva) through
// RaceMenu's "Face [Ovl#]" slots, plus the brow/squint "welling eyes" fallback when no
// tear texture is installed. Overlay opacities were 0.03-0.2 in the original and read as
// nothing in game; they are now scaled to be visible. The sweat overlay was dropped: a
// face-only texture never lined up with the body.
namespace Skin
{
	void OnDataLoaded();

	void OnPaint(const Pulse::Beat& a_beat);
	void OnClimaxPeak(RE::Actor* a_actor);
	void OnAfterglow(RE::Actor* a_actor);
	void OnDistress(RE::Actor* a_actor);
	void ClearActor(RE::Actor* a_actor);
	void ClearAll();
	void Tick();  // expire timed overlays

	void TestTear(RE::Actor* a_actor);
	void TestSaliva(RE::Actor* a_actor);
	void TestBlush(RE::Actor* a_actor);

	[[nodiscard]] int FaceOverlaySlots();   // skee64.ini [Overlays/Face] iNumOverlays
	[[nodiscard]] std::string Status();
	[[nodiscard]] std::string ResolvedPath(int a_effect);  // 0 blush, 1 saliva, 2 tear
}
