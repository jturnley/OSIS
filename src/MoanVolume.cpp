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

namespace MoanVolume
{
	namespace
	{
		namespace S = Settings::Audio;

		struct Cfg
		{
			bool on = false;
			float master = 1.0f;
			std::array<float, 9> pers{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };  // balanced, stoic, vocal, shy, dominant, timid, submissive, wild, crazed
			bool mouth = true;
		};

		Cfg ReadCfg()
		{
			std::scoped_lock l(Settings::lock);
			Cfg c;
			c.on = S::bPersonalityVolume && Settings::General::bEnabled;
			c.master = S::fMoanMaster;
			c.pers = { 1.0f, S::fStoicVolume, S::fVocalVolume, S::fShyVolume, S::fDominantVolume, S::fTimidVolume, S::fSubmissiveVolume, S::fWildVolume, S::fCrazedVolume };
			c.mouth = S::bMouthFollowsVolume;
			return c;
		}

		float PersScale(const Cfg& a_cfg, int a_pers) { return a_cfg.pers[static_cast<std::size_t>(std::clamp(a_pers, 0, 8))]; }

		// What the hook and the sweep read, kept by Tick.
		struct Target
		{
			RE::NiAVObject* node;
			float scale;
			RE::FormID id;
		};
		struct Applied
		{
			float volume = 0.0f;   // what we left the sound at
			float at = 0.0f;       // scene time, for pruning
			bool corrected = false;  // the one late correction has been made
			RE::FormID actor = 0;
		};

		std::mutex g_lock;
		std::vector<Target> g_targets;                  // under g_lock: the 3D of each actor in a scene whose voice is not at the standard volume
		std::unordered_map<RE::FormID, float> g_byActor;  // under g_lock: the same, by actor
		std::unordered_map<std::uint32_t, Applied> g_applied;  // under g_lock: sound id -> what was done to it
		std::atomic_bool g_active{ false };
		std::atomic_bool g_hooked{ false };
		std::atomic<std::uint32_t> g_atStart{ 0 };
		std::atomic<std::uint32_t> g_late{ 0 };

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
					if (snd->volume <= 0.001f) continue;  // muted (a victim's, or the takeover's): nothing to scale
					out[n].soundID = soundID;
					out[n].node = node;
					out[n].volume = snd->volume;
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

