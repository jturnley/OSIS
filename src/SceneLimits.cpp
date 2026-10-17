// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "SceneLimits.h"

#include "Debug.h"
#include "Face/Engine.h"
#include "OStimData.h"
#include "Papyrus.h"
#include "Settings.h"

namespace SceneLimits
{
	namespace
	{
		constexpr auto kSceneMenu = "OstimSceneMenu";
		constexpr auto kUserFile = "Data/SKSE/Plugins/OSIS/scene_roles_user.txt";
		constexpr int kRedirectRange = 3;        // navigation steps OStim may walk to get to a scene that fits
		constexpr int kWideRange = 6;            // and the second look when none did
		constexpr int kAutoRange = 4;            // auto mode's next scene, like OStim's "limit to navigation distance"
		constexpr float kTargetTimeout = 12.0f;  // our navigation is still under way
		constexpr float kAutoCheck = 1.0f;       // how often OStim's auto mode is looked at
		constexpr float kStopGrace = 2.5f;       // our StopAutoMode may not have run yet
		constexpr float kAutoFitShare = 0.7f;    // of the scenes our auto mode picks, the share that is one of the limited actor's own kind

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

		std::string Trim(std::string s)
		{
			while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
			while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
			return s;
		}

		void Task(std::function<void()> a_fn) { SKSE::GetTaskInterface()->AddTask(std::move(a_fn)); }

		// ================================================================ who is on top
		const OStimData::TagList kFemLabels{ "cowgirl", "reversecowgirl", "femdom", "facesitting", "riding", "ride" };  // the one with the vagina (or anus) is on top
		const OStimData::TagList kMaleLabels{ "missionary", "doggystyle", "doggy", "behind", "prone", "mating", "standingdoggy", "bentover", "bendover" };  // the one with the penis is on top
		const OStimData::TagList kLying{ "lyingback", "lyingfront", "lyingside", "lying", "laying", "lyinback", "lyinfrong", "lyingbottom" };

		enum class Kind
		{
			kNone,
			kPositional,  // somebody is on top of, or in charge of, somebody: the giver (the action's actor) unless the data says otherwise
			kHole,        // oral on a vagina or an anus: only a label (face-sitting, femdom) or an actor tag says who is on top; otherwise neutral
		};

		Kind KindOf(const std::string& a_type)
		{
			static const std::unordered_set<std::string> positional{
				"vaginalsex", "analsex", "vaginaltailsex", "analtailsex", "vaginalfisting", "analfisting", "vaginaltoying", "analtoying",
				"grindingpenis", "grindingthigh", "grindingfoot", "mounting", "buttsmothering", "breastsmothering", "rubbingpenisagainstface",
				"spanking", "choking", "pullinghair", "breastslapping"
			};
			static const std::unordered_set<std::string> hole{
				"cunnilingus", "lickingvagina", "vulvallicking", "vulvaleating", "3pp_cunnilingus", "anilingus", "anallicking", "rimjob"
			};
			if (positional.contains(a_type)) return Kind::kPositional;
			if (hole.contains(a_type)) return Kind::kHole;
			return Kind::kNone;
		}

		struct Classified
		{
			std::vector<Role> roles;
			std::string why;
		};

		std::mutex g_roleLock;  // a leaf: nothing else is taken while it is held but OStimData's own lock
		std::unordered_map<std::string, Classified> g_classified;
		std::unordered_map<std::string, std::vector<Role>> g_userScene;
		std::vector<std::pair<std::string, std::vector<Role>>> g_userTag;
		std::once_flag g_userOnce;

		const char* RoleLetter(Role r) { return r == Role::kActive ? "a" : (r == Role::kPassive ? "p" : "n"); }

		std::string RolesText(const std::vector<Role>& v)
		{
			std::string out;
			for (const auto r : v) {
				if (!out.empty()) out += ',';
				out += RoleLetter(r);
			}
			return out;
		}

