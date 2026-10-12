// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Moans.h"

#include "Debug.h"

#include "Face/Engine.h"
#include "LipSync.h"
#include "Scenes.h"
#include "Settings.h"
#include "VoiceModel.h"

namespace Moans
{
	namespace
	{
		struct State
		{
			float nextAt = 0.0f;      // when the next moan may start
			bool playing = false;     // one of ours is playing
			std::uint32_t soundID = 0;
			float playedAt = 0.0f;
			float interval = 0.0f;    // seconds to wait once it has ended
			bool climaxPending = false;  // OStim reported their orgasm and we have not played the climax yet
			std::string gate;            // what the next moan is waiting for, for the debug trace
		};

		std::unordered_map<RE::FormID, State> g_state;  // main thread only
		std::mutex g_statusLock;                        // the counters and text below, read by the menu
		std::uint32_t g_played = 0;
		std::uint32_t g_failed = 0;
		std::uint32_t g_noEntry = 0;    // a list was found but no entry's condition held (or the muffled list was empty)
		std::uint32_t g_noSet = 0;      // no voice set for the actor
		std::uint32_t g_notAllowed = 0; // the scene's actions do not let them moan
		std::uint32_t g_climaxPlayed = 0;
		std::uint32_t g_climaxNone = 0;  // an orgasm with no climax we could play (no set, or no entry fits): OStim's sound is muted too, so it is silent
		std::uint32_t g_climaxLeft = 0;  // sets whose climax has dialogue, left to OStim
		std::string g_last = "none yet";
		std::size_t g_actors = 0;

		struct Cfg
		{
			bool enabled = false;
			float volume = 1.0f;
			float intervalMin = 2.5f;
			float intervalMax = 4.0f;
			bool personality = true;
			bool climax = true;
			bool fromOStim = false;  // interval and volume are OStim's own
		};

		// OStim's own moan settings are its MCM's: three global variables in OStim.esp (Moan interval min / max in milliseconds, Moan volume), the
		// same for every actor. They are looked up once found; a miss is tried again every few seconds, in case the data was not loaded yet.
		struct OStimGlobals
		{
			std::atomic<RE::TESGlobal*> min{ nullptr };
			std::atomic<RE::TESGlobal*> max{ nullptr };
			std::atomic<RE::TESGlobal*> volume{ nullptr };
			std::atomic<std::int64_t> retryAt{ 0 };  // steady_clock milliseconds
		} g_ostim;

		bool ReadOStimSettings(float& a_min, float& a_max, float& a_volume)
		{
			if (!g_ostim.min.load() || !g_ostim.max.load() || !g_ostim.volume.load()) {
				const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
				if (now < g_ostim.retryAt.load()) return false;
				g_ostim.retryAt.store(now + 5000);
				auto* dh = RE::TESDataHandler::GetSingleton();
				if (!dh) return false;
				g_ostim.min.store(dh->LookupForm<RE::TESGlobal>(0xE0A, "OStim.esp"));
				g_ostim.max.store(dh->LookupForm<RE::TESGlobal>(0xE0B, "OStim.esp"));
				g_ostim.volume.store(dh->LookupForm<RE::TESGlobal>(0xE0C, "OStim.esp"));
				if (!g_ostim.min.load() || !g_ostim.max.load() || !g_ostim.volume.load()) return false;
			}
			a_min = std::clamp(g_ostim.min.load()->value / 1000.0f, 0.0f, 30.0f);
			a_max = std::clamp(g_ostim.max.load()->value / 1000.0f, a_min, 30.0f);
			a_volume = std::clamp(g_ostim.volume.load()->value, 0.0f, 1.0f);
			return true;
		}

