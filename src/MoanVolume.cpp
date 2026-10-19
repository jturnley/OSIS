// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "MoanVolume.h"

#include "Debug.h"
#include "Face/Engine.h"
#include "LipSync.h"
#include "Scenes.h"
#include "Settings.h"
#include "Virginity.h"
#if !OSIS_LITE
#	include "VoiceModel.h"
#endif

namespace MoanVolume
{
	namespace
	{
		namespace S = Settings::Audio;

		constexpr std::array<const char*, 3> kClassNames{ "quiet", "normal", "loud" };
		constexpr std::array<const char*, 5> kKindNames{ "as they come", "no moans", "quiet moans", "normal moans", "loud moans" };
		constexpr std::array<const char*, 4> kLevelNames{ "quiet", "normal", "loud", "high" };
		constexpr std::array<const char*, 3> kPhaseNames{ "stages 1-3", "stage 4", "the climax" };
		constexpr std::array<float, 4> kMouth{ 0.75f, 1.0f, 1.2f, 1.35f };  // the mouth's opening for each volume tier

		struct Cfg
		{
			bool on = false;
			float master = 1.0f;
			std::array<float, 4> tier{ 0.7f, 1.0f, 1.3f, 1.6f };  // the volume tiers: quiet, normal, loud, high
			bool mouth = true;
			bool level = false;
			float strength = 1.0f;
			bool select = true;
			bool lines = false;     // a moan gives way to a spoken line
			float duck = 1.0f;      // the others' moans while a line is spoken in their scene
			float lineVol = 1.0f;   // the spoken lines of a scene's actors
			std::array<std::array<S::MoanRule, 3>, 9> rules{};
		};

		Cfg ReadCfg()
		{
			std::scoped_lock l(Settings::lock);
			Cfg c;
			c.on = S::bPersonalityVolume && Settings::General::bEnabled;
			c.master = S::fMoanMaster;
			c.tier = { S::fVolumeQuiet, 1.0f, S::fVolumeLoud, S::fVolumeHigh };
			c.mouth = S::bMouthFollowsVolume;
			c.level = S::bLevelMoans;
			c.strength = S::fLevelStrength;
			c.select = S::bSelectMoans;
			c.lines = S::bQuietDuringLines && Settings::General::bEnabled;
			c.duck = S::fLineDuck;
			c.lineVol = Settings::General::bEnabled ? S::fSpokenLineVolume : 1.0f;
			c.rules = S::rules;
			return c;
		}

		std::size_t Index(int a_pers) { return static_cast<std::size_t>(std::clamp(a_pers, 0, 8)); }

		// 0 the first three stages, 1 the fourth stage, 2 the climax.
		int PhaseOf(int a_stage, bool a_climax) { return a_climax ? 2 : (a_stage >= 4 ? 1 : 0); }

		unsigned MaskOf(int a_kind)
		{
			switch (a_kind) {
			case S::kMoanQuiet: return 1u;
			case S::kMoanNormal: return 2u;
			case S::kMoanLoud: return 4u;
			default: return 0u;
			}
		}

		// What the hook does to an OStim sound of one actor in one phase.
		struct Mode
		{
			float volume = 1.0f;   // the master times the volume tier
			float targetDb = std::numeric_limits<float>::quiet_NaN();  // the loudness the sound is brought toward (NaN: left at its own)
			bool mute = false;     // no moan at all
			float strength = 1.0f;
			float mouth = 1.0f;
		};

		Mode ModeOf(const Cfg& a_cfg, const S::MoanRule& a_rule, const LipSync::LevelScale& a_scale)
		{
			Mode m;
			const int level = std::clamp(a_rule.level, 0, 3);
			m.volume = a_cfg.master * a_cfg.tier[static_cast<std::size_t>(level)];
			m.mute = a_rule.kind == S::kMoanNone;
			m.strength = a_cfg.strength;
			m.mouth = kMouth[static_cast<std::size_t>(level)];
			const unsigned mask = MaskOf(a_rule.kind);
			if (a_cfg.level && a_scale.ready && mask != 0u) m.targetDb = a_scale.median[mask == 1u ? 0 : (mask == 2u ? 1 : 2)];
			return m;
		}