		Role ParseRole(const std::string& a_token)
		{
			const auto t = Lower(Trim(a_token));
			if (t.empty()) return Role::kNeutral;
			switch (t.front()) {
			case 'a':  // active, aggressive
			case 't':  // top
				return Role::kActive;
			case 'p':  // passive
			case 'b':  // bottom
				return Role::kPassive;
			default:   // n, neutral, -
				return Role::kNeutral;
			}
		}

		std::vector<Role> ParseRoles(const std::string& a_text)
		{
			std::vector<Role> out;
			std::size_t start = 0;
			while (start <= a_text.size()) {
				const auto comma = a_text.find(',', start);
				out.push_back(ParseRole(a_text.substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
				if (comma == std::string::npos) break;
				start = comma + 1;
			}
			return out;
		}

		// scene|<scene id>|a,p        the scene's roles, one per actor (a active, p passive, n neutral)
		// tag|<scene tag>|a,p         the same for every scene with the tag (a scene's own line wins)
		void LoadUser()
		{
			std::call_once(g_userOnce, [] {
				std::ifstream f(kUserFile);
				if (!f) return;
				std::string line;
				std::size_t n = 0;
				while (std::getline(f, line)) {
					if (!line.empty() && line.back() == '\r') line.pop_back();
					line = Trim(line);
					if (line.empty() || line[0] == ';' || line[0] == '#') continue;
					const auto b1 = line.find('|');
					const auto b2 = b1 == std::string::npos ? std::string::npos : line.find('|', b1 + 1);
					if (b2 == std::string::npos) continue;
					const auto what = Lower(Trim(line.substr(0, b1)));
					const auto key = Lower(Trim(line.substr(b1 + 1, b2 - b1 - 1)));
					auto roles = ParseRoles(line.substr(b2 + 1));
					if (key.empty()) continue;
					if (what == "scene") g_userScene[key] = std::move(roles);
					else if (what == "tag") g_userTag.emplace_back(key, std::move(roles));
					else continue;
					++n;
				}
				logger::info("Scene limits: {} line(s) of your own from {}", n, kUserFile);
			});
		}

		bool Has(const OStimData::TagList& a_tags, const char* a_tag) { return std::ranges::find(a_tags, a_tag) != a_tags.end(); }

		// Who is on top in one action: true the giver (its actor), false the receiver (its target), nullopt when the data does not say.
		std::optional<bool> GiverOnTop(const OStimData::Scene& s, const OStimData::Action& a, Kind a_kind, std::string& a_why)
		{
			const auto& g = s.actors[static_cast<std::size_t>(a.actor)];
			const auto& r = s.actors[static_cast<std::size_t>(a.target)];
			// 1. the scene's actor tags
			const bool gTop = Has(g.tags, "ontop"), gBottom = Has(g.tags, "onbottom"), rTop = Has(r.tags, "ontop"), rBottom = Has(r.tags, "onbottom");
			if ((gTop && !rTop) || (rBottom && !gBottom)) {
				a_why = "actor tags (ontop / onbottom)";
				return true;
			}
			if ((rTop && !gTop) || (gBottom && !rBottom)) {
				a_why = "actor tags (ontop / onbottom)";
				return false;
			}
			// 2. the scene's tags: cowgirl and the like put the one with the vagina on top, missionary and the like the one with the penis
			const bool fem = OStimData::HasAnySceneTag(s, kFemLabels), male = OStimData::HasAnySceneTag(s, kMaleLabels);
			if (fem != male) {
				const std::uint8_t want = fem ? static_cast<std::uint8_t>(OStimData::kReqVagina | OStimData::kReqAnus) : OStimData::kReqPenis;
				const bool gHas = (OStimData::ActionRequirements(a.type, 0) & want) != 0;
				const bool rHas = (OStimData::ActionRequirements(a.type, 1) & want) != 0;
				if (gHas != rHas) {
					a_why = fem ? "scene tag (cowgirl, femdom...): the vagina is on top" : "scene tag (missionary, doggy...): the penis is on top";
					return gHas;
				}
			}
			if (a_kind != Kind::kPositional) return std::nullopt;
			// 3. postures: whoever is lying down while the other is not is underneath
			const bool gLies = OStimData::HasAny(g.tags, kLying), rLies = OStimData::HasAny(r.tags, kLying);
			if (gLies != rLies) {
				a_why = "postures (the one lying down is underneath)";
				return !gLies;
			}
			// 4. the scene says the receiver does the moving
			if (a.performer == a.target && a.performer != a.actor) {
				a_why = "performer (the receiver does the moving)";
				return false;
			}
			a_why = "the action (the giver leads)";
			return true;
		}

		Classified ClassifyNode(const OStimData::Scene& s)
		{
			Classified out;
			const auto n = s.actors.size();
			out.roles.assign(n, Role::kNeutral);
			// Your own lines: the scene itself, then the first of its tags with one.
			if (const auto it = g_userScene.find(s.id); it != g_userScene.end()) {
				for (std::size_t i = 0; i < n && i < it->second.size(); ++i) out.roles[i] = it->second[i];
				out.why = "your scene_roles_user.txt (scene)";
				return out;
			}
			for (const auto& [tag, roles] : g_userTag) {
				if (!OStimData::HasAny(s.tags, { tag })) continue;
				for (std::size_t i = 0; i < n && i < roles.size(); ++i) out.roles[i] = roles[i];
				out.why = std::format("your scene_roles_user.txt (tag {})", tag);
				return out;
			}
			std::vector<int> active(n, 0), passive(n, 0);
			std::string why;
			for (const auto& a : s.actions) {
				const Kind kind = KindOf(a.type);
				if (kind == Kind::kNone) continue;
				if (a.actor < 0 || a.target < 0 || a.actor == a.target || a.actor >= static_cast<int>(n) || a.target >= static_cast<int>(n)) continue;
				std::string w;
				const auto top = GiverOnTop(s, a, kind, w);
				if (!top) continue;
				const int on = *top ? a.actor : a.target;
				const int under = *top ? a.target : a.actor;
				++active[static_cast<std::size_t>(on)];
				++passive[static_cast<std::size_t>(under)];
				if (why.empty()) why = std::format("{} {}->{}: {}", a.type, a.actor, a.target, w);
			}
			for (std::size_t i = 0; i < n; ++i) {
				if (active[i] > 0 && passive[i] == 0) out.roles[i] = Role::kActive;
				else if (passive[i] > 0 && active[i] == 0) out.roles[i] = Role::kPassive;
			}
			out.why = why.empty() ? "nothing in it puts anyone on top" : why;
			return out;
		}

		Classified Classify(const std::string& a_id, int a_hop = 0)
		{
			const auto s = OStimData::GetScene(a_id);
			if (!s->known) return {};
			if (!s->destination.empty() && a_hop < 8) {
				// A transition is the scene it leads to.
				const auto it = g_userScene.find(s->id);
				if (it == g_userScene.end()) {
					auto out = Classify(s->destination, a_hop + 1);
					if (!out.why.empty()) out.why = std::format("transition to {}: {}", s->destination, out.why);
					return out;
				}
			}
			return ClassifyNode(*s);
		}

		const Classified& ClassifiedFor(std::string_view a_scene, std::unique_lock<std::mutex>& a_held)
		{
			static const Classified kEmpty;
			LoadUser();
			const auto id = Lower(std::string(a_scene));
			a_held = std::unique_lock(g_roleLock);
			if (const auto it = g_classified.find(id); it != g_classified.end()) return it->second;
			auto c = Classify(id);
			if (OStimData::GetScene(id)->known) {
				Debug::Log(Debug::Cat::Scene, "scene limits: {} gives [{}] ({})", id, RolesText(c.roles), c.why);
				return g_classified.emplace(id, std::move(c)).first->second;
			}
			return kEmpty;
		}

		// ================================================================ who is limited
		enum class Dir : std::uint8_t
		{
			kNone,
			kPassive,     // shy, timid, submissive: no scene that puts them on top
			kAggressive,  // dominant, crazed: no scene that puts them underneath
		};

		Dir DirOf(int a_pers)
		{
			using namespace Face::Engine;
			switch (a_pers) {
			case Pers::kShy:
			case Pers::kTimid:
			case Pers::kSubmissive:
				return Dir::kPassive;
			case Pers::kDominant:
			case Pers::kCrazed:
				return Dir::kAggressive;
			default:
				return Dir::kNone;
			}
		}

		struct Limit
		{
			RE::FormID id = 0;
			int pos = -1;
			int pers = -1;
			std::string name;
			Dir dir = Dir::kNone;
			bool lifted = false;  // their own orgasm has come
			bool yields = false;  // the other actor wants the same role: with nobody to give way, neither is limited
			[[nodiscard]] bool Binds() const { return dir != Dir::kNone && !lifted && !yields; }
		};

		struct Thread
		{
			int tid = -1;
			bool player = false;
			bool consent = true;
			bool enabled = false;
			std::string scene;
			std::string signature;
			std::vector<Limit> limits;
			std::vector<RE::ActorHandle> actors;  // by OStim position
			// steering
			std::string target;          // where we sent the thread
			float targetUntil = 0.0f;
			bool pending = false;        // a scene query is out
			bool wide = false;           // the nearby look found nothing: the wide one is out
			bool giveUp = false;         // nothing within reach fits: left to OStim until the scene or the limits change
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

			[[nodiscard]] bool Active() const
			{
				return enabled && consent && std::ranges::any_of(limits, [](const Limit& l) { return l.Binds(); });
			}
		};

		std::mutex g_lock;  // after Scenes::Lock() and Settings::lock; taken before g_roleLock
		std::unordered_map<int, Thread> g_threads;
		std::string g_last = "none yet";
		std::atomic_bool g_menuTask = false;
		std::atomic<std::uint32_t> g_menuTrims = 0;
		float g_nextMenuCheck = 0.0f;
		std::string g_lastMenuNote;  // the trace says a menu's outcome once, not at every look

		struct Config
		{
			bool enabled = false;
			bool player = false;
			bool steer = true;
			bool redirect = true;
			bool notify = true;
			float interval = 20.0f;
		};

		Config ReadConfig()
		{
			std::scoped_lock l(Settings::lock);
			namespace S = Settings::SceneLimits;
			return { Settings::General::bEnabled && S::bEnabled, S::bPlayer, S::bTakeOverAuto, S::bRedirect, S::bNotify, S::fAutoInterval };
		}

		std::string DirText(Dir d) { return d == Dir::kPassive ? "passive scenes only" : (d == Dir::kAggressive ? "aggressive scenes only" : "no limit"); }

		// The first limit a scene breaks, as a line for the player.
		const Limit* Breaks(const Thread& k, const std::vector<Role>& a_roles)
		{
			for (const auto& l : k.limits) {
				if (!l.Binds() || l.pos < 0 || l.pos >= static_cast<int>(a_roles.size())) continue;
				const Role r = a_roles[static_cast<std::size_t>(l.pos)];
				if ((l.dir == Dir::kPassive && r == Role::kActive) || (l.dir == Dir::kAggressive && r == Role::kPassive)) return &l;
			}
			return nullptr;
		}

		// How many of the limited actors this scene gives their own kind of role (a passive one underneath, an aggressive one on top).
		int Fit(const Thread& k, const std::vector<Role>& a_roles)
		{
			int fit = 0;
			for (const auto& l : k.limits) {
				if (!l.Binds() || l.pos < 0 || l.pos >= static_cast<int>(a_roles.size())) continue;
				const Role r = a_roles[static_cast<std::size_t>(l.pos)];
				if ((l.dir == Dir::kPassive && r == Role::kPassive) || (l.dir == Dir::kAggressive && r == Role::kActive)) ++fit;
			}
			return fit;
		}

		bool AllowedLocked(const Thread& k, std::string_view a_scene)
		{
			if (!k.Active()) return true;
			return Breaks(k, RolesOf(a_scene)) == nullptr;
		}

		bool IsTransition(std::string_view a_id)
		{
			const auto s = OStimData::GetScene(a_id);
			return s->known && !s->destination.empty();
		}

		Thread* Find(int a_tid)
		{
			const auto it = g_threads.find(a_tid);
			return it == g_threads.end() ? nullptr : &it->second;
		}

		std::vector<RE::Actor*> Actors(const Thread& k)
		{
			std::vector<RE::Actor*> out;
			for (const auto& h : k.actors) {
				auto* a = h.get().get();
				if (!a) return {};
				out.push_back(a);
			}
			return out;
		}

		void Remember(Thread& k, const std::string& a_id)
		{
			k.recent.push_back(a_id);
			if (k.recent.size() > 6) k.recent.pop_front();
		}

		// A scene among a_ids that fits: not a transition, not the current scene, and not one of the last few unless nothing else is left. Redirects and (a_auto) auto mode both
		// prefer scenes that give a limited actor their own kind of role; auto mode lets the rest in some of the time, so that it is not all one kind of scene.
		std::string Choose(const Thread& k, const std::vector<std::string>& a_ids, bool a_auto)
		{
			struct Cand
			{
				std::string id;
				int fit;
			};
			std::vector<Cand> fresh, repeat;
			for (auto id : a_ids) {
				id = Lower(std::move(id));
				if (id == k.scene || IsTransition(id)) continue;
				const auto s = OStimData::GetScene(id);
				if (!s->known) continue;
				const auto roles = RolesOf(id);
				if (Breaks(k, roles)) continue;
				(std::ranges::find(k.recent, id) != k.recent.end() ? repeat : fresh).push_back({ std::move(id), Fit(k, roles) });
			}
			auto& from = fresh.empty() ? repeat : fresh;
			if (from.empty()) return {};
			const int best = std::ranges::max_element(from, {}, &Cand::fit)->fit;
			std::vector<const Cand*> pool;
			const bool onlyBest = best > 0 && (!a_auto || RandF(0.0f, 1.0f) < kAutoFitShare);
			for (const auto& c : from) {
				if (!onlyBest || c.fit == best) pool.push_back(&c);
			}
			return pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(Rng())]->id;
		}

		void Go(Thread& k, const std::string& a_id, std::string_view a_why)
		{
			k.target = a_id;
			k.targetUntil = Scenes::Now() + kTargetTimeout;
			Remember(k, a_id);
			Papyrus::NavigateTo(k.tid, a_id);
			g_last = std::format("thread {}: {} {} -> {}", k.tid, a_why, k.scene, a_id);
			logger::info("Scene limits: {}", g_last);
			Debug::Log(Debug::Cat::Scene, "scene limits: {}", g_last);
		}

		void GiveUp(Thread& k, std::string_view a_why)
		{
			k.giveUp = true;
			k.pending = false;
			k.wide = false;
			if (k.ownAuto) Papyrus::StartAutoMode(k.tid);  // OStim's own auto mode is better than none
			k.ownAuto = false;
			g_last = std::format("thread {}: {}; left to OStim", k.tid, a_why);
			logger::info("Scene limits: {}", g_last);
		}

		// Hand OStim's auto mode back when we have been driving it.
		void Release(Thread& k)
		{
			if (!k.ownAuto) return;
			k.ownAuto = false;
			Papyrus::StartAutoMode(k.tid);
			g_last = std::format("thread {}: auto mode handed back", k.tid);
		}

		// Move the thread to a scene that fits: one within navigation range (OStim walks there). Nothing near: a second, wider look.
		void Move(Thread& k, int a_range, bool a_auto, std::string a_why)
		{
			if (k.pending || k.giveUp) return;
			auto actors = Actors(k);
			if (actors.empty()) return;
			k.pending = true;
			const int tid = k.tid;
			const std::string from = k.scene;
			Papyrus::GetScenesInRange(from, std::move(actors), a_range, [tid, a_range, a_auto, a_why](std::vector<std::string> a_ids) {
				Task([tid, a_range, a_auto, a_why, ids = std::move(a_ids)]() {
					std::scoped_lock l(g_lock);
					auto* k = Find(tid);
					if (!k) return;
					k->pending = false;
					if (!k->Active()) return;
					if (const auto id = Choose(*k, ids, a_auto); !id.empty()) {
						Go(*k, id, a_why);
						return;
					}
					if (a_range < kWideRange) {
						Move(*k, kWideRange, a_auto, a_why);
						return;
					}
					if (a_auto) {
						k->nextAuto = Scenes::Now() + 5.0f;  // nothing to change to from here; look again soon
						return;
					}
					GiveUp(*k, "no scene within reach fits the actors");
				});
			});
		}

		std::string PersName(int a_pers) { return Face::Engine::PersonalityName(a_pers); }

		// The thread is on a scene that does not fit: send it somewhere that does.
		void Check(Thread& k, const Config& c, float now)
		{
			if (!c.redirect || !k.Active() || k.giveUp || k.pending) return;
			if (!k.target.empty()) {
				if (k.scene == k.target) {
					k.failures = 0;
				} else if (now < k.targetUntil) {
					return;  // still walking there
				} else if (++k.failures >= 3) {
					k.target.clear();
					GiveUp(k, "OStim never reached the scenes it was sent to");  // stop fighting it
					return;
				}
				k.target.clear();
			}
			if (k.scene.empty()) return;
			const auto roles = RolesOf(k.scene);
			const Limit* broken = Breaks(k, roles);
			if (!broken) return;
			++k.redirects;
			const std::string why = broken->dir == Dir::kPassive ? std::format("{} ({}) isn't ready to take the lead", broken->name, PersName(broken->pers))
																 : std::format("{} ({}) won't be underneath", broken->name, PersName(broken->pers));
			if (k.player && c.notify) Papyrus::Notify("OSIS: " + why);
			Move(k, kRedirectRange, false, "redirected (" + why + ")");
		}

		void OnAutoMode(int a_tid, bool a_on)
		{
			std::scoped_lock l(g_lock);
			auto* k = Find(a_tid);
			if (!k) return;
			k->autoQuery = false;
			if (!k->Active() || k->giveUp || !a_on) return;
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
				k->nextAuto = now + RandF(2.0f, 6.0f);
				g_last = std::format("thread {}: auto mode taken over (scenes that fit the limits)", a_tid);
				logger::info("Scene limits: {}", g_last);
			}
		}

