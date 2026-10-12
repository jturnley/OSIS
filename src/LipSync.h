// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

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

	// For Voice: is this sound file one of OStim's voice-set sounds (moans, climax, reactions,
	// muffled ones included)? Safe from the audio thread once data has loaded.
	[[nodiscard]] bool IsMoanResource(const RE::BSResource::ID& a_id);
	[[nodiscard]] RE::Actor* ActorOf(RE::NiAVObject* a_node);  // the actor a playing sound follows

	// Plays one of OStim's voice-set moans on an actor the way OStim does (BSAudioManager::GetSoundHandle on the
	// descriptor, follow the actor's 3D, Play). The sound is ours: neither the start hook nor the sweep mutes it, and
	// Lip-Sync follows it like any other moan. Returns what happened, for the log and the menu. Main thread.
	std::string PlayMoanOn(RE::Actor* a_actor, float a_volume = 1.0f);

	// The same for one named descriptor. soundID is the engine's id for the sound that was started, for IsSoundPlaying.
	struct Played
	{
		bool ok = false;
		std::uint32_t soundID = 0;
		std::string text;
	};
	[[nodiscard]] Played PlayDescriptor(RE::Actor* a_actor, RE::FormID a_descriptor, float a_volume);
	[[nodiscard]] bool IsSoundPlaying(std::uint32_t a_soundID);

	// The sound files of the voice sets' plain moans (both lists) that no climax, comment or event reaction also uses: what the Director
	// takeover mutes and replaces. a_other is every descriptor that is not a plain moan. Call once, after the voice sets are parsed.
	// a_climax is the same for the climax lists. A file two classes share is in neither set.
	void SetPlainMoans(const std::vector<RE::FormID>& a_plain, const std::vector<RE::FormID>& a_climax, const std::vector<RE::FormID>& a_other);
	[[nodiscard]] bool IsPlainMoanResource(const RE::BSResource::ID& a_id);  // safe from the audio thread once SetPlainMoans has run
	[[nodiscard]] bool IsClimaxResource(const RE::BSResource::ID& a_id);
	void StopSound(std::uint32_t a_soundID);  // stops a sound by its engine id (one of ours giving way to a climax)

	// Scene time until which a voice-set sound that is not ours (a climax, a reaction) is expected to keep playing on this actor; 0 or in
	// the past if none. Only tracked while Lip-Sync is running.
	[[nodiscard]] float VoiceBusyUntil(RE::Actor* a_actor);
	[[nodiscard]] bool PlayingOwn();                       // true while PlayMoanOn is inside Play() (the start hook runs on this thread)
	[[nodiscard]] bool IsOwnSound(std::uint32_t a_soundID);  // a sound PlayMoanOn started recently
}