		// What the hook and the sweep read, kept by Tick.
		struct Target
		{
			RE::NiAVObject* node;
			RE::FormID id;
			Mode now;       // the rule for the stage the scene is at
			Mode climax;    // the rule for the climax
			bool climaxing;
			int phase;
			int stage;
			bool speaking = false;  // a line is being spoken on this actor: no moan of theirs
			float duck = 1.0f;      // a line is being spoken by someone else in their scene: their moans are turned down by this
		};
		struct Applied
		{
			float volume = 0.0f;   // what we left the sound at
			float at = 0.0f;       // scene time, for pruning
			bool corrected = false;  // the one late correction has been made
			RE::FormID actor = 0;
			bool ducked = false;     // turned down for a line someone else is speaking
		};

		std::mutex g_lock;
		std::vector<Target> g_targets;                          // under g_lock: the 3D of each actor in a scene whose moans are shaped
		std::unordered_map<RE::FormID, Target> g_byActor;       // under g_lock: the same, by actor (node unused)
		std::unordered_map<RE::FormID, float> g_climaxUntil;    // under g_lock: scene time until which the climax rule applies, from the orgasm event
		std::unordered_map<std::uint32_t, Applied> g_applied;   // under g_lock: sound id -> what was done to it
		std::atomic_bool g_active{ false };
		std::atomic_bool g_hooked{ false };
		std::atomic<std::uint32_t> g_atStart{ 0 };
		std::atomic<std::uint32_t> g_late{ 0 };
		std::atomic<std::uint32_t> g_muted{ 0 };
		std::atomic<std::uint32_t> g_rejected{ 0 };
		constexpr float kLineTail = 0.35f;                      // seconds a moan stays away after a spoken line's last frame
		std::unordered_map<RE::FormID, float> g_lineUntil;      // main thread (Tick): scene time until which a line is counted as being spoken on this actor

		// The lines the scene's actors speak (Audio::fSpokenLineVolume). A spoken line is a sound of the Voice category (AudioCategoryVOC, or its parent AudioCategoryVOCGeneral) that
		// follows an actor of a scene; the start hook turns it down as it starts, and the sweep catches one the hook missed or whose volume was set again.
		std::atomic<float> g_lineVol{ 1.0f };                      // the setting as of the last Tick
		std::atomic<const RE::BSISoundCategory*> g_voiceCat[2]{};  // the two Voice categories, resolved on the main thread
		struct LineSound
		{
			float volume = 0.0f;   // what we left it at
			float at = 0.0f;       // scene time, for pruning
			RE::FormID actor = 0;
			bool fixed = false;    // the one late correction has been made
		};
		std::unordered_map<std::uint32_t, LineSound> g_lines;      // under g_lock: sound id -> what was done to a spoken line
		std::atomic<std::uint32_t> g_linesScaled{ 0 };
		std::unordered_set<std::uint32_t> g_unmatchedSeen;         // main thread (Tick): categories of other sounds on a scene's actors already named in the debug log

		bool IsVoiceCategory(const RE::BSISoundCategory* a_cat)
		{
			if (!a_cat) return false;
			for (const auto& v : g_voiceCat) {
				const auto* p = v.load(std::memory_order_relaxed);
				if (p && p == a_cat) return true;
			}
			return false;
		}

		void ResolveVoiceCategories()  // main thread
		{
			constexpr RE::FormID ids[2] = { 0x876BB, 0x876BD };
			for (int i = 0; i < 2; ++i) {
				if (g_voiceCat[i].load()) continue;
				if (auto* f = RE::TESForm::LookupByID<RE::BGSSoundCategory>(ids[i])) {
					g_voiceCat[i].store(static_cast<const RE::BSISoundCategory*>(f));
					Debug::Log(Debug::Cat::Moan, "spoken lines: the Voice sound category {:08X} is resolved", ids[i]);
				}
			}
		}

		// The form id of a sound category, for the debug log only. SEH: the pointer is a sound's own and is not known to be a form.
		std::uint32_t CategoryFormID(const RE::BSISoundCategory* a_cat) noexcept
		{
			__try {
				return reinterpret_cast<const RE::BGSSoundCategory*>(reinterpret_cast<const std::byte*>(a_cat) - 0x30)->GetFormID();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return 0;
			}
		}

