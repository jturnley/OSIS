#pragma once

// Lip-sync that follows the moans OStim actually plays.
//
// The Papyrus add-on baked FaceFX .lip files and re-played its own clips through dialogue
// topics. The generated file names were truncated by the engine, so nothing ever played.
// Here:
//  1. At data load, OStim's voice-set JSONs name the moan/climax sound descriptors. Their
//     sound files are decoded once (PCM .wav) into loudness + brightness envelopes.
//  2. Each tick, the audio manager's list of sounds that follow a 3D object is checked for
//     one of those files attached to a scene actor. Its playback position starts the
//     actor's mouth track, which Face::Output plays back every frame.
// No bake, no FaceFX, no extra audio.
namespace LipSync
{
	void OnDataLoaded();   // parse voice sets, start background decoding
	void Poll();           // main thread

	[[nodiscard]] std::string Status();
	[[nodiscard]] std::size_t EnvelopeCount();
}
