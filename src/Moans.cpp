// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Moans.h"

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
		};

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
			return c;
		}

		float Rand(float a_lo, float a_hi)
		{
			thread_local std::mt19937 rng{ std::random_device{}() };
			return std::uniform_real_distribution<float>(a_lo, a_hi)(rng);
		}

		// How often a personality moans, as a multiple of the interval: a vocal actor never stops, a stoic one hardly ever starts.
		float PersonalityScale(int a_arch)
		{
			namespace P = Face::Engine::Pers;
			switch (a_arch) {
			case P::kVocal:
			case P::kWild: return 0.6f;
			case P::kStoic: return 2.0f;
			case P::kShy:
			case P::kTimid: return 1.4f;
			default: return 1.0f;
			}
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
			float interval = choice.entry->interval > 0.0f ? choice.entry->interval : Rand(a_cfg.intervalMin, a_cfg.intervalMax);
			if (a_cfg.personality) interval *= PersonalityScale(a_slot.arch);
			a_st.playing = true;
			a_st.soundID = played.soundID;
			a_st.playedAt = a_now;
			a_st.interval = interval;

			std::scoped_lock l(g_statusLock);
			++g_climaxPlayed;
			g_last = std::format("{}: CLIMAX, {} stage {} ({} list)", a_actor->GetDisplayFullName(), resolved.set->name, choice.index, choice.muffled ? "muffled" : "plain");
			if (g_climaxPlayed <= 3) logger::info("Moans: {}", g_last);
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

			auto defer = [&](float a_seconds) { st.nextAt = a_now + a_seconds; };

			if (a_actor->IsDead() || !a_actor->Get3D()) return defer(1.0f);
			// Their own orgasm: the climax sound is OStim's, and a moan on top of it is noise.
			if (a_slot.climaxing || a_slot.broken) return defer(0.5f);
			// Someone else holds the mouth: a spoken line (a dialogue mod, DDF) or an expression override.
			if (a_now < a_slot.externalMouthUntil || a_slot.exprOverride) return defer(0.5f);
			// OStim's own rule: no moan while the actor is talking (the flag is not reliable on the player).
			if (!a_actor->IsPlayerRef() && a_actor->GetActorRuntimeData().voiceTimer > 0.0f) return defer(0.5f);
			// A climax or a reaction of OStim's is playing on them.
			if (LipSync::VoiceBusyUntil(a_actor) > a_now) return defer(0.25f);

			const auto ctx = VoiceModel::Context(a_actor);
			if (!ctx.inScene) return;
			// No action of theirs lets them moan (a kiss, say): the scene is quiet for them, as it is in OStim.
			if (ctx.known && !ctx.moan) {
				{
					std::scoped_lock l(g_statusLock);
					++g_notAllowed;
				}
				return defer(1.0f);
			}

			const auto resolved = VoiceModel::Resolve(a_actor);
			if (!resolved.set) {
				{
					std::scoped_lock l(g_statusLock);
					++g_noSet;
				}
				return defer(2.0f);
			}
			const auto choice = VoiceModel::Select(resolved.set->moan, ctx.muffled, a_actor, ctx.partner);
			if (!choice.entry) {
				{
					std::scoped_lock l(g_statusLock);
					++g_noEntry;
				}
				return defer(1.0f);
			}

			const auto played = LipSync::PlayDescriptor(a_actor, choice.entry->sound, a_cfg.volume);
			if (!played.ok) {
				std::scoped_lock l(g_statusLock);
				++g_failed;
				g_last = "failed: " + played.text;
				if (g_failed <= 5) logger::warn("Moans: {}", played.text);
				return defer(2.0f);
			}

			float interval = choice.entry->interval > 0.0f ? choice.entry->interval : Rand(a_cfg.intervalMin, a_cfg.intervalMax);
			if (a_cfg.personality) interval *= PersonalityScale(a_slot.arch);
			st.playing = true;
			st.soundID = played.soundID;
			st.playedAt = a_now;
			st.interval = interval;

			std::scoped_lock l(g_statusLock);
			++g_played;
			g_last = std::format("{}: {} stage {} ({} list), then {:.2f} s", a_actor->GetDisplayFullName(), resolved.set->name, choice.index,
				choice.muffled ? "muffled" : "plain", interval);
			if (g_played <= 3) logger::info("Moans: first moans: {}", g_last);
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

	std::string Status()
	{
		const auto cfg = ReadCfg();
		std::scoped_lock l(g_statusLock);
		if (!cfg.enabled) return "off";
		return std::format(
			"on, {} actor(s); {} moans and {} climaxes played, {} failed, {} with no entry fitting, {} with no voice set, {} not allowed by the scene; "
			"climaxes: {} with nothing to play, {} left to OStim (the set has climax dialogue). Last: {}",
			g_actors, g_played, g_climaxPlayed, g_failed, g_noEntry, g_noSet, g_notAllowed, g_climaxNone, g_climaxLeft, g_last);
	}
}