		// Bringing a file to a loudness: the gain that moves its measured level toward the target, a fraction of the way in dB, kept sensible.
		float LevelGain(const LipSync::Level& a_level, float a_targetDb, float a_strength)
		{
			if (!a_level.known || std::isnan(a_targetDb) || a_strength <= 0.0f) return 1.0f;
			return std::clamp(std::pow(10.0f, a_strength * (a_targetDb - a_level.db) / 20.0f), 0.2f, 4.0f);
		}

		// The stage from the scene alone: the speed, in quarters with the fastest the fourth stage; the excitement when the scene has one speed.
		int StageFromScene(const Scenes::Thread& a_thread, const Scenes::Slot& a_slot)
		{
			if (a_thread.maxSpeed > 0) return 1 + std::clamp(static_cast<int>(3.0f * static_cast<float>(a_thread.speed) / static_cast<float>(a_thread.maxSpeed)), 0, 3);
			return 1 + std::clamp(a_slot.raw / 25, 0, 3);
		}

		// The stage the actor is at; the voice set's own where it has stages (full edition). The caller holds Scenes::Lock().
		int StageOfLocked([[maybe_unused]] RE::Actor* a_actor, const Scenes::Thread& a_thread, const Scenes::Slot& a_slot)
		{
#if !OSIS_LITE
			VoiceModel::Request(a_actor);
			const auto resolved = VoiceModel::Resolve(a_actor);
			if (resolved.set && resolved.set->moan.sound.size() >= 2) {
				const auto ctx = VoiceModel::Context(a_actor);
				const auto natural = VoiceModel::Select(resolved.set->moan, ctx.muffled, a_actor, ctx.partner);
				if (natural.entry) return StageOfEntry(natural.index, static_cast<int>(resolved.set->moan.sound.size()));
			}
#endif
			return StageFromScene(a_thread, a_slot);
		}

		// The nodes a sound follows, from the one it follows up to the root, and the sound's id. SEH: nothing with a destructor may live here, because the
		// audio manager's maps belong to the audio thread.
		int FollowChain(RE::BSAudioManager* am, RE::BSGameSound* snd, RE::NiAVObject** out, int max, std::uint32_t* a_id) noexcept
		{
			int n = 0;
			__try {
				std::uint32_t id = 0;
				bool found = false;
				for (auto& [soundID, s] : am->activeSounds) {
					if (s == snd) {
						id = soundID;
						found = true;
						break;
					}
				}
				if (!found) return 0;
				*a_id = id;
				auto it = am->movingSounds.find(id);
				if (it == am->movingSounds.end()) return 0;
				for (RE::NiAVObject* node = it->second.get(); node && n < max; node = node->parent) out[n++] = node;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return n;
			}
			return n;
		}

		struct Voiced
		{
			std::uint32_t soundID;
			RE::NiAVObject* node;
			float volume;
			RE::BSResource::ID res;
			const RE::BSISoundCategory* cat;
		};

