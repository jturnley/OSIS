#include "SceneLock.h"

#include "Face/Internal.h"
#include "OStimData.h"
#include "Papyrus.h"
#include "Settings.h"

namespace SceneLock
{
	namespace
	{
		constexpr auto kSceneMenu = "OstimSceneMenu";
		constexpr int kRedirectRange = 3;        // navigation steps OStim may walk to get back
		constexpr int kAutoRange = 4;            // auto mode's next scene, like OStim's "limit to navigation distance"
		constexpr float kStartWindow = 6.0f;     // a scene that turns non-consensual this soon: started as one
		constexpr float kTargetTimeout = 12.0f;  // our navigation is still under way
		constexpr float kAutoCheck = 1.0f;       // how often OStim's auto mode is looked at
		constexpr float kStopGrace = 2.5f;       // our StopAutoMode may not have run yet

		struct Lock
		{
			int tid = -1;
			bool locked = false;
			bool player = false;
			std::vector<RE::ActorHandle> actors;  // by OStim position
			std::string scene;
			std::string target;          // where we sent the thread
			float targetUntil = 0.0f;
			bool pending = false;        // a scene query is out
			bool unavailable = false;    // no non-consensual scene fits these actors
			bool ownAuto = false;        // OStim's auto mode is off and we drive the thread
			bool autoQuery = false;
			float stoppedAt = -100.0f;
			float nextAutoCheck = 0.0f;
			float nextAuto = 0.0f;
			float nextCheck = 0.0f;
			std::deque<std::string> recent;
			int redirects = 0;
			int steps = 0;
			int failures = 0;            // our navigations that never arrived, in a row
		};

		std::mutex g_lock;  // after Scenes::Lock() and Settings::lock
		std::unordered_map<int, Lock> g_locks;
		std::string g_last = "none yet";
		std::atomic_bool g_menuTask = false;
		std::atomic<std::uint32_t> g_menuTrims = 0;
		float g_nextMenuCheck = 0.0f;

		std::mt19937& Rng()
		{
			static std::mt19937 rng{ std::random_device{}() };
			return rng;
		}

		float RandF(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(Rng()); }