		Cfg ReadCfg()
		{
			std::scoped_lock l(Settings::lock);
			Cfg c;
			c.enabled = Settings::Voice::bDirectorOwnMoans && !Settings::Voice::bDirectorMuteMoans && Settings::General::bEnabled &&
			            Settings::Face::iMode == Settings::Face::kDirector;
			c.volume = Settings::Voice::fOwnMoanVolume;
			c.intervalMin = Settings::Voice::fOwnMoanIntervalMin;
			c.intervalMax = std::max(c.intervalMin, Settings::Voice::fOwnMoanIntervalMax);
			c.personality = Settings::Voice::bOwnMoanPersonality;
			c.climax = Settings::Voice::bDirectorOwnClimax;
			if (Settings::Voice::bUseOStimMoanSettings) {
				float lo = 0.0f, hi = 0.0f, vol = 1.0f;
				if (ReadOStimSettings(lo, hi, vol)) {
					c.intervalMin = lo;
					c.intervalMax = hi;
					c.volume = vol;
					c.fromOStim = true;
				}
			}
			return c;
		}

		float Rand(float a_lo, float a_hi)
		{
			thread_local std::mt19937 rng{ std::random_device{}() };
			return std::uniform_real_distribution<float>(a_lo, a_hi)(rng);
		}

		// How often a personality moans, as a multiple of the interval: a wild actor moans often, a stoic one hardly ever starts. A vocal actor is
		// not scaled; NextInterval gives it the shortest gap there is.
		float PersonalityScale(int a_arch)
		{
			namespace P = Face::Engine::Pers;
			switch (a_arch) {
			case P::kWild: return 0.6f;
			case P::kStoic: return 2.0f;
			case P::kShy:
			case P::kTimid: return 1.4f;
			default: return 1.0f;
			}
		}

		// The wait after a moan ends. A vocal actor never stops: the next moan follows as soon as this one is done, whatever the voice-set entry or the interval says.
		constexpr float kVocalGap = 0.05f;

		float NextInterval(float a_entryInterval, const Cfg& a_cfg, int a_arch)
		{
			if (a_cfg.personality && a_arch == Face::Engine::Pers::kVocal) return kVocalGap;
			float interval = a_entryInterval > 0.0f ? a_entryInterval : Rand(a_cfg.intervalMin, a_cfg.intervalMax);
			if (a_cfg.personality) interval *= PersonalityScale(a_arch);
			return interval;
		}

		// The climax sound, at the orgasm. OStim plays it whatever the actions say about moaning ("ignoreChecks"), so only the muffled flag matters.
		// Returns false when nothing could be played, in which case the normal moan flow goes on.
		bool PlayClimax(RE::Actor* a_actor, const Scenes::Slot& a_slot, State& a_st, float a_now, const Cfg& a_cfg)
		{
			if (a_actor->IsDead() || !a_actor->Get3D()) return false;
			const auto resolved = VoiceModel::Resolve(a_actor);
			if (!resolved.set) {
				std::scoped_lock l(g_statusLock);
				++g_climaxNone;
				return false;
			}
			if (resolved.set->climaxDialogue) {
				std::scoped_lock l(g_statusLock);
				++g_climaxLeft;
				return false;
			}
			const auto ctx = VoiceModel::Context(a_actor);
			const auto choice = VoiceModel::Select(resolved.set->climax, ctx.muffled, a_actor, ctx.partner);
			if (!choice.entry) {
				std::scoped_lock l(g_statusLock);
				++g_climaxNone;
				return false;
			}
			// A moan of ours that is still sounding gives way to the climax.
			if (a_st.playing) LipSync::StopSound(a_st.soundID);
			const auto played = LipSync::PlayDescriptor(a_actor, choice.entry->sound, a_cfg.volume);
			if (!played.ok) {
				std::scoped_lock l(g_statusLock);
				++g_failed;
				g_last = "climax failed: " + played.text;
				if (g_failed <= 5) logger::warn("Moans: {}", played.text);
				return false;
			}
			const float interval = NextInterval(choice.entry->interval, a_cfg, a_slot.arch);
			a_st.playing = true;
			a_st.soundID = played.soundID;
			a_st.playedAt = a_now;
			a_st.interval = interval;

			std::scoped_lock l(g_statusLock);
			++g_climaxPlayed;
			g_last = std::format("{}: CLIMAX, {} stage {} ({} list)", a_actor->GetDisplayFullName(), resolved.set->name, choice.index, choice.muffled ? "muffled" : "plain");
			if (g_climaxPlayed <= 3) logger::info("Moans: {}", g_last);
			Debug::Log(Debug::Cat::Moan, "{}", g_last);
			return true;
		}

