// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Scenes.h"


#include "Face/Engine.h"
#include "Face/Output.h"
#include "Papyrus.h"
#include "Pulse.h"
#include "Settings.h"
#if !OSIS_LITE
#	include "SpellCast.h"
#endif

namespace Scenes
{
	namespace
	{
		std::recursive_mutex g_lock;
		std::map<int, Thread> g_threads;
		std::uint32_t g_generation = 0;  // bumps on load so late VM callbacks are dropped

		void Task(std::function<void()> a_fn)
		{
			SKSE::GetTaskInterface()->AddTask(std::move(a_fn));
		}

		bool EnabledNow()
		{
			std::scoped_lock l(Settings::lock);
			return Settings::General::bEnabled;
		}

		// ---- slot bookkeeping
		void ClearSlot(Thread& t, Slot& s)
		{
			auto* a = s.Get();
			if (!a) return;
			Pulse::ClearActor(a, t.id);
			Papyrus::ClearLookAt(a);
			Face::Output::Release(a, 0.6f);
			Face::Engine::RestoreOStimFace(s, a);
			if (s.tongueOn) Papyrus::UnequipObject(a, "tongue");
			if (auto m = s.marker.get()) {
				m->Disable();
				m->SetDelete(true);
			}
		}

		void ApplyActors(Thread& t, const std::vector<RE::Actor*>& actors)
		{
			bool includeNPCs;
			{
				std::scoped_lock l(Settings::lock);
				includeNPCs = Settings::General::bIncludeNPCs;
			}
			std::vector<Slot> next;
			for (int pos = 0; pos < static_cast<int>(actors.size()); ++pos) {
				RE::Actor* a = actors[pos];
				if (!a) continue;
				Slot s;
				if (auto* old = t.Find(a)) s = std::move(*old);
				s.handle = a->GetHandle();
				s.id = a->GetFormID();
				s.name = a->GetDisplayFullName();
				s.pos = pos;
				s.player = a->IsPlayerRef();
				auto* base = a->GetActorBase();
				s.female = base && base->GetSex() == RE::SEX::kFemale;
				s.painted = Face::Engine::IsHuman(a) && (s.player || includeNPCs) && !a->IsChild();
				next.push_back(std::move(s));
			}
			// actors that left the thread go back to neutral
			for (auto& old : t.slots) {
				const bool kept = std::ranges::any_of(next, [&](const Slot& n) { return n.id == old.id; });
				if (!kept) ClearSlot(t, old);
			}
			t.slots = std::move(next);
			t.hasPlayer = std::ranges::any_of(t.slots, [](const Slot& s) { return s.player; });
			t.actorsKnown = true;
		}

		bool WithinRange(const Thread& t)
		{
			if (t.hasPlayer) return true;
			bool allowed;
			float radius;
			{
				std::scoped_lock l(Settings::lock);
				allowed = Settings::General::bNPCOnlyScenes;
				radius = Settings::General::fNPCSceneRadius;
			}
			if (!allowed) return false;
			auto* player = RE::PlayerCharacter::GetSingleton();
			for (const auto& s : t.slots) {
				auto* a = s.Get();
				if (a && player && a->Is3DLoaded() && a->GetPosition().GetDistance(player->GetPosition()) <= radius) return true;
			}
			return false;
		}

		void EndThread(int tid)
		{
			std::scoped_lock l(g_lock);
			auto it = g_threads.find(tid);
			if (it == g_threads.end()) return;
			auto& t = it->second;
#if !OSIS_LITE
			SpellCast::ThreadEnded(t.spellVictims);
#endif
			if (t.active) Face::Engine::EndScene(t);
			else {
				for (auto& s : t.slots) ClearSlot(t, s);
			}
			g_threads.erase(it);
		}

		// Async refresh: OThread.GetActors / GetScene / GetSpeed. Results come back through
		// the VM and are applied as main-thread tasks.
		void Refresh(int tid)
		{
			const auto gen = g_generation;
			Papyrus::GetThreadActors(tid, [tid, gen](std::vector<RE::Actor*> actors) {
				std::vector<RE::ActorHandle> handles;
				for (auto* a : actors) handles.push_back(a ? a->GetHandle() : RE::ActorHandle{});
				Task([tid, gen, handles = std::move(handles)]() {
					std::scoped_lock l(g_lock);
					if (gen != g_generation) return;
					auto it = g_threads.find(tid);
					if (it == g_threads.end()) return;
					auto& t = it->second;
					std::vector<RE::Actor*> list;
					for (auto& h : handles) list.push_back(h.get().get());
					if (std::ranges::none_of(list, [](RE::Actor* a) { return a != nullptr; })) {
						if (t.actorsKnown) EndThread(tid);  // thread is gone
						return;
					}
					const bool first = !t.actorsKnown;
					ApplyActors(t, list);
#if !OSIS_LITE
					if (first) t.spellVictims = SpellCast::StartedBySpell(list);
#endif
					if (!WithinRange(t)) {
						if (t.active) Face::Engine::EndScene(t);
						t.active = false;
						return;
					}
					if (first || !t.active) {
						t.active = true;
						Face::Engine::RefreshDerived(t, true);
						Face::Engine::OnThreadReady(t);
					}
				});
			});
			Papyrus::GetThreadScene(tid, [tid, gen](std::string scene) {
				Task([tid, gen, scene = std::move(scene)]() {
					std::scoped_lock l(g_lock);
					if (gen != g_generation || scene.empty()) return;
					auto it = g_threads.find(tid);
					if (it == g_threads.end() || it->second.sceneID == scene) return;
					auto& t = it->second;
					t.sceneID = scene;
					t.meta = OStimData::GetScene(scene);
					if (t.active) Face::Engine::OnSceneChanged(t);
				});
			});
			Papyrus::GetThreadSpeed(tid, [tid, gen](std::int32_t speed) {
				Task([tid, gen, speed]() {
					std::scoped_lock l(g_lock);
					if (gen != g_generation) return;
					if (auto it = g_threads.find(tid); it != g_threads.end()) it->second.speed = speed;
				});
			});
		}