		int CollectVoiced(RE::BSAudioManager* am, Voiced* out, int max) noexcept
		{
			int n = 0;
			__try {
				for (auto& [soundID, nodePtr] : am->movingSounds) {
					if (n >= max) break;
					RE::NiAVObject* node = nodePtr.get();
					if (!node) continue;
					auto it = am->activeSounds.find(soundID);
					if (it == am->activeSounds.end() || !it->second) continue;
					RE::BSGameSound* snd = it->second;
					if (snd->volume <= 0.001f) continue;  // muted (a victim's, a rule's, or a try of the takeover that was the wrong class): nothing to scale
					out[n].soundID = soundID;
					out[n].node = node;
					out[n].volume = snd->volume;
					out[n].cat = snd->category;
					std::memcpy(&out[n].res, &snd->resourceID, sizeof(RE::BSResource::ID));
					++n;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return n;
			}
			return n;
		}

		void SetSoundVolume(std::uint32_t a_id, float a_volume)
		{
			RE::BSSoundHandle h;
			h.soundID = a_id;
			h.assumeSuccess = false;
			h.state = RE::BSSoundHandle::AssumedState::kPlaying;
			h.SetVolume(a_volume);
		}

		// The volume an OStim sound of this actor should have, and why. Not a change: -1.
		struct Decision
		{
			float volume = -1.0f;
			float factor = 1.0f;
			bool mute = false;
			float gain = 1.0f;
			int phase = 0;
			bool line = false;   // muted for a line spoken on this actor
		};

		Decision Decide(const Target& a_target, bool a_climaxNow, const LipSync::Level& a_level, float a_before)
		{
			Decision d;
			const Mode& m = a_climaxNow ? a_target.climax : a_target.now;
			d.phase = a_climaxNow ? 2 : (a_target.stage >= 4 ? 1 : 0);
			if (a_target.speaking) {
				d.mute = true;
				d.line = true;
				d.volume = 0.0f;
				return d;
			}
			if (m.mute) {
				d.mute = true;
				d.volume = 0.0f;
				return d;
			}
			d.gain = LevelGain(a_level, m.targetDb, m.strength);
			d.factor = m.volume * d.gain * a_target.duck;
			if (std::abs(d.factor - 1.0f) < 0.01f) return d;
			d.volume = std::clamp(a_before * d.factor, 0.0f, 1.0f);
			return d;
		}

		struct PlayHook
		{
			static void thunk(RE::BSGameSound* a_sound)
			{
				float volume = -1.0f;
				if (a_sound && LipSync::IsMoanResource(a_sound->resourceID)) {
					if (LipSync::PlayingOwn()) volume = StartOwn(a_sound);
					else if (g_active.load(std::memory_order_relaxed)) volume = StartTheirs(a_sound);
				} else if (a_sound && g_active.load(std::memory_order_relaxed) && g_lineVol.load(std::memory_order_relaxed) < 0.999f) {
					volume = StartLine(a_sound);
				}
				func(a_sound);
				if (volume >= 0.0f) {
					a_sound->SetVolume(volume);
					if (volume > 0.0f) g_atStart.fetch_add(1, std::memory_order_relaxed);
				}
			}

			// A sound the Director takeover is playing: it asked for files of some loudness classes. One of another class is muted, and PlayDescriptor, which is
			// inside Play() on this thread, stops it and tries again.
			static float StartOwn(RE::BSGameSound* a_sound)
			{
				const unsigned mask = LipSync::WantedClasses();
				if (mask == 0u) return -1.0f;
				const auto level = LipSync::LevelOf(a_sound->resourceID);
				if (!level.known || (mask & (1u << level.cls)) != 0u) return -1.0f;
				LipSync::NoteRejected();
				g_rejected.fetch_add(1, std::memory_order_relaxed);
				a_sound->volume = 1.0e-5f;
				Debug::Log(Debug::Cat::Moan, "takeover try muted: the file measures {:.1f} dBFS, a {} one; the rule asks for {}", level.db, kClassNames[static_cast<std::size_t>(level.cls)],
					std::format("{}{}{}", (mask & 1u) ? "quiet " : "", (mask & 2u) ? "normal " : "", (mask & 4u) ? "loud " : ""));
				return 0.0f;
			}

			// An OStim sound. Sets the volume before Play, and records the sound before Play so the per-tick correction never takes a sound that is starting for
			// one it missed.
			static float StartTheirs(RE::BSGameSound* a_sound)
			{
				auto* am = RE::BSAudioManager::GetSingleton();
				if (!am) return -1.0f;
				RE::NiAVObject* chain[24];
				std::uint32_t id = 0;
				const int n = FollowChain(am, a_sound, chain, 24, &id);
				if (n <= 0) return -1.0f;
				Target target{};
				bool climaxNow = false;
				bool found = false;
				{
					std::scoped_lock l(g_lock);
					for (int i = 0; i < n && !found; ++i) {
						for (const auto& t : g_targets) {
							if (t.node == chain[i]) {
								target = t;
								found = true;
								break;
							}
						}
					}
					if (found) {
						const auto it = g_climaxUntil.find(target.id);
						climaxNow = target.climaxing || (it != g_climaxUntil.end() && Scenes::Now() < it->second);
					}
				}
				if (!found) return -1.0f;
				const auto level = LipSync::LevelOf(a_sound->resourceID);
				const float before = a_sound->volume;
				const auto d = Decide(target, climaxNow, level, before);
				if (d.volume < 0.0f) return -1.0f;
				a_sound->volume = d.mute ? 1.0e-5f : d.volume;
				{
					std::scoped_lock l(g_lock);
					g_applied[id] = { d.volume, Scenes::Now(), false, target.id, target.duck < 1.0f };
				}
				if (d.mute) g_muted.fetch_add(1, std::memory_order_relaxed);
				const std::string file = Debug::On() ? LipSync::PathOf(a_sound->resourceID) : std::string();
				Debug::Log(Debug::Cat::Moan, "{:08X}: voice-set sound {} ({}, stage {}): {}{}{}", target.id, id, kPhaseNames[static_cast<std::size_t>(d.phase)], target.stage,
					d.mute ? std::string(d.line ? "muted: a line is being spoken on them" : "muted: the rule says no moans") : std::format("starts at {:.2f} x{:.2f} = {:.2f}", before, d.factor, d.volume),
					level.known ? std::format(" | the file measures {:.1f} dBFS, {}{}", level.db, kClassNames[static_cast<std::size_t>(level.cls)], d.gain != 1.0f ? std::format(", leveling gain x{:.2f}", d.gain) : std::string())
								: std::string(" | the file's loudness is not known"),
								file.empty() ? std::string() : " | file " + file);
				return d.volume;
			}

			// A line spoken by an actor of a scene (a Voice-category sound that follows them): started at the setting's share of its volume.
			static float StartLine(RE::BSGameSound* a_sound)
			{
				if (!IsVoiceCategory(a_sound->category)) return -1.0f;
				auto* am = RE::BSAudioManager::GetSingleton();
				if (!am) return -1.0f;
				RE::NiAVObject* chain[24];
				std::uint32_t id = 0;
				const int n = FollowChain(am, a_sound, chain, 24, &id);
				if (n <= 0) return -1.0f;
				RE::FormID actor = 0;
				{
					std::scoped_lock l(g_lock);
					for (int i = 0; i < n && !actor; ++i) {
						for (const auto& t : g_targets) {
							if (t.node == chain[i]) {
								actor = t.id;
								break;
							}
						}
					}
					if (!actor) return -1.0f;
				}
				const float before = a_sound->volume;
				const float volume = std::clamp(before * g_lineVol.load(std::memory_order_relaxed), 0.0f, 1.0f);
				a_sound->volume = volume;
				{
					std::scoped_lock l(g_lock);
					g_lines[id] = { volume, Scenes::Now(), actor, false };
				}
				g_linesScaled.fetch_add(1, std::memory_order_relaxed);
				Debug::Log(Debug::Cat::Moan, "{:08X}: spoken line, sound {}: starts at {:.2f} x{:.2f} = {:.2f}", actor, id, before, g_lineVol.load(), volume);
				return volume;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void InstallHook()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_BSXAudio2GameSound[0] };
		PlayHook::func = vtbl.write_vfunc(0x15, PlayHook::thunk);
		g_hooked = true;
		logger::info("Moan volume: sound start hook installed");
	}

	void NoteOrgasm(RE::FormID a_actor)
	{
		std::scoped_lock l(g_lock);
		g_climaxUntil[a_actor] = Scenes::Now() + 8.0f;
	}

	Plan PlanFor(RE::Actor* a_actor, int a_stage, bool a_climax)
	{
		Plan plan;
		if (!a_actor) return plan;
		const auto c = ReadCfg();
		if (!c.on) return plan;
		plan.phase = PhaseOf(a_stage, a_climax);
		if (Virginity::IsBroken(a_actor)) {  // makes no sound
			plan.silent = true;
			return plan;
		}
		const auto& rule = c.rules[Index(Face::Engine::Archetype(a_actor))][static_cast<std::size_t>(plan.phase)];
		plan.silent = rule.kind == S::kMoanNone;
		plan.classMask = MaskOf(rule.kind);
		plan.volume = c.master * c.tier[static_cast<std::size_t>(std::clamp(rule.level, 0, 3))];
		return plan;
	}

	int StageOfEntry(int a_index, int a_count)
	{
		if (a_count <= 1) return 1;
		return 1 + std::clamp((3 * std::clamp(a_index, 0, a_count - 1)) / (a_count - 1), 0, 3);
	}

	int StageOf(RE::Actor* a_actor)
	{
		if (!a_actor) return 1;
		std::scoped_lock sl(Scenes::Lock());
		auto* t = Scenes::ThreadOf(a_actor);
		auto* s = t ? t->Find(a_actor) : nullptr;
		return s ? StageOfLocked(a_actor, *t, *s) : 1;
	}

	float MouthGain(RE::Actor* a_actor)
	{
		if (!a_actor) return 1.0f;
		const auto c = ReadCfg();
		if (!c.on || !c.mouth) return 1.0f;
		std::scoped_lock l(g_lock);
		const auto it = g_byActor.find(a_actor->GetFormID());
		if (it == g_byActor.end()) return 1.0f;
		const auto ua = g_climaxUntil.find(a_actor->GetFormID());
		const bool climax = it->second.climaxing || (ua != g_climaxUntil.end() && Scenes::Now() < ua->second);
		return climax ? it->second.climax.mouth : it->second.now.mouth;
	}

	void Tick()
	{
		const auto c = ReadCfg();
		const auto scale = LipSync::LevelScaleInfo();
		std::vector<Target> targets;
		std::unordered_map<RE::FormID, Target> byActor;
		g_lineVol.store(c.lineVol, std::memory_order_relaxed);
		if (c.lineVol < 0.999f) ResolveVoiceCategories();
		if (c.on || c.lines || c.lineVol < 0.999f) {
			std::scoped_lock sl(Scenes::Lock());
			const float sceneNow = Scenes::Now();
			// Who is speaking a line, with a short tail so a moan does not start on the line's last breath, and in which scenes.
			std::unordered_set<int> talkingThreads;
			if (c.lines) {
				Scenes::ForEachActor([&](RE::Actor* a, const Scenes::Slot& s) {
					if (!s.painted) return;
					if (LipSync::SpokenLine(a)) {
						auto& until = g_lineUntil[s.id];
						if (until < sceneNow) Debug::Log(Debug::Cat::Moan, "{}: a spoken line starts: their moans are cut and the others' turned down while it lasts", Debug::Label(a));
						until = sceneNow + kLineTail;
					}
				});
				std::erase_if(g_lineUntil, [sceneNow](const auto& kv) { return kv.second < sceneNow; });
				Scenes::ForEachActor([&](RE::Actor* a, const Scenes::Slot& s) {
					const auto it = g_lineUntil.find(s.id);
					if (it != g_lineUntil.end()) {
						if (auto* t = Scenes::ThreadOf(a)) talkingThreads.insert(t->id);
					}
				});
			} else {
				g_lineUntil.clear();
			}
			Scenes::ForEachActor([&](RE::Actor* a, const Scenes::Slot& s) {
				if (!s.painted) return;
				auto* t = Scenes::ThreadOf(a);
				if (!t) return;
				const int pers = Face::Engine::Archetype(a);
				const int stage = StageOfLocked(a, *t, s);
				const int phase = PhaseOf(stage, false);
				// A broken woman makes no sound at all, at any stage and at the climax.
				const S::MoanRule silent{ S::kMoanNone, S::kLevelNormal };
				const bool broken = c.on && Virginity::IsBroken(a);
				const Mode plain{};  // the personality rules are off: only the spoken-line rule shapes anything
				Target target{ nullptr, s.id, c.on ? ModeOf(c, broken ? silent : c.rules[Index(pers)][static_cast<std::size_t>(phase)], scale) : plain,
					c.on ? ModeOf(c, broken ? silent : c.rules[Index(pers)][2], scale) : plain, s.climaxing, phase, stage };
				if (c.lines) {
					target.speaking = g_lineUntil.contains(s.id);
					target.duck = (!target.speaking && talkingThreads.contains(t->id)) ? c.duck : 1.0f;
				}
				byActor[s.id] = target;
				for (auto* n : { a->Get3D(), a->Get3D1(false), a->Get3D1(true) }) {
					if (n) {
						target.node = n;
						targets.push_back(target);
					}
				}
			});
		}
		const bool active = !byActor.empty();
		{
			std::scoped_lock l(g_lock);
			g_targets = std::move(targets);
			g_byActor = std::move(byActor);
			const float now = Scenes::Now();
			std::erase_if(g_climaxUntil, [now](const auto& kv) { return kv.second < now; });
		}
		g_active.store(active, std::memory_order_relaxed);
		if (!active) {
			std::scoped_lock l(g_lock);
			g_applied.clear();
			g_lines.clear();
			return;
		}

		// A voice-set sound the hook did not shape, or one whose volume was set again after Play, is corrected once.
		auto* am = RE::BSAudioManager::GetSingleton();
		if (!am || RE::UI::GetSingleton()->GameIsPaused()) return;
		std::array<Voiced, 64> voiced{};
		const int n = CollectVoiced(am, voiced.data(), static_cast<int>(voiced.size()));
		const float now = Scenes::Now();
		struct Fix
		{
			std::uint32_t id;
			float volume;
			RE::FormID actor;
			float factor;
			int kind = 0;  // 0 corrected, 1 cut for a line spoken on them, 2 turned down for a line spoken by someone else, 3 a spoken line turned down
		};
		std::vector<Fix> fixes;
		const float lineVol = c.lineVol;
		for (int i = 0; i < n; ++i) {
			const auto& v = voiced[static_cast<std::size_t>(i)];
			if (LipSync::IsOwnSound(v.soundID)) continue;
			if (!LipSync::IsMoanResource(v.res)) {
				// Not a voice-set sound: a spoken line, if it is of the Voice category and follows an actor of a scene.
				if (lineVol >= 0.999f) continue;
				auto* speaker = LipSync::ActorOf(v.node);
				if (!speaker) continue;
				std::scoped_lock l(g_lock);
				if (!g_byActor.contains(speaker->GetFormID())) continue;
				if (!IsVoiceCategory(v.cat)) {
					const std::uint32_t cat = v.cat ? CategoryFormID(v.cat) : 0u;
					if (Debug::On() && g_unmatchedSeen.size() < 24 && g_unmatchedSeen.insert(cat).second)
						Debug::Log(Debug::Cat::Moan, "spoken lines: a sound of sound category {:08X} follows {} (not the Voice category, so left alone)", cat, Debug::Label(speaker));
					continue;
				}
				const auto lit = g_lines.find(v.soundID);
				if (lit == g_lines.end()) {
					const float target = std::clamp(v.volume * lineVol, 0.0f, 1.0f);
					g_lines[v.soundID] = { target, now, speaker->GetFormID(), false };
					fixes.push_back({ v.soundID, target, speaker->GetFormID(), lineVol, 3 });
				} else if (!lit->second.fixed && std::abs(v.volume - lit->second.volume) > 0.02f) {  // set again after we did it: once more
					const float target = std::clamp(v.volume * lineVol, 0.0f, 1.0f);
					lit->second.volume = target;
					lit->second.fixed = true;
					fixes.push_back({ v.soundID, target, speaker->GetFormID(), lineVol, 3 });
				}
				continue;
			}
			auto* owner = LipSync::ActorOf(v.node);
			if (!owner) continue;
			const auto level = LipSync::LevelOf(v.res);
			std::scoped_lock l(g_lock);
			const auto it = g_byActor.find(owner->GetFormID());
			if (it == g_byActor.end()) continue;
			const auto ua = g_climaxUntil.find(owner->GetFormID());
			const bool climaxNow = it->second.climaxing || (ua != g_climaxUntil.end() && now < ua->second);
			const auto d = Decide(it->second, climaxNow, level, v.volume);
			auto ap = g_applied.find(v.soundID);
			if (d.line) {  // a line is being spoken on this actor and one of their moans is still sounding: cut it
				if (ap == g_applied.end()) g_applied[v.soundID] = { 0.0f, now, true, owner->GetFormID() };
				else ap->second.volume = 0.0f;
				fixes.push_back({ v.soundID, 0.0f, owner->GetFormID(), 0.0f, 1 });
				continue;
			}
			if (it->second.duck < 1.0f && ap != g_applied.end() && !ap->second.ducked) {  // a line began while this moan was sounding: turn it down, once
				const float target = std::clamp(v.volume * it->second.duck, 0.0f, 1.0f);
				ap->second.volume = target;
				ap->second.ducked = true;
				fixes.push_back({ v.soundID, target, owner->GetFormID(), it->second.duck, 2 });
				continue;
			}
			if (ap == g_applied.end()) {
				const float target = d.volume < 0.0f ? v.volume : d.volume;  // Decide's volume already has the duck in it
				g_applied[v.soundID] = { target, now, true, owner->GetFormID(), it->second.duck < 1.0f };
				if (d.volume >= 0.0f && std::abs(target - v.volume) > 0.005f) fixes.push_back({ v.soundID, target, owner->GetFormID(), d.factor });
			} else if (!ap->second.corrected && std::abs(v.volume - ap->second.volume) > 0.02f) {
				const float target = d.volume < 0.0f ? v.volume : d.volume;  // someone set a volume after our hook did: shape that one
				ap->second.volume = target;
				ap->second.corrected = true;
				ap->second.ducked = it->second.duck < 1.0f;
				if (d.volume >= 0.0f) fixes.push_back({ v.soundID, target, owner->GetFormID(), d.factor });
			}
		}
		for (const auto& f : fixes) {
			SetSoundVolume(f.id, f.volume);
			g_late.fetch_add(1, std::memory_order_relaxed);
			if (f.kind == 3) {
				g_linesScaled.fetch_add(1, std::memory_order_relaxed);
				Debug::Log(Debug::Cat::Moan, "{}: spoken line, sound {}: turned to {:.2f} (x{:.2f}; the start hook did not shape it)", Debug::Label(f.actor), f.id, f.volume, f.factor);
			} else if (f.kind == 1) Debug::Log(Debug::Cat::Moan, "{}: voice-set sound {} cut: a line is being spoken on them", Debug::Label(f.actor), f.id);
			else if (f.kind == 2) Debug::Log(Debug::Cat::Moan, "{}: voice-set sound {} turned down to {:.2f} (x{:.2f}): a line is being spoken in their scene", Debug::Label(f.actor), f.id, f.volume, f.factor);
			else Debug::Log(Debug::Cat::Moan, "{}: voice-set sound {} corrected to {:.2f} (x{:.2f}; it was not shaped when it started)", Debug::Label(f.actor), f.id, f.volume, f.factor);
		}
		std::scoped_lock l(g_lock);
		std::erase_if(g_applied, [now](const auto& kv) { return now - kv.second.at > 20.0f; });
		std::erase_if(g_lines, [now](const auto& kv) { return now - kv.second.at > 30.0f; });
	}

	std::string Describe(RE::Actor* a_actor)
	{
		if (!a_actor) return "none";
		const auto c = ReadCfg();
		if (!c.on) return "personalities do not shape moans (off)";
		const int pers = Face::Engine::Archetype(a_actor);
		const auto scale = LipSync::LevelScaleInfo();
		Target listed{};
		bool in = false;
		{
			std::scoped_lock l(g_lock);
			if (const auto it = g_byActor.find(a_actor->GetFormID()); it != g_byActor.end()) {
				listed = it->second;
				in = true;
			}
		}
		std::string rules;
		for (std::size_t ph = 0; ph < 3; ++ph) {
			const auto& r = c.rules[Index(pers)][ph];
			rules += std::format("{}{}: {}, {}", rules.empty() ? "" : " | ", kPhaseNames[ph], kKindNames[static_cast<std::size_t>(std::clamp(r.kind, 0, 4))], kLevelNames[static_cast<std::size_t>(std::clamp(r.level, 0, 3))]);
		}
		return std::format("{} rules [{}] | master x{:.2f}, leveling {}{} | now {}: {}", Face::Engine::PersonalityName(pers), rules, c.master, c.level ? "on" : "off",
			c.level && !scale.ready ? " (loudness not measured yet)" : "", in ? std::format("stage {}, {}", listed.stage, listed.climaxing ? "climaxing" : kPhaseNames[static_cast<std::size_t>(listed.phase)]) : std::string("not in the hook's table"),
			in ? std::format("mouth gain x{:.2f}", listed.now.mouth) : std::string("-"));
	}

	std::string Status()
	{
		const auto c = ReadCfg();
		if (!c.on) return "off";
		const auto s = LipSync::LevelScaleInfo();
		return std::format("{}; {} voice-set sound(s) shaped as they started ({} muted by a rule), {} corrected afterwards, {} wrong-class takeover tries; {}", g_hooked ? "sound start hook installed" : "no start hook (the per-tick correction does it)",
			g_atStart.load() + g_muted.load(), g_muted.load(), g_late.load(), g_rejected.load(),
			s.ready ? std::format("loudness of {} files measured: quiet below {:.1f} dBFS, loud from {:.1f}", s.count[0] + s.count[1] + s.count[2], s.quietBelow, s.loudFrom)
					: std::string("loudness not measured yet"));
	}
}