		std::string Signature(const Thread& k)
		{
			std::string out;
			for (const auto& l : k.limits) {
				if (l.dir == Dir::kNone) continue;
				out += std::format("{}:{}{}{};", l.pos, static_cast<int>(l.dir), l.lifted ? "L" : "", l.yields ? "Y" : "");
			}
			return out;
		}

		std::string LimitsText(const Thread& k)
		{
			std::string out;
			for (const auto& l : k.limits) {
				if (l.dir == Dir::kNone) continue;
				if (!out.empty()) out += "; ";
				out += std::format("{} ({}): {}{}", l.name, PersName(l.pers), DirText(l.dir), l.lifted ? ", lifted: their own orgasm has come" : (l.yields ? ", not limited: their partner wants the same role" : ""));
			}
			return out.empty() ? std::string("nobody is limited") : out;
		}

		void Refresh(Scenes::Thread& t, bool a_sceneChanged)
		{
			const auto c = ReadConfig();
			std::scoped_lock l(g_lock);
			auto& k = g_threads[t.id];
			k.tid = t.id;
			k.player = t.hasPlayer;
			k.consent = t.consent;
			k.enabled = c.enabled;
			const std::string scene = Lower(t.sceneID);
			if (scene != k.scene) {
				k.scene = scene;
				k.giveUp = false;
				k.failures = 0;
			}
			int n = 0;
			for (const auto& s : t.slots) n = std::max(n, s.pos + 1);
			std::vector<RE::ActorHandle> v(static_cast<std::size_t>(n));
			for (const auto& s : t.slots) {
				if (s.pos >= 0) v[static_cast<std::size_t>(s.pos)] = s.handle;
			}
			k.actors = std::move(v);

			std::vector<Limit> next;
			int live = 0;
			for (const auto& s : t.slots) {
				auto* a = s.Get();
				if (!a) continue;
				++live;
				Limit lim;
				lim.id = s.id;
				lim.pos = s.pos;
				lim.name = a->GetDisplayFullName();
				lim.pers = Face::Engine::Archetype(a);
				const bool player = a->IsPlayerRef();
				lim.dir = (c.enabled && t.consent && (!player || c.player)) ? DirOf(lim.pers) : Dir::kNone;
				lim.lifted = s.orgasms > 0;
				next.push_back(std::move(lim));
			}
			// A scene with one actor has nobody to be on top of or underneath: no limit, and no reason to touch OStim's auto mode (its own pacing ends such a scene).
			if (live < 2) {
				for (auto& lim : next) lim.dir = Dir::kNone;
			}
			// Two actors that want the same role (two aggressive, or two passive) cannot both have it, and limiting either to the scenes that suit them leaves very few: neither is limited.
			if (live == 2) {
				std::vector<Limit*> want;
				for (auto& lim : next) {
					if (lim.Binds()) want.push_back(&lim);
				}
				if (want.size() == 2 && want[0]->dir == want[1]->dir) {
					want[0]->yields = true;
					want[1]->yields = true;
				}
			}
			k.limits = std::move(next);

			const auto sig = Signature(k);
			if (sig != k.signature) {
				k.signature = sig;
				k.giveUp = false;
				k.failures = 0;
				g_last = std::format("thread {}: {}", k.tid, LimitsText(k));
				if (!sig.empty()) logger::info("Scene limits: {}", g_last);
				Debug::Log(Debug::Cat::Scene, "scene limits: {}", g_last);
			}
			if (!k.Active()) {
				Release(k);
				return;
			}
			if (a_sceneChanged || !k.target.empty()) Check(k, c, Scenes::Now());
		}