		void PollOverrides(Thread& t)
		{
			const auto gen = g_generation;
			const int tid = t.id;
			for (auto& s : t.slots) {
				auto* a = s.Get();
				if (!a || !s.painted) continue;
				const RE::FormID id = s.id;
				// Any mod that puts a tongue out through OStim shows up here, not just ours. Always
				// polled: this drives handing the face to an ahegao mod, not only lip-sync, so it
				// must work with lip-sync off or its tongue handling set to Ignore.
				{
					Papyrus::IsObjectEquipped(a, "tongue", [tid, id, gen](bool out) {
						Task([tid, id, gen, out]() {
							std::scoped_lock l(g_lock);
							if (gen != g_generation) return;
							auto it = g_threads.find(tid);
							if (it == g_threads.end()) return;
							for (auto& s : it->second.slots) {
								if (s.id == id) s.tongueOut = out;
							}
						});
					});
				}
				Papyrus::HasExpressionOverride(a, [tid, id, gen](bool has) {
					Task([tid, id, gen, has]() {
						std::scoped_lock l(g_lock);
						if (gen != g_generation) return;
						auto it = g_threads.find(tid);
						if (it == g_threads.end()) return;
						for (auto& s : it->second.slots) {
							if (s.id == id) s.exprOverride = has;
						}
					});
				});
			}
			Papyrus::IsThreadRunning(tid, [tid, gen](bool running) {
				if (running) return;
				Task([tid, gen]() {
					if (gen == g_generation) EndThread(tid);
				});
			});
		}

		// ---- OStim events
		void OnThreadStart(int tid)
		{
			if (!EnabledNow() || !Face::Engine::OStimPresent() || tid < 0) return;
			std::scoped_lock l(g_lock);
			auto& t = g_threads[tid];
			t = Thread{};
			t.id = tid;
			t.start = Now();
			t.leadin = true;
			t.normalPreWindow = true;
			t.normalProbe = "thread-start";
			Refresh(tid);
		}

		void OnSceneChanged(int tid, std::string scene)
		{
			if (!EnabledNow()) return;
			std::scoped_lock l(g_lock);
			auto it = g_threads.find(tid);
			if (it == g_threads.end()) {
				// Started before we were listening (e.g. enabled mid-scene).
				OnThreadStart(tid);
				it = g_threads.find(tid);
				if (it == g_threads.end()) return;
			}
			auto& t = it->second;
			if (!scene.empty() && scene != t.sceneID) {
				t.sceneID = scene;
				t.meta = OStimData::GetScene(scene);
				if (t.active) Face::Engine::OnSceneChanged(t);
			}
			Refresh(tid);  // actor list can change with the scene (actors join/leave)
		}

		void OnOrgasm(int tid, RE::FormID actorID)
		{
			std::scoped_lock l(g_lock);
			auto it = g_threads.find(tid);
			auto* a = RE::TESForm::LookupByID<RE::Actor>(actorID);
			if (it == g_threads.end() || !a || !it->second.active) return;
			Face::Engine::OnOrgasm(it->second, a);
		}

		class EventSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* e, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
			{
				if (!e) return RE::BSEventNotifyControl::kContinue;
				const std::string_view name = e->eventName.c_str();
				const std::string str = e->strArg.c_str();
				const float num = e->numArg;
				const RE::FormID sender = e->sender ? e->sender->GetFormID() : 0;
				const int tid = static_cast<int>(num);