		std::string Lower(std::string s)
		{
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		const OStimData::TagList& NCTags() { return Face::Engine::detail::T().tagForced; }

		std::string NCTagsCSV()
		{
			std::string out;
			for (const auto& t : NCTags()) {
				if (!out.empty()) out += ',';
				out += t;
			}
			return out;
		}

		struct Config
		{
			bool enabled = false;
			float interval = 20.0f;
		};

		Config ReadConfig()
		{
			std::scoped_lock l(Settings::lock);
			return { Settings::General::bEnabled && Settings::Face::bNCSceneLock, Settings::Face::fNCAutoInterval };
		}

		void Task(std::function<void()> a_fn) { SKSE::GetTaskInterface()->AddTask(std::move(a_fn)); }

		bool IsTransition(std::string_view a_id)
		{
			const auto s = OStimData::GetScene(a_id);
			return s->known && !s->destination.empty();
		}

		std::vector<RE::Actor*> Actors(const Lock& k)
		{
			std::vector<RE::Actor*> out;
			for (const auto& h : k.actors) {
				auto* a = h.get().get();
				if (!a) return {};
				out.push_back(a);
			}
			return out;
		}

		void ReadActors(Lock& k, const Scenes::Thread& t)
		{
			int n = 0;
			for (const auto& s : t.slots) n = std::max(n, s.pos + 1);
			std::vector<RE::ActorHandle> v(static_cast<std::size_t>(n));
			for (const auto& s : t.slots) {
				if (s.pos >= 0) v[s.pos] = s.handle;
			}
			k.actors = std::move(v);
			k.player = t.hasPlayer;
		}

		void Remember(Lock& k, const std::string& a_id)
		{
			k.recent.push_back(a_id);
			if (k.recent.size() > 6) k.recent.pop_front();
		}

		// A non-consensual destination among a_ids: not a transition, not the current scene, and
		// not one of the last few unless nothing else is left.
		std::string Choose(const Lock& k, const std::vector<std::string>& a_ids)
		{
			std::vector<std::string> fresh, repeat;
			for (auto id : a_ids) {
				id = Lower(std::move(id));
				if (id == k.scene || IsTransition(id)) continue;
				const auto s = OStimData::GetScene(id);
				if (!s->known || !OStimData::HasAnySceneTag(*s, NCTags())) continue;
				(std::ranges::find(k.recent, id) != k.recent.end() ? repeat : fresh).push_back(std::move(id));
			}
			auto& from = fresh.empty() ? repeat : fresh;
			if (from.empty()) return {};
			return from[std::uniform_int_distribution<std::size_t>(0, from.size() - 1)(Rng())];
		}

		void Go(Lock& k, const std::string& a_id, bool a_warp, std::string_view a_why)
		{
			k.target = a_id;
			k.targetUntil = Scenes::Now() + kTargetTimeout;
			Remember(k, a_id);
			if (a_warp) Papyrus::WarpTo(k.tid, a_id, k.player);
			else Papyrus::NavigateTo(k.tid, a_id);
			g_last = std::format("thread {}: {} {} -> {}{}", k.tid, a_why, k.scene, a_id, a_warp ? " (warp)" : "");
			logger::info("Scene lock: {}", g_last);
		}

		void GiveUp(Lock& k, std::string_view a_why)
		{
			k.unavailable = true;
			if (k.ownAuto) Papyrus::StartAutoMode(k.tid);  // OStim's own auto mode is better than none
			k.ownAuto = false;
			g_last = std::format("thread {}: {}; left to OStim", k.tid, a_why);
			logger::warn("Scene lock: {}", g_last);
		}

		Lock* Find(int a_tid)
		{
			auto it = g_locks.find(a_tid);
			return it == g_locks.end() ? nullptr : &it->second;
		}

		void Warp(int a_tid, std::string a_why);

		// Move the thread to a non-consensual scene: one within navigation range if there is one
		// (OStim walks there), else any that fits the actors and furniture (OStim's own library,
		// the same pick it makes by furniture type).
		void Move(Lock& k, int a_range, std::string a_why)
		{
			if (k.pending || k.unavailable) return;
			auto actors = Actors(k);
			if (actors.empty()) return;
			k.pending = true;
			const int tid = k.tid;
			Papyrus::GetScenesInRange(k.scene, std::move(actors), a_range, [tid, a_why](std::vector<std::string> a_ids) {
				Task([tid, a_why, ids = std::move(a_ids)]() {
					std::scoped_lock l(g_lock);
					auto* k = Find(tid);
					if (!k) return;
					if (const auto id = Choose(*k, ids); !id.empty()) {
						k->pending = false;
						Go(*k, id, false, a_why);
						return;
					}
					Warp(tid, a_why);
				});
			});
		}

		void Warp(int a_tid, std::string a_why)  // g_lock held
		{
			Papyrus::GetFurnitureType(a_tid, [a_tid, a_why](std::string a_furniture) {
				Task([a_tid, a_why, furniture = std::move(a_furniture)]() {
					std::scoped_lock l(g_lock);
					auto* k = Find(a_tid);
					if (!k) return;
					auto actors = Actors(*k);
					if (actors.empty()) {
						k->pending = false;
						return;
					}
					Papyrus::GetRandomFurnitureSceneWithAnyTag(std::move(actors), furniture, NCTagsCSV(), [a_tid, a_why](std::string a_id) {
						Task([a_tid, a_why, id = Lower(std::move(a_id))]() {
							std::scoped_lock l(g_lock);
							auto* k = Find(a_tid);
							if (!k) return;
							k->pending = false;
							if (id.empty()) GiveUp(*k, "no non-consensual scene fits its actors and furniture");
							else if (id != k->scene) Go(*k, id, true, a_why);
						});
					});
				});
			});
		}

		// The thread is on a scene that doesn't lead to a non-consensual one: send it back.
		void Check(Lock& k, float now)
		{
			if (!k.locked || k.unavailable || k.pending) return;
			if (!k.target.empty()) {
				if (k.scene == k.target) {
					k.failures = 0;
				} else if (now < k.targetUntil) {
					return;  // still walking there
				} else if (++k.failures >= 3) {
					k.target.clear();
					GiveUp(k, "OStim never reached the non-consensual scenes it was sent to");  // stop fighting it
					return;
				}
				k.target.clear();
			}
			if (k.scene.empty() || SceneLock::Allowed(k.scene)) return;
			++k.redirects;
			Move(k, kRedirectRange, "redirected");
		}

		void Engage(Lock& k, std::string_view a_why, float now)
		{
			if (k.locked) return;
			k.locked = true;
			k.nextAutoCheck = 0.0f;
			g_last = std::format("thread {}: {}; scenes limited to non-consensual ones", k.tid, a_why);
			logger::info("Scene lock: {}", g_last);
			Check(k, now);
		}

		void OnAutoMode(int a_tid, bool a_on, float a_interval)
		{
			std::scoped_lock l(g_lock);
			auto* k = Find(a_tid);
			if (!k) return;
			k->autoQuery = false;
			if (!k->locked || k->unavailable || !a_on) return;
			const float now = Scenes::Now();
			Papyrus::StopAutoMode(a_tid);
			if (k->ownAuto && k->player && now - k->stoppedAt > kStopGrace) {
				// Auto mode looked on to the player and they pressed OStim's auto-mode key: hand it back.
				k->ownAuto = false;
				g_last = std::format("thread {}: auto mode off (player)", a_tid);
				return;
			}
			k->stoppedAt = now;
			if (!k->ownAuto) {
				k->ownAuto = true;
				k->nextAuto = now + a_interval * RandF(0.6f, 1.4f);
				g_last = std::format("thread {}: auto mode taken over (non-consensual scenes only)", a_tid);
				logger::info("Scene lock: {}", g_last);
			}
		}

		// UI thread. OStim's scene menu keeps its options in the movie (optionBoxes.Options); the
		// ones that don't lead to a non-consensual scene are dropped and the rest re-assigned.
		// OStim's own handler still runs the selection.
		void TrimMenu()
		{
			g_menuTask = false;
			auto* ui = RE::UI::GetSingleton();
			auto menu = ui ? ui->GetMenu(kSceneMenu) : nullptr;
			if (!menu || !menu->uiMovie) return;
			auto& movie = menu->uiMovie;
			RE::GFxValue boxes;
			if (!movie->GetVariable(&boxes, "_root.optionBoxesContainer.optionBoxes") || !boxes.IsObject()) return;
			RE::GFxValue options, maxIdx;
			if (!boxes.GetMember("Options", &options) || !options.IsArray()) return;
			if (!boxes.GetMember("maxOptionIdx", &maxIdx) || !maxIdx.IsNumber()) return;
			const int n = std::min(static_cast<int>(maxIdx.GetNumber()) + 1, static_cast<int>(options.GetArraySize()));
			if (n <= 0) return;

			struct Option
			{
				RE::GFxValue id, title, image, description;
			};
			std::vector<Option> keep;
			bool trimmed = false;
			for (int i = 0; i < n; ++i) {
				RE::GFxValue o;
				if (!options.GetElement(static_cast<std::uint32_t>(i), &o) || !o.IsObject()) return;
				Option opt;
				if (!o.GetMember("NodeID", &opt.id) || !opt.id.IsString()) return;
				const std::string id = opt.id.GetString();
				if (!OStimData::GetScene(id)->known) return;  // not scenes: OStim's options pages
				if (!SceneLock::Allowed(id)) {
					trimmed = true;
					continue;
				}
				o.GetMember("Title", &opt.title);
				o.GetMember("ImagePath", &opt.image);
				o.GetMember("Description", &opt.description);
				keep.push_back(std::move(opt));
			}
			if (!trimmed) return;
			RE::GFxValue list;
			movie->CreateArray(&list);
			for (auto& opt : keep) {
				RE::GFxValue obj;
				movie->CreateObject(&obj);
				obj.SetMember("NodeID", opt.id);
				obj.SetMember("Title", opt.title);
				obj.SetMember("ImagePath", opt.image);
				obj.SetMember("Description", opt.description);
				list.PushBack(obj);
			}
			boxes.Invoke("AssignData", nullptr, &list, 1);
			g_menuTrims.fetch_add(1, std::memory_order_relaxed);
		}
	}