		void Step(RE::Actor* a_actor, const Scenes::Slot& a_slot, float a_now, const Cfg& a_cfg)
		{
			auto& st = g_state[a_slot.id];
			if (st.nextAt <= 0.0f) {
				// A fresh actor: OStim starts its moan timer when they join the scene.
				st.nextAt = a_now + Rand(a_cfg.intervalMin, a_cfg.intervalMax);
			}
			VoiceModel::Request(a_actor);

			if (st.climaxPending) {
				st.climaxPending = false;
				if (a_cfg.climax && PlayClimax(a_actor, a_slot, st, a_now, a_cfg)) return;
			}

			if (st.playing) {
				// Ours is still sounding, or ended a moment ago; a sound the engine lost is given up on after 15 s.
				if (LipSync::IsSoundPlaying(st.soundID) && a_now - st.playedAt < 15.0f) return;
				st.playing = false;
				st.nextAt = a_now + st.interval;
			}
			if (a_now < st.nextAt) return;

			auto defer = [&](float a_seconds, const char* a_why) {
				st.nextAt = a_now + a_seconds;
				if (Debug::On() && st.gate != a_why) {
					st.gate = a_why;
					Debug::Log(Debug::Cat::Moan, "{}: next moan waits: {}", Debug::Label(a_actor), a_why);
				}
			};

			if (a_actor->IsDead() || !a_actor->Get3D()) return defer(1.0f, "dead or no 3D");
			// Their own orgasm: the climax sound is OStim's, and a moan on top of it is noise.
			if (a_slot.climaxing || a_slot.broken) return defer(0.5f, a_slot.climaxing ? "their own orgasm" : "broken victim");
			// Someone else holds the mouth: a spoken line (a dialogue mod, DDF) or an expression override.
			if (a_now < a_slot.externalMouthUntil || a_slot.exprOverride) return defer(0.5f, a_slot.exprOverride ? "OStim's expression override holds the mouth" : "another mod holds the mouth");
			// OStim's own rule: no moan while the actor is talking (the flag is not reliable on the player).
			if (!a_actor->IsPlayerRef() && a_actor->GetActorRuntimeData().voiceTimer > 0.0f) return defer(0.5f, "they are speaking");
			// A climax or a reaction of OStim's is playing on them.
			if (LipSync::VoiceBusyUntil(a_actor) > a_now) return defer(0.25f, "an OStim climax or reaction sound is playing on them");

			const auto ctx = VoiceModel::Context(a_actor);
			if (!ctx.inScene) return;
			// No action of theirs lets them moan (a kiss, say): the scene is quiet for them, as it is in OStim.
			if (ctx.known && !ctx.moan) {
				{
					std::scoped_lock l(g_statusLock);
					++g_notAllowed;
				}
				return defer(1.0f, "no action of theirs lets them moan");
			}

			const auto resolved = VoiceModel::Resolve(a_actor);
			if (!resolved.set) {
				{
					std::scoped_lock l(g_statusLock);
					++g_noSet;
				}
				return defer(2.0f, "no voice set found for them");
			}
			const auto choice = VoiceModel::Select(resolved.set->moan, ctx.muffled, a_actor, ctx.partner);
			if (!choice.entry) {
				{
					std::scoped_lock l(g_statusLock);
					++g_noEntry;
				}
				return defer(1.0f, "no entry's condition holds (or the muffled list is empty)");
			}

			const auto played = LipSync::PlayDescriptor(a_actor, choice.entry->sound, a_cfg.volume);
			if (!played.ok) {
				std::scoped_lock l(g_statusLock);
				++g_failed;
				g_last = "failed: " + played.text;
				if (g_failed <= 5) logger::warn("Moans: {}", played.text);
				return defer(2.0f, "the sound could not be played");
			}

			const float interval = NextInterval(choice.entry->interval, a_cfg, a_slot.arch);
			st.playing = true;
			st.soundID = played.soundID;
			st.playedAt = a_now;
			st.interval = interval;

			std::scoped_lock l(g_statusLock);
			++g_played;
			g_last = std::format("{}: {} stage {} ({} list), then {:.2f} s", a_actor->GetDisplayFullName(), resolved.set->name, choice.index,
				choice.muffled ? "muffled" : "plain", interval);
			if (g_played <= 3) logger::info("Moans: first moans: {}", g_last);
			st.gate.clear();
			Debug::Log(Debug::Cat::Moan, "{}", g_last);
		}
	}