				if (name == "ostim_thread_start") Task([tid]() { OnThreadStart(tid); });
				else if (name == "ostim_thread_scenechanged") Task([tid, str]() { OnSceneChanged(tid, str); });
				else if (name == "ostim_thread_speedchanged") Task([tid]() {
					std::scoped_lock l(g_lock);
					if (g_threads.contains(tid)) Refresh(tid);
				});
				else if (name == "ostim_actor_orgasm") Task([tid, sender]() { OnOrgasm(tid, sender); });
				else if (name == "ostim_thread_end") Task([tid]() { EndThread(tid); });
				else if (name == "ostim_end") Task([]() {
					std::scoped_lock l(g_lock);
					std::vector<int> ids;
					for (auto& [id, t] : g_threads) {
						if (t.hasPlayer) ids.push_back(id);
					}
					for (int id : ids) EndThread(id);
				});
				else if (name == "SLED_LipSyncMouth") Task([str, num]() {
					// numArg is an absolute Utility.GetCurrentRealTime() deadline
					std::scoped_lock l(g_lock);
					if (auto* a = RE::TESForm::LookupByID<RE::Actor>(static_cast<RE::FormID>(std::strtoul(str.c_str(), nullptr, 10)))) Face::Engine::SetExternalMouth(a, num);
				});
				else if (name == "SLED_Hint") Task([str]() {
					// "formID|kind|strength"
					const auto p1 = str.find('|');
					const auto p2 = p1 == std::string::npos ? std::string::npos : str.find('|', p1 + 1);
					if (p2 == std::string::npos) return;
					const auto id = static_cast<RE::FormID>(std::strtoul(str.substr(0, p1).c_str(), nullptr, 10));
					std::scoped_lock l(g_lock);
					if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id)) Face::Engine::StoreHint(a, str.substr(p1 + 1, p2 - p1 - 1), std::atoi(str.c_str() + p2 + 1));
				});
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		EventSink g_sink;
	}

	// ------------------------------------------------------------------ Thread
	Slot* Thread::Find(RE::Actor* a)
	{
		if (!a) return nullptr;
		for (auto& s : slots) {
			if (s.id == a->GetFormID()) return &s;
		}
		return nullptr;
	}

	Slot* Thread::FindPos(int p)
	{
		for (auto& s : slots) {
			if (s.pos == p) return &s;
		}
		return nullptr;
	}

	int Thread::PaintedCount() const
	{
		return static_cast<int>(std::ranges::count_if(slots, [](const Slot& s) { return s.painted; }));
	}

	bool Thread::SpellVictim(const Slot& s) const
	{
		return !s.player && std::ranges::find(spellVictims, s.id) != spellVictims.end();
	}

	// ------------------------------------------------------------------ public
	float Now() { return static_cast<float>(RE::GetDurationOfApplicationRunTime()) / 1000.0f; }

	std::recursive_mutex& Lock() { return g_lock; }

	void Init()
	{
		if (auto* src = SKSE::GetModCallbackEventSource()) {
			src->AddEventSink(&g_sink);
			logger::info("Listening for OStim mod events");
		}
	}

	void Tick()
	{
		if (RE::UI::GetSingleton()->GameIsPaused()) return;
		std::scoped_lock l(g_lock);
		if (!EnabledNow()) {
			if (!g_threads.empty()) OnDisabled();
			return;
		}
		const float now = Now();
		for (auto& [id, t] : g_threads) {
			if (!t.active || now < t.nextTick) continue;
			if (!WithinRange(t)) {
				Face::Engine::EndScene(t);
				t.active = false;
				continue;
			}
			Refresh(id);
			PollOverrides(t);
			Face::Engine::OnTick(t);
		}
		std::erase_if(g_threads, [](auto& kv) { return !kv.second.active && kv.second.actorsKnown && kv.second.slots.empty(); });
	}

	void OnGameLoad()
	{
		std::scoped_lock l(g_lock);
		++g_generation;
		g_threads.clear();
		Face::Output::ForgetAll();
		Face::Engine::ClearStrayTongues();
		Face::Engine::RestorePersistedTakeovers();
	}

	void OnDisabled()
	{
		std::scoped_lock l(g_lock);
		std::vector<int> ids;
		for (auto& [id, t] : g_threads) ids.push_back(id);
		for (int id : ids) EndThread(id);
		Face::Engine::RestorePersistedTakeovers();
	}

	Thread* PlayerThread()
	{
		for (auto& [id, t] : g_threads) {
			if (t.active && t.hasPlayer) return &t;
		}
		return nullptr;
	}

	Thread* ThreadOf(RE::Actor* a)
	{
		if (!a) return nullptr;
		for (auto& [id, t] : g_threads) {
			if (t.Find(a)) return &t;
		}
		return nullptr;
	}

	bool InAnyScene(RE::Actor* a) { return ThreadOf(a) != nullptr; }

	std::vector<ThreadStatus> Snapshot()
	{
		std::scoped_lock l(g_lock);
		std::vector<ThreadStatus> out;
		for (auto& [id, t] : g_threads) {
			ThreadStatus ts{ id, t.sceneID, t.hasPlayer, t.consent, t.toneRough, t.normalActive, t.orgasm, t.sceneOral, t.spellNonConsent, t.acceptedBySubmissive, t.speed, t.maxSpeed,
				t.afterglow, t.plateau, t.start > 0.0f ? Now() - t.start : 0.0f, t.normalProbe, {} };
			for (auto& s : t.slots) {
				ts.slots.push_back({ s.name, s.faceOwner, s.mouthOwner, s.eyeOwner, s.headOwner, Face::Engine::PersonalityName(s.arch), s.archSource,
					s.normalMood, s.enj, s.raw, s.dom, s.phrase, s.painted, s.takenOver, s.exprOverride });
			}
			out.push_back(std::move(ts));
		}
		return out;
	}
}