		struct PlayHook
		{
			static void thunk(RE::BSGameSound* a_sound)
			{
				float scaled = -1.0f;
				std::uint32_t id = 0;
				RE::FormID actor = 0;
				if (a_sound && g_active.load(std::memory_order_relaxed) && !LipSync::PlayingOwn() && LipSync::IsMoanResource(a_sound->resourceID)) {
					if (auto* am = RE::BSAudioManager::GetSingleton()) {
						RE::NiAVObject* chain[24];
						const int n = FollowChain(am, a_sound, chain, 24, &id);
						if (n > 0) {
							float scale = 1.0f;
							{
								std::scoped_lock l(g_lock);
								for (int i = 0; i < n && scale == 1.0f; ++i) {
									for (const auto& t : g_targets) {
										if (t.node == chain[i]) {
											scale = t.scale;
											actor = t.id;
											break;
										}
									}
								}
							}
							if (scale != 1.0f) {
								const float before = a_sound->volume;
								scaled = std::clamp(before * scale, 0.0f, 1.0f);
								a_sound->volume = scaled;
								{
									// Recorded before Play, so the per-tick correction never takes a sound that is starting for one it missed.
									std::scoped_lock l(g_lock);
									g_applied[id] = { scaled, Scenes::Now(), false, actor };
								}
								Debug::Log(Debug::Cat::Moan, "{:08X}: voice-set sound {} starts at volume {:.2f} x{:.2f} = {:.2f}", actor, id, before, scale, scaled);
							}
						}
					}
				}
				func(a_sound);
				if (scaled >= 0.0f) {
					a_sound->SetVolume(scaled);
					g_atStart.fetch_add(1, std::memory_order_relaxed);
				}
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

	float Scale(RE::Actor* a_actor)
	{
		if (!a_actor) return 1.0f;
		const auto c = ReadCfg();
		if (!c.on) return 1.0f;
		return c.master * PersScale(c, Face::Engine::Archetype(a_actor));
	}

	float Apply(RE::Actor* a_actor, float a_base)
	{
		return std::clamp(a_base * Scale(a_actor), 0.0f, 1.0f);
	}

	float MouthGain(RE::Actor* a_actor)
	{
		if (!a_actor) return 1.0f;
		const auto c = ReadCfg();
		if (!c.on || !c.mouth) return 1.0f;
		return std::clamp(PersScale(c, Face::Engine::Archetype(a_actor)), 0.5f, 1.4f);
	}

	void Tick()
	{
		const auto c = ReadCfg();
		std::vector<Target> targets;
		std::unordered_map<RE::FormID, float> byActor;
		if (c.on) {
			std::scoped_lock sl(Scenes::Lock());
			Scenes::ForEachActor([&](RE::Actor* a, const Scenes::Slot& s) {
				if (!s.painted) return;
				const float scale = c.master * PersScale(c, Face::Engine::Archetype(a));
				if (std::abs(scale - 1.0f) < 0.005f) return;
				byActor[s.id] = scale;
				for (auto* n : { a->Get3D(), a->Get3D1(false), a->Get3D1(true) }) {
					if (n) targets.push_back({ n, scale, s.id });
				}
			});
		}
		const bool active = !byActor.empty();
		{
			std::scoped_lock l(g_lock);
			g_targets = std::move(targets);
			g_byActor = std::move(byActor);
		}
		g_active.store(active, std::memory_order_relaxed);
		if (!active) {
			std::scoped_lock l(g_lock);
			g_applied.clear();
			return;
		}

		// A voice-set sound the hook did not scale, or one whose volume was set again after Play, is corrected once.
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
		};
		std::vector<Fix> fixes;
		for (int i = 0; i < n; ++i) {
			const auto& v = voiced[static_cast<std::size_t>(i)];
			if (!LipSync::IsMoanResource(v.res) || LipSync::IsOwnSound(v.soundID)) continue;
			auto* owner = LipSync::ActorOf(v.node);
			if (!owner) continue;
			float scale = 1.0f;
			{
				std::scoped_lock l(g_lock);
				const auto it = g_byActor.find(owner->GetFormID());
				if (it == g_byActor.end()) continue;
				scale = it->second;
				auto ap = g_applied.find(v.soundID);
				if (ap == g_applied.end()) {
					const float target = std::clamp(v.volume * scale, 0.0f, 1.0f);
					g_applied[v.soundID] = { target, now, true, owner->GetFormID() };
					if (std::abs(target - v.volume) > 0.005f) fixes.push_back({ v.soundID, target, owner->GetFormID() });
				} else if (!ap->second.corrected && std::abs(v.volume - ap->second.volume) > 0.02f) {
					const float target = std::clamp(v.volume * scale, 0.0f, 1.0f);  // someone set a volume after our hook did: scale that one
					ap->second.volume = target;
					ap->second.corrected = true;
					fixes.push_back({ v.soundID, target, owner->GetFormID() });
				}
			}
		}
		for (const auto& f : fixes) {
			SetSoundVolume(f.id, f.volume);
			g_late.fetch_add(1, std::memory_order_relaxed);
			Debug::Log(Debug::Cat::Moan, "{}: voice-set sound {} corrected to volume {:.2f} (it was not scaled when it started)", Debug::Label(f.actor), f.id, f.volume);
		}
		std::scoped_lock l(g_lock);
		std::erase_if(g_applied, [now](const auto& kv) { return now - kv.second.at > 20.0f; });
	}

	std::string Describe(RE::Actor* a_actor)
	{
		if (!a_actor) return "none";
		const auto c = ReadCfg();
		const int pers = Face::Engine::Archetype(a_actor);
		float table = 1.0f;
		bool listed = false;
		{
			std::scoped_lock l(g_lock);
			if (const auto it = g_byActor.find(a_actor->GetFormID()); it != g_byActor.end()) {
				table = it->second;
				listed = true;
			}
		}
		return std::format("{} | personality {} x{:.2f} * master x{:.2f} = x{:.2f}; mouth gain x{:.2f} | in the hook's table: {}{}", c.on ? "personality volume on" : "personality volume off",
			Face::Engine::PersonalityName(pers), PersScale(c, pers), c.master, c.on ? c.master * PersScale(c, pers) : 1.0f, MouthGain(a_actor), listed ? "yes" : "no (standard volume)",
			listed ? std::format(" x{:.2f}", table) : std::string());
	}

	std::string Status()
	{
		const auto c = ReadCfg();
		if (!c.on) return "off";
		return std::format("{}; {} voice-set sound(s) scaled as they started, {} corrected afterwards", g_hooked ? "sound start hook installed" : "no start hook (the per-tick correction does it)",
			g_atStart.load(), g_late.load());
	}
}
