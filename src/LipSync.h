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
	// For the debug trace: the clip this actor's mouth is following, and why it is not following one. Main thread.
	[[nodiscard]] std::string Describe(RE::Actor* a_actor);
	[[nodiscard]] std::size_t EnvelopeCount();

	// For Voice: is this sound file one of OStim's voice-set sounds (moans, climax, reactions,
	// muffled ones included)? Safe from the audio thread once data has loaded.
	[[nodiscard]] bool IsMoanResource(const RE::BSResource::ID& a_id);

	// How loud a voice-set sound file is, measured from its waveform when it was decoded (the RMS of its loud parts, dBFS), and which third it is in:
	// across every decoded voice-set file, the quietest third is class 0 (quiet), the middle third 1 (normal) and the loudest third 2 (loud). Known once
	// the decoding has finished; safe from the audio thread after that.
	struct Level
	{
		bool known = false;
		float db = 0.0f;
		int cls = 1;
	};
	[[nodiscard]] Level LevelOf(const RE::BSResource::ID& a_id);
	// The path (relative to Data) of a voice-set sound file, for the debug trace; empty when the file is not one a voice set names.
	[[nodiscard]] std::string PathOf(const RE::BSResource::ID& a_id);
	struct LevelScale
	{
		bool ready = false;
		float quietBelow = 0.0f;       // dBFS: files below this are quiet
		float loudFrom = 0.0f;         // files from this are loud
		std::array<float, 3> median{};  // the middle file of each third, dBFS: what "quiet", "normal" and "loud" sound like
		std::array<std::size_t, 3> count{};
	};
	[[nodiscard]] LevelScale LevelScaleInfo();
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
		int tries = 1;  // sounds started to get one of the loudness classes asked for
	};
	// a_classMask asks for a file of some loudness classes (bit 0 quiet, bit 1 normal, bit 2 loud; 0 any): a descriptor holds many files and the engine picks one at random, so a try of
	// the wrong class is muted by the start hook (MoanVolume), stopped here, and another is made, up to six times; the last is played whatever it is.
	[[nodiscard]] Played PlayDescriptor(RE::Actor* a_actor, RE::FormID a_descriptor, float a_volume, unsigned a_classMask = 0);
	// For the start hook, which runs inside Play() on the thread that called PlayDescriptor: the classes this try must be one of (0: any), and the hook's word that it is not.
	[[nodiscard]] unsigned WantedClasses();
	void NoteRejected();
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
