#pragma once

// Softbody Arousal, merged. GT Softbody (CBBE) morphs and Body Blushing overlays follow a
// per-actor tissue level that eases toward a target built from:
//   - the arousal mod (OSL Aroused / SLO Aroused NG), 0-100
//   - OStim excitement while the actor is in a scene (a floor, not an override)
//   - OSED scene state: an orgasm spikes to full engorgement for fClimaxHold seconds,
//     edging (plateau) holds the response high, afterglow keeps it up while resolving
//   - personality: vocal/dominant engorge faster, stoic slower; shy flush harder
// The same level drives Living Skin's face blush, so face and body flush together.
namespace Arousal
{
	struct StatusRow
	{
		std::string name;
		float arousal;   // 0-100 from the arousal mod
		float target;    // 0-1 after scene factors
		float level;     // 0-1 smoothed tissue response
		std::string why; // strongest factor
	};

	void Init();          // kDataLoaded: detect arousal mods and RaceMenu overlay slots
	void OnGameLoad();    // forget per-actor state
	void Tick();          // main thread; runs every fInterval
	void RequestClearAll();

	void OnClimax(RE::Actor* a_actor);              // from the pulse bus
	[[nodiscard]] float Flush(RE::Actor* a_actor);  // 0-1 level x personality flush, for face blush

	[[nodiscard]] int BodyOverlaySlots();
	[[nodiscard]] bool HasOSL();
	[[nodiscard]] bool HasSLO();
	[[nodiscard]] int ActiveSource();   // Settings::Arousal::Source; kAuto means none available
	[[nodiscard]] std::vector<StatusRow> Snapshot();
}