	bool Allowed(std::string_view a_scene)
	{
		auto s = OStimData::GetScene(a_scene);
		for (int hop = 0; hop < 8 && s->known && !s->destination.empty(); ++hop) s = OStimData::GetScene(s->destination);
		if (!s->known) return true;  // can't read it: don't fight it
		return OStimData::HasAnySceneTag(*s, NCTags());
	}

	void OnThreadReady(Scenes::Thread& t)
	{
		if (!ReadConfig().enabled) return;
		const float now = Scenes::Now();
		{
			std::scoped_lock l(g_lock);
			auto& k = g_locks[t.id];
			k.tid = t.id;
			ReadActors(k, t);
			k.scene = Lower(t.sceneID);
			if (!t.consent) {
				Engage(k, t.spellNonConsent ? "started by the player's spell" : "started in a non-consensual scene", now);
				return;
			}
		}
		// The mod that started it may say so in the thread's metadata.
		const int tid = t.id;
		Papyrus::GetThreadMetadata(tid, [tid](std::vector<std::string> a_meta) {
			for (auto& m : a_meta) m = Lower(std::move(m));
			if (!OStimData::HasAny(a_meta, NCTags())) return;
			Task([tid]() {
				std::scoped_lock l(g_lock);
				if (auto* k = Find(tid)) Engage(*k, "started as non-consensual (thread metadata)", Scenes::Now());
			});
		});
	}

