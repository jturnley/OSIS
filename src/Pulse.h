#pragma once

// The pulse bus. In the Papyrus mods the core broadcast SLED_* mod events and every
// add-on parsed "threadID|formID" strings to find its actor. Here the add-ons are called
// directly; the same SLED_* events are still sent to Papyrus listeners when
// General::bPulseBus is on, so third-party mods that listened keep working.
namespace Pulse
{
	struct Beat
	{
		RE::Actor* actor = nullptr;
		int thread = -1;
		int enj = 0;          // effective intensity 0..130 (SLED_Paint numArg)
		int raw = 0;          // OStim excitement 0..100
		int dom = 0;          // Scenes::Dom
		int phrase = 0;       // 0..4
		bool consent = true;
		bool yieldMouth = false;
		bool orgasm = false;
		float sceneTime = 0.0f;
	};

	void Paint(const Beat& a_beat);                          // every arc, per actor
	void DomChanged(const Beat& a_beat, int a_previous);     // SLED_ClimaxPeak / Afterglow / Distress / Rise
	void PhraseChanged(const Beat& a_beat);                  // SLED_Beat
	void Climax(RE::Actor* a_actor, int a_thread);           // OStim orgasm event
	void ClearActor(RE::Actor* a_actor, int a_thread);       // SLED_ClearActor
	void SceneEnd(int a_thread);                             // SLED_SceneEnd

	// Raw event for third-party listeners only.
	void Emit(const char* a_event, int a_thread, RE::Actor* a_actor, float a_value);
}
