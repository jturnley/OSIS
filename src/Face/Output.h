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

	// Mfg preset layout: [0..15] phonemes, [16..29] modifiers, [30] mood, [31] strength.
	// Blink (16/17) is never written so natural blinking keeps working.
	void ApplyPreset(RE::Actor* a_actor, const std::array<float, 32>& a_preset, bool a_skipPhonemes,
		float a_exprStr, float a_modStr, float a_phStr, float a_speed);

	// Stop writing the mouth (phonemes) so OStim's oral override, dialogue lip-sync or
	// another mod can drive it. Taking it back resumes from the live values.
	void SetMouthOwned(RE::Actor* a_actor, bool a_owned);

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

	// Called from the per-frame animation hooks.
	void Update(RE::Actor* a_actor, float a_delta);
	// Fallback when the NPC animation hook couldn't be installed: every painted NPC, from
	// the main-thread heartbeat.
	void UpdateNPCs(float a_delta);
}