		// UI thread. OStim's scene menu keeps its options in the movie (optionBoxes.Options); the ones that do not fit are dropped and the rest re-assigned. OStim's own handler still
		// runs the selection. Never to nothing.
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

			const auto allowed = [](const std::string& a_id) {
				std::scoped_lock l(g_lock);
				for (const auto& [tid, k] : g_threads) {
					if (k.player && k.Active()) return AllowedLocked(k, a_id);
				}
				return true;
			};

			struct Option
			{
				RE::GFxValue id, title, image, description;
			};
			std::vector<Option> keep;
			std::string hidden;
			bool trimmed = false;
			for (int i = 0; i < n; ++i) {
				RE::GFxValue o;
				if (!options.GetElement(static_cast<std::uint32_t>(i), &o) || !o.IsObject()) return;
				Option opt;
				if (!o.GetMember("NodeID", &opt.id) || !opt.id.IsString()) return;
				const std::string id = opt.id.GetString();
				if (!OStimData::GetScene(id)->known) return;  // not scenes: OStim's options pages
				if (!allowed(id)) {
					trimmed = true;
					hidden += (hidden.empty() ? "" : ", ") + id;
					continue;
				}
				o.GetMember("Title", &opt.title);
				o.GetMember("ImagePath", &opt.image);
				o.GetMember("Description", &opt.description);
				keep.push_back(std::move(opt));
			}
			if (!trimmed) return;  // nothing to hide
			if (keep.empty()) {    // nothing would be left: leave the menu as it is
				const auto note = std::format("menu left alone: all {} option(s) would be hidden ({})", n, hidden);
				if (note != g_lastMenuNote) {
					g_lastMenuNote = note;
					Debug::Log(Debug::Cat::Scene, "scene limits: {}", note);
				}
				return;
			}
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
			g_lastMenuNote.clear();
			Debug::Log(Debug::Cat::Scene, "scene limits: menu trimmed: hid {} of {} option(s): {}", n - static_cast<int>(keep.size()), n, hidden);
		}
	}

	std::vector<Role> RolesOf(std::string_view a_scene)
	{
		std::unique_lock<std::mutex> held;
		return ClassifiedFor(a_scene, held).roles;
	}

	std::string ExplainRoles(std::string_view a_scene)
	{
		std::unique_lock<std::mutex> held;
		const auto& c = ClassifiedFor(a_scene, held);
		if (c.roles.empty()) return "unknown scene";
		return std::format("[{}] {}", RolesText(c.roles), c.why);
	}

	bool Allowed(int a_thread, std::string_view a_scene)
	{
		std::scoped_lock l(g_lock);
		const auto* k = Find(a_thread);
		return !k || AllowedLocked(*k, a_scene);
	}

	void OnThreadReady(Scenes::Thread& t) { Refresh(t, true); }

	void OnSceneChanged(Scenes::Thread& t) { Refresh(t, true); }

	void OnTick(Scenes::Thread& t) { Refresh(t, false); }

	void OnSceneEnd(Scenes::Thread& t)
	{
		std::scoped_lock l(g_lock);
		g_threads.erase(t.id);
	}

	void Tick()
	{
		const auto c = ReadConfig();
		const float now = Scenes::Now();
		std::scoped_lock l(g_lock);
		bool playerLimited = false;
		for (auto& [tid, k] : g_threads) {
			if (!c.enabled || !k.Active() || (!c.steer && k.ownAuto)) {
				Release(k);
				if (!c.enabled) continue;
			}
			if (!k.Active()) continue;
			playerLimited |= k.player;
			// OStim's auto mode picks from every scene: take it over.
			if (c.steer && !k.giveUp) {
				if (!k.autoQuery && now >= k.nextAutoCheck) {
					k.autoQuery = true;
					k.nextAutoCheck = now + kAutoCheck;
					Papyrus::IsInAutoMode(tid, [tid = tid](bool a_on) { Task([tid, a_on]() { OnAutoMode(tid, a_on); }); });
				}
				if (k.ownAuto && !k.pending && k.target.empty() && now >= k.nextAuto) {
					k.nextAuto = now + c.interval * RandF(0.6f, 1.4f);
					++k.steps;
					Move(k, kAutoRange, true, "auto mode");
				}
			}
			if (now >= k.nextCheck) {
				k.nextCheck = now + 1.0f;
				Check(k, c, now);
			}
		}
		if (!c.enabled) {
			g_threads.clear();
			return;
		}
		if (playerLimited && now >= g_nextMenuCheck && RE::UI::GetSingleton()->IsMenuOpen(kSceneMenu) && !g_menuTask.exchange(true)) {
			g_nextMenuCheck = now + 0.1f;
			SKSE::GetTaskInterface()->AddUITask(TrimMenu);
		}
	}

	void Clear()
	{
		std::scoped_lock l(g_lock);
		g_threads.clear();
	}

	std::string Describe(RE::Actor* a_actor)
	{
		if (!a_actor) return "no actor";
		std::scoped_lock l(g_lock);
		for (const auto& [tid, k] : g_threads) {
			for (const auto& lim : k.limits) {
				if (lim.id != a_actor->GetFormID()) continue;
				if (!k.enabled) return "off";
				if (!k.consent) return "none (not a consensual scene)";
				if (k.limits.size() < 2) return "none (alone in the scene)";
				if (lim.dir == Dir::kNone) return std::format("none ({})", PersName(lim.pers));
				return std::format("{} ({}){}", DirText(lim.dir), PersName(lim.pers), lim.lifted ? ", lifted: their own orgasm has come" : (lim.yields ? ", not limited: their partner wants the same role" : ""));
			}
		}
		return "not in a scene";
	}

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		int limited = 0, driven = 0, redirects = 0, steps = 0;
		std::string scenes;
		for (const auto& [tid, k] : g_threads) {
			if (!k.Active()) continue;
			++limited;
			driven += k.ownAuto;
			redirects += k.redirects;
			steps += k.steps;
			scenes += std::format("\nThread {} is on {}: {}; {}", tid, k.scene, ExplainRoles(k.scene), LimitsText(k));
		}
		return std::format("{} thread(s) limited by personality ({} on our auto mode; {} auto step(s), {} redirect(s)); scene menu trimmed {} time(s){}\nLast: {}",
			limited, driven, steps, redirects, g_menuTrims.load(), scenes, g_last);
	}
}