	void OnSceneChanged(Scenes::Thread& t)
	{
		if (!ReadConfig().enabled) return;
		const float now = Scenes::Now();
		std::scoped_lock l(g_lock);
		auto& k = g_locks[t.id];
		k.tid = t.id;
		ReadActors(k, t);
		k.scene = Lower(t.sceneID);
		if (!k.locked) {
			if (!t.consent && now - t.start < kStartWindow) Engage(k, "turned non-consensual as it started", now);
			return;
		}
		Check(k, now);
	}

	void OnSceneEnd(Scenes::Thread& t)
	{
		std::scoped_lock l(g_lock);
		g_locks.erase(t.id);
	}

	void Tick()
	{
		const auto c = ReadConfig();
		const float now = Scenes::Now();
		std::scoped_lock l(g_lock);
		if (!c.enabled) {
			for (auto& [tid, k] : g_locks) {
				if (k.ownAuto) Papyrus::StartAutoMode(tid);
			}
			g_locks.clear();
			return;
		}
		bool playerLocked = false;
		for (auto& [tid, k] : g_locks) {
			if (!k.locked || k.unavailable) continue;
			playerLocked |= k.player;
			// OStim's auto mode picks from every scene: take it over.
			if (!k.autoQuery && now >= k.nextAutoCheck) {
				k.autoQuery = true;
				k.nextAutoCheck = now + kAutoCheck;
				Papyrus::IsInAutoMode(tid, [tid = tid, interval = c.interval](bool a_on) { Task([tid, a_on, interval]() { OnAutoMode(tid, a_on, interval); }); });
			}
			if (k.ownAuto && !k.pending && k.target.empty() && now >= k.nextAuto) {
				k.nextAuto = now + c.interval * RandF(0.6f, 1.4f);
				++k.steps;
				Move(k, kAutoRange, "auto mode");
			}
			if (now >= k.nextCheck) {
				k.nextCheck = now + 1.0f;
				Check(k, now);
			}
		}
		if (playerLocked && now >= g_nextMenuCheck && RE::UI::GetSingleton()->IsMenuOpen(kSceneMenu) && !g_menuTask.exchange(true)) {
			g_nextMenuCheck = now + 0.1f;
			SKSE::GetTaskInterface()->AddUITask(TrimMenu);
		}
	}

	void Clear()
	{
		std::scoped_lock l(g_lock);
		g_locks.clear();
	}

	bool IsLocked(int a_thread)
	{
		std::scoped_lock l(g_lock);
		const auto it = g_locks.find(a_thread);
		return it != g_locks.end() && it->second.locked;
	}

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		int locked = 0, driven = 0, redirects = 0, steps = 0;
		for (const auto& [tid, k] : g_locks) {
			if (!k.locked) continue;
			++locked;
			driven += k.ownAuto;
			redirects += k.redirects;
			steps += k.steps;
		}
		return std::format("{} non-consensual thread(s) held to non-consensual scenes ({} on our auto mode; {} auto step(s), {} redirect(s)); "
		                   "scene menu trimmed {} time(s)\nLast: {}",
			locked, driven, steps, redirects, g_menuTrims.load(), g_last);
	}
}