	void Tick()
	{
		const auto cfg = ReadCfg();
		if (!cfg.enabled) {
			if (!g_state.empty()) g_state.clear();
			return;
		}
		if (RE::UI::GetSingleton()->GameIsPaused()) return;
		const float now = Scenes::Now();

		std::unordered_set<RE::FormID> seen;
		{
			std::scoped_lock sl(Scenes::Lock());
			Scenes::ForEachActor([&](RE::Actor* a, const Scenes::Slot& s) {
				// Victims have their own voice (Voice.cpp); everyone else whose face OSIS paints gets the takeover.
				if (!s.painted || s.victim) return;
				seen.insert(s.id);
				Step(a, s, now, cfg);
			});
		}
		std::erase_if(g_state, [&](const auto& kv) { return !seen.contains(kv.first); });
		std::scoped_lock l(g_statusLock);
		g_actors = seen.size();
	}

	void OnOrgasm(RE::Actor* a_actor)
	{
		if (!a_actor) return;
		const auto it = g_state.find(a_actor->GetFormID());
		if (it != g_state.end()) it->second.climaxPending = true;  // played on the next tick, from Step
	}

	void Clear()
	{
		g_state.clear();
	}

	std::string Describe(RE::Actor* a)
	{
		if (!a) return "none";
		const auto it = g_state.find(a->GetFormID());
		if (it == g_state.end()) return "not tracked (the takeover is off, or they are not painted)";
		const auto& st = it->second;
		const float now = Scenes::Now();
		return std::format("{} | next moan in {:.1f} s (interval {:.2f} s) | climax pending={} | waiting for: {}", st.playing ? std::format("moan sounding (sound {}, {:.1f} s ago)", st.soundID, now - st.playedAt) : std::string("quiet"),
			std::max(0.0f, st.nextAt - now), st.interval, st.climaxPending, st.gate.empty() ? "nothing" : st.gate);
	}

	std::string Status()
	{
		const auto cfg = ReadCfg();
		std::scoped_lock l(g_statusLock);
		if (!cfg.enabled) return "off";
		return std::format(
			"on, {} actor(s); {} moans and {} climaxes played, {} failed, {} with no entry fitting, {} with no voice set, {} not allowed by the scene; "
			"climaxes: {} with nothing to play, {} left to OStim (the set has climax dialogue). Interval and volume: {}. Last: {}",
			g_actors, g_played, g_climaxPlayed, g_failed, g_noEntry, g_noSet, g_notAllowed, g_climaxNone, g_climaxLeft,
			cfg.fromOStim ? "OStim's" : "OSIS's own", g_last);
	}

	bool OStimSettings(std::string& a_text)
	{
		float lo = 0.0f, hi = 0.0f, vol = 1.0f;
		if (!ReadOStimSettings(lo, hi, vol)) {
			a_text = "OStim's moan settings could not be read (OStim.esp not found); OSIS's own are used.";
			return false;
		}
		a_text = std::format("OStim's settings right now: a moan every {:.1f} to {:.1f} s, volume {:.2f}.", lo, hi, vol);
		return true;
	}
}
