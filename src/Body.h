// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

#include "Pulse.h"

// Port of OSED_Body.psc: toe curl and hand grip at climax. The Papyrus version pushed
// NiOverride transforms of 0.4-1.5 degrees that the animation overwrote every frame, so
// nothing was visible. Here the curl is applied to the bone after each animation update.
namespace Body
{
	void OnClimaxPeak(RE::Actor* a_actor, float a_magnitude);   // 0..1
	void OnPaint(const Pulse::Beat& a_beat);                    // every arc: sustained foot flex
	// How far along the genital chain should be on this actor, 0-1. Held until set again, eased
	// in and out, applied every frame like the curl. Male bodies only; ignored without the bones.
	void SetGenitalResponse(RE::Actor* a_actor, float a_level);
	void TestGenitals(RE::Actor* a_actor);
	void Test(RE::Actor* a_actor);                              // MCM crosshair / scene test
	void ClearActor(RE::Actor* a_actor);
	void ClearAll();

	void Update(RE::Actor* a_actor, float a_delta);             // animation hook

	[[nodiscard]] std::string Status();
}
