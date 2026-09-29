#pragma once

// Port of OSED_Body.psc: toe curl and hand grip at climax. The Papyrus version pushed
// NiOverride transforms of 0.4-1.5 degrees that the animation overwrote every frame, so
// nothing was visible. Here the curl is applied to the bone after each animation update.
namespace Body
{
	void OnClimaxPeak(RE::Actor* a_actor, float a_magnitude);   // 0..1
	void Test(RE::Actor* a_actor);                              // MCM crosshair / scene test
	void ClearActor(RE::Actor* a_actor);
	void ClearAll();

	void Update(RE::Actor* a_actor, float a_delta);             // animation hook

	[[nodiscard]] std::string Status();
}
