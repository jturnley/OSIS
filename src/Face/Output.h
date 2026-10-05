// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// Per-actor face targets and the per-frame writer.
//
// The engine (Director / Assist / lip-sync / watcher) sets targets on the game thread at
// its own cadence, the way the Papyrus scripts called MfgConsoleFuncExt. Every animation
// update then eases the live face toward those targets and writes BSFaceGenAnimationData
// directly. Unlike the original 1-3 s Mfg writes this never loses a fight with OStim's own
// face updater, and transitions are smooth instead of stepped.
//
// Ids follow the vanilla MFG console: phonemes 0-15, modifiers 0-13, expressions 0-16.
// Values are 0..1. Speed is the Mfg-style transition time in seconds.
namespace Face::Output
{
	constexpr int kPhonemes = 16;
	constexpr int kModifiers = 14;
	constexpr int kExpressions = 17;

	// modifier ids
	enum Mod : int
	{
		kBlinkL = 0,
		kBlinkR,
		kBrowDownL,
		kBrowDownR,
		kBrowInL,
		kBrowInR,
		kBrowUpL,
		kBrowUpR,
		kLookDown,
		kLookLeft,
		kLookRight,
		kLookUp,
		kSquintL,
		kSquintR,
	};

	// phoneme ids
	enum Ph : int
	{
		kAah = 0,
		kBigAah = 1,
		kBMP = 2,
		kEee = 5,
		kOh = 11,
	};

	void SetPhoneme(RE::Actor* a_actor, int a_id, float a_value, float a_speed);
	void SetModifier(RE::Actor* a_actor, int a_id, float a_value, float a_speed);
	void SetMood(RE::Actor* a_actor, int a_mood, float a_strength, float a_speed);
	void ResetPhonemes(RE::Actor* a_actor, float a_speed);
	void ResetModifiers(RE::Actor* a_actor, float a_speed);
	// Ease the mood (expression) channels out and then stop writing them, so a mood we set earlier
	// - the Director's, or a shock - does not keep overwriting OStim's own for the rest of the
	// scene. Without it, every frame writes all 17 expression channels once any of them was used.
	void ReleaseMood(RE::Actor* a_actor, float a_speed);

	// Mfg preset layout: [0..15] phonemes, [16..29] modifiers, [30] mood, [31] strength.
	// Blink (16/17) is never written so natural blinking keeps working.
	void ApplyPreset(RE::Actor* a_actor, const std::array<float, 32>& a_preset, bool a_skipPhonemes,
		float a_exprStr, float a_modStr, float a_phStr, float a_speed);

	// Stop writing the mouth (phonemes) so OStim's oral override, dialogue lip-sync or
	// another mod can drive it. Taking it back resumes from the live values.
	void SetMouthOwned(RE::Actor* a_actor, bool a_owned);
	// Hold the jaw at least this far open and keep the lips apart, whatever else is driving the
	// mouth. Used while a tongue is out, where a closing mouth pushes it through the lips. 0 off.
	void SetMouthFloor(RE::Actor* a_actor, float a_floor);
	// Write nothing at all for this actor while another mod owns its face. The channels are
	// kept, so painting resumes where it left off; the other mod's values simply stand.
	// OStim's own face writer is running for this actor, so the face is shared. Writes then add to
	// whatever OStim has put in a channel (the larger of the two) instead of replacing it, and a
	// channel we stop using is handed back as OStim had it. Off, OSIS owns the face outright.
	void SetLayered(RE::Actor* a_actor, bool a_layered);
	void SetSuspended(RE::Actor* a_actor, bool a_suspended);
	[[nodiscard]] bool IsSuspended(RE::Actor* a_actor);

	// Lip-sync layer: a loudness/brightness envelope decoded from the moan file that is
	// playing on the actor. While it plays it replaces the base mouth.
	struct Envelope
	{
		float frameSeconds = 0.01f;
		std::vector<float> loud;    // 0..1 per frame
		std::vector<float> bright;  // 0..1 per frame (zero-crossing rate: breathy / "ee")
		[[nodiscard]] float Duration() const { return frameSeconds * static_cast<float>(loud.size()); }
	};

	struct TrackParams
	{
		float gain = 1.0f;
		float minOpen = 0.0f;   // the mouth never closes past this (used to clear an out tongue)
		float maxOpen = 0.85f;
		float attack = 0.04f;
		float release = 0.12f;
		bool holdEyes = true;
	};

	void SetMouthTrack(RE::Actor* a_actor, std::shared_ptr<const Envelope> a_envelope, float a_startRealTime, const TrackParams& a_params);
	void ClearMouthTrack(RE::Actor* a_actor);
	[[nodiscard]] bool HasMouthOverride(RE::Actor* a_actor);  // a lip-sync track is playing

	// Ease everything back to neutral, then stop writing (ResetMfg equivalent).
	void Release(RE::Actor* a_actor, float a_speed = 0.6f);
	void ReleaseAll(float a_speed = 0.6f);
	// Drop state without touching the face (the actor's 3D is gone).
	void Forget(RE::FormID a_id);
	void ForgetAll();

	[[nodiscard]] bool IsPainted(RE::Actor* a_actor);
	[[nodiscard]] std::size_t PaintedCount();

	// Diagnostics. For a_seconds of face writes (the clock starts at the next write, so time
	// spent in a menu does not count), log once a second per actor: what we wrote, what the
	// game finally rendered, what dialogue lip-sync contributed, and how often anything else
	// changed the face between two of our writes. Separates "the engine asks for a flat face"
	// from "something overwrites it" with numbers instead of guesses.
	void ArmProbe(float a_seconds);
	[[nodiscard]] bool Probing();

	// Called from the per-frame animation hooks.
	void Update(RE::Actor* a_actor, float a_delta);
	// Called just before the game's own animation update for this actor, which is where it reads the
	// face. With OStim's writer on (layered) it puts our last values back over anything OStim wrote
	// since, so they are what the game reads. Does no easing and writes only what Update last did.
	void Reassert(RE::Actor* a_actor);
	// Fallback when the NPC animation hook couldn't be installed: every painted NPC, from
	// the main-thread heartbeat.
	void UpdateNPCs(float a_delta);
}
