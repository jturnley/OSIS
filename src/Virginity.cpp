// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Virginity.h"

#include "Debug.h"
#include "Experience.h"
#include "Face/Engine.h"
#include "LipSync.h"
#include "OStimData.h"
#include "Openers.h"
#include "Papyrus.h"
#include "Settings.h"

namespace Virginity
{
	namespace
	{
		namespace P = Face::Engine::Pers;
		namespace V = Settings::Virginity;

		constexpr auto kConfig = "Data/SKSE/Plugins/StorageUtilData/ovirginity_config.json";
#if OSIS_BIO_TABLES
		constexpr auto kReligion = "Data/SKSE/Plugins/OSIS/npc_religion.txt";
#endif
		constexpr auto kReligionUser = "Data/SKSE/Plugins/OSIS/npc_religion_user.txt";
		constexpr float kBrokenBase = 0.02f;   // the multiplier on OStim's excitement rate for a broken woman: a tenth of a stoic's
		constexpr float kBrokenStep = 0.01f;   // and what every silent "orgasm" adds to it
		constexpr int kBrokenCycles = 10;      // silent orgasms in one session, with nothing rough in it, that make her stoic

		// What the scene has done so far for one woman. Gone when the scene ends.
		struct Rt
		{
			int virgin = -1;                 // OVirginity's answer: 1 yes, 0 no, -1 not known yet
			bool asked = false;
			bool startedVirgin = false;      // she was a virgin when this scene found her
			bool penetrated = false;         // the scene has reached the vaginal sex that ends it
			int orgasmsBase = 0;             // her orgasms (Slot::orgasms) when the scene found her
			int orgasmsAtPenetration = 0;
			bool rough = false;              // the penetration came in a rough scene
			bool fast = false;               // the speed went past the limit before she had an orgasm
			int cycles = 0;                  // a broken woman's silent orgasms this session
			bool negative = false;           // something rough, or not consensual, was seen this session
			bool openerDone = false;         // her opening line has been said (or passed over)
			float openerAt = 0.0f;           // scene time it is due, 0 until the scene has found her a virgin
		};

		std::mutex g_lock;
		std::unordered_map<RE::FormID, Record> g_records;
		std::unordered_map<RE::FormID, Rt> g_rt;
		std::unordered_map<RE::FormID, int> g_answers;  // what OVirginity's data said for each woman asked: 1, 0, or -1 not calculated yet
		std::unordered_set<RE::FormID> g_calculating;   // asked OVirginity to work it out
		std::unordered_map<RE::FormID, float> g_askedAt;  // scene time OVirginity's data was asked about them, for the debug trace
		std::unordered_set<RE::FormID> g_pending;       // VirginAnswer has asked OVirginity's data about them and the answer is not back yet
		std::unordered_set<std::string> g_alwaysVirgin;  // ovirginity_config.json's lists, by name
		std::unordered_set<std::string> g_neverVirgin;
		std::once_flag g_once;
		bool g_present = false;                          // OVirginity Reflowered is installed
		RE::TESQuest* g_quest = nullptr;
		std::atomic<std::uint32_t> g_firstTimes{ 0 };    // first scenes given a personality this session
		std::atomic<std::uint32_t> g_deflowered{ 0 };

		enum class Faith { kNone, kReligious, kDibella };
		std::unordered_map<std::string, Faith> g_faith;       // npc_religion.txt
		std::unordered_map<std::string, Faith> g_faithUser;   // npc_religion_user.txt

		std::mt19937& Rng()
		{
			static std::mt19937 rng{ std::random_device{}() };
			return rng;
		}

		// 0 .. a_n - 1
		int Roll(int a_n)
		{
			std::scoped_lock l(g_lock);
			return std::uniform_int_distribution<int>(0, std::max(1, a_n) - 1)(Rng());
		}

		std::string Normalise(std::string_view a_name)
		{
			std::string out;
			bool gap = true;
			for (const unsigned char c : a_name) {
				if (std::isalnum(c)) {
					out.push_back(static_cast<char>(std::tolower(c)));
					gap = false;
				} else if (!gap) {
					out.push_back(' ');
					gap = true;
				}
			}
			if (!out.empty() && out.back() == ' ') out.pop_back();
			return out;
		}

		void ReadFaith(const char* a_path, std::unordered_map<std::string, Faith>& a_into, std::unordered_set<std::string>* a_none = nullptr)
		{
			std::ifstream f(a_path);
			if (!f) return;
			std::string line;
			while (std::getline(f, line)) {
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (line.empty() || line[0] == ';' || line[0] == '#') continue;
				const auto bar = line.find('|');
				if (bar == std::string::npos) continue;
				const std::string name = Normalise(std::string_view(line).substr(0, bar));
				std::string kind = line.substr(bar + 1);
				while (!kind.empty() && std::isspace(static_cast<unsigned char>(kind.back()))) kind.pop_back();
				std::ranges::transform(kind, kind.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (name.empty()) continue;
				if (kind == "dibella") a_into[name] = Faith::kDibella;
				else if (kind == "religious") a_into[name] = Faith::kReligious;
				else if (kind == "none") a_into[name] = Faith::kNone;
			}
			(void)a_none;
		}

		void EnsureLoaded()
		{
			std::call_once(g_once, [] {
				if (auto* dh = RE::TESDataHandler::GetSingleton()) {
					g_quest = dh->LookupForm<RE::TESQuest>(0x800, "OVirginity.esp");
					g_present = g_quest != nullptr;
				}
				std::ifstream f(kConfig);
				if (f) {
					const auto doc = json::parse(f, nullptr, false, true);
					if (doc.is_object() && doc.contains("stringList") && doc.at("stringList").is_object()) {
						const auto& lists = doc.at("stringList");
						const auto read = [&](const char* a_key, std::unordered_set<std::string>& a_into) {
							if (!lists.contains(a_key) || !lists.at(a_key).is_array()) return;
							for (const auto& n : lists.at(a_key)) {
								if (n.is_string()) a_into.insert(Normalise(n.get<std::string>()));
							}
						};
						read("alwaysvirgin", g_alwaysVirgin);
						read("nevervirgin", g_neverVirgin);
					}
				}
#if OSIS_BIO_TABLES
				ReadFaith(kReligion, g_faith);
#endif
				ReadFaith(kReligionUser, g_faithUser);
				logger::info("First times: OVirginity Reflowered {}; {} always and {} never virgin by name (ovirginity_config.json); {} religious or Dibella names ({} of your own)", g_present ? "found" : "not found, so nothing here applies",
					g_alwaysVirgin.size(), g_neverVirgin.size(), g_faith.size(), g_faithUser.size());
			});
		}

		// Whether the woman is religious, from the tables by name and from the temples and the priesthood of the base game.
		Faith FaithOf(RE::Actor* a_actor)
		{
			const std::string name = Normalise(a_actor->GetDisplayFullName());
			if (const auto it = g_faithUser.find(name); it != g_faithUser.end()) return it->second;
			if (const auto it = g_faith.find(name); it != g_faith.end()) return it->second;
			const auto inFaction = [&](RE::FormID a_id) {
				auto* f = RE::TESForm::LookupByID<RE::TESFaction>(a_id);
				return f && a_actor->IsInFaction(f);
			};
			// Markarth's Temple of Dibella and its inner sanctum
			if (inFaction(0x656EA) || inFaction(0x656ED)) return Faith::kDibella;
			// Windhelm's temple and priests, Whiterun's Temple of Kynareth, Solitude's Temple of the Divines, Falkreath's House of Arkay, Riften's Temple of Mara, the Vigilants of Stendarr, priests
			for (const RE::FormID id : { 0x103961u, 0x45F61u, 0xECC44u, 0xC13FDu, 0xC139Au, 0xA2526u, 0xB3292u, 0x5159Au }) {
				if (inFaction(id)) return Faith::kReligious;
			}
			return Faith::kNone;
		}

		const char* FaithName(Faith a_faith)
		{
			return a_faith == Faith::kDibella ? "worships Dibella" : (a_faith == Faith::kReligious ? "religious" : "not religious");
		}

		Record& Rec(RE::FormID a_id)
		{
			return g_records[a_id];  // the caller holds g_lock
		}

		std::int32_t AsInt(const RE::BSScript::Variable& v)
		{
			if (v.IsInt()) return v.GetSInt();
			if (v.IsBool()) return v.GetBool() ? 1 : 0;
			if (v.IsFloat()) return static_cast<std::int32_t>(v.GetFloat());
			return -1;
		}

		// Keep an answer from OVirginity (1 virgin, 0 not, -1 not worked out yet); the caller holds g_lock. Two things do not replace an answer in hand: a late "not worked out yet" (the
		// lists, or OVirginity's own roll, may have settled it since the question was asked), and, while she is in a scene, a 0 for a 1: OVirginity ends a virginity itself when the
		// penetration begins, and the question may be answered after that, but what this is for is what the scene found. (The answers are dropped at the scene's end.)
		void Store(RE::FormID a_id, int a_answer, const char* a_via)
		{
			float ago = 0.0f;
			if (const auto at = g_askedAt.find(a_id); at != g_askedAt.end()) ago = Scenes::Now() - at->second;
			const auto it = g_answers.find(a_id);
			bool kept = false;
			if (it != g_answers.end()) {
				if (a_answer == -1 && (it->second == 0 || it->second == 1)) kept = true;
				else if (a_answer == 0 && it->second == 1 && g_rt.contains(a_id)) kept = true;
			}
			Debug::Log(Debug::Cat::Pers, "{}: first times: OVirginity says {} ({}, {:.1f} s after it was asked){}", Debug::Label(a_id), a_answer == 1 ? "virgin" : (a_answer == 0 ? "not a virgin" : "not worked out yet"), a_via, ago,
				kept ? std::format(" - keeping {}", it->second) : std::string());
			if (!kept) g_answers[a_id] = a_answer;
		}

		// OVirginity keeps a woman's answer in OStim's NPC data ("IsVirgin": 1, 0, or -1 for not worked out yet). The woman goes in as an Actor: both OUtils.GetNPCDataInt and
		// OVirginityScript.isVirgin take one, and an ObjectReference-typed argument arrives as None (GetNPCDataInt then answers -1 for everyone and isVirgin(None) is false, so every
		// woman read as not a virgin).
		void Ask(RE::Actor* a_actor)
		{
			const RE::FormID id = a_actor->GetFormID();
			{
				std::scoped_lock l(g_lock);
				g_askedAt[id] = Scenes::Now();
			}
			Papyrus::CallStatic("OUtils", "GetNPCDataInt", RE::MakeFunctionArguments(std::move(a_actor), RE::BSFixedString("IsVirgin")),
				[id](const RE::BSScript::Variable& v) {
					std::scoped_lock l(g_lock);
					g_pending.erase(id);
					Store(id, AsInt(v), "its data");
				});
		}

		// Not worked out yet: the config's lists decide some by name; for the rest OVirginity is asked to roll it (isVirgin does, and keeps the answer).
		void Settle(RE::Actor* a_actor)
		{
			const RE::FormID id = a_actor->GetFormID();
			const std::string name = Normalise(a_actor->GetActorBase() ? a_actor->GetActorBase()->GetName() : "");
			std::scoped_lock l(g_lock);
			if (g_alwaysVirgin.contains(name)) {
				Store(id, 1, "ovirginity_config.json's always-virgin list");
				return;
			}
			if (g_neverVirgin.contains(name)) {
				Store(id, 0, "ovirginity_config.json's never-virgin list");
				return;
			}
			if (!g_calculating.insert(id).second || !g_quest) return;
			g_askedAt[id] = Scenes::Now();
			Papyrus::CallMethod(g_quest, "OVirginityScript", "isVirgin", RE::MakeFunctionArguments(std::move(a_actor)),
				[id](const RE::BSScript::Variable& v) {
					std::scoped_lock l2(g_lock);
					Store(id, AsInt(v) > 0 ? 1 : 0, "its roll");
				});
		}

		const OStimData::TagList& VaginalSex()
		{
			static const OStimData::TagList list = OStimData::SplitCSV("vaginalsex");
			return list;
		}

		// The first scene of a virgin woman who gave consent: her personality.
		int ChooseFirst(RE::Actor* a_actor, std::string& a_why)
		{
			const Faith faith = FaithOf(a_actor);
			if (faith == Faith::kDibella) {
				static constexpr int kPicks[3] = { P::kBalanced, P::kVocal, P::kWild };
				a_why = "a virgin who worships Dibella";
				return kPicks[Roll(3)];
			}
			if (faith == Faith::kReligious) {
				a_why = "a religious virgin";
				return P::kShy;
			}
			const int roll = Roll(100);
			if (roll < V::iShyChance) {
				a_why = "a virgin's first consensual scene (shy)";
				return P::kShy;
			}
			if (roll < V::iShyChance + V::iTimidChance) {
				a_why = "a virgin's first consensual scene (timid)";
				return P::kTimid;
			}
			std::string source;
			const int p = Face::Engine::RollPersonality(a_actor, &source);
			a_why = "a virgin's first consensual scene (the usual rules: " + source + ")";
			return p;
		}

#if !OSIS_LITE
		// A non-consensual first time: broken 28%, stoic 28%, shy 24%, submissive 10%, 5% crazed about the first partner who gives her an orgasm, and 5% dominant (taken from broken, stoic and shy).
		int ChooseTrauma(RE::Actor* a_actor, Record& a_rec, std::string& a_why)
		{
			const int roll = Roll(100);
			if (roll < 28) {
				a_rec.broken = 1;
				a_rec.brokenRate = kBrokenBase;
				a_why = "a non-consensual first time (broken)";
				return P::kStoic;  // what she is as a personality while broken, and what she becomes when she is not
			}
			if (roll < 56) {
				a_why = "a non-consensual first time (stoic)";
				return P::kStoic;
			}
			if (roll < 80) {
				a_why = "a non-consensual first time (shy)";
				return P::kShy;
			}
			if (roll < 90) {
				a_why = "a non-consensual first time (submissive)";
				return P::kSubmissive;
			}
			if (roll >= 95) {
				a_why = "a non-consensual first time (dominant)";
				return P::kDominant;
			}
			a_rec.pendingCrazed = 1;
			std::string source;
			const int p = Face::Engine::RollPersonality(a_actor, &source);
			a_why = "a non-consensual first time (crazed about the first partner who gives her an orgasm; otherwise " + source + ")";
			return p;
		}
#endif

		// An extraordinary first time: balanced, vocal or wild, or (when the chances leave room) she stays as she is.
		int ChooseExtraordinary(int a_orgasms)
		{
			const int above = std::max(0, a_orgasms - V::iExtraordinaryOrgasms);
			double balanced = std::max(0, V::iExtraordinaryChance - V::iExtraordinaryStep * above);
			double other = V::iExtraordinaryChance + V::iExtraordinaryStep * above;  // each of vocal and wild
			const double total = balanced + 2.0 * other;
			if (total > 100.0) {
				balanced = balanced * 100.0 / total;
				other = other * 100.0 / total;
			}
			const double draw = Roll(10000) / 100.0;
			if (draw < balanced) return P::kBalanced;
			if (draw < balanced + other) return P::kVocal;
			if (draw < balanced + 2.0 * other) return P::kWild;
			return -1;
		}

		void Resolve([[maybe_unused]] Scenes::Thread& a_thread, Scenes::Slot& a_slot, RE::Actor* a_actor, Rt& a_rt)
		{
			Record rec;
			{
				std::scoped_lock l(g_lock);
				rec = Rec(a_slot.id);
			}
			const int orgasms = std::max(0, a_slot.orgasms - a_rt.orgasmsBase);
			int next = -1;
			std::string why;
			if (a_rt.penetrated) {
				// The scene that ended it.
				const bool noOrgasmBefore = a_rt.orgasmsAtPenetration <= a_rt.orgasmsBase;
#if !OSIS_LITE
				if (!a_thread.consent) {
					why = "her first time was not consensual: her first scene decided";
				} else
#endif
				if (orgasms >= V::iExtraordinaryOrgasms && !a_rt.rough) {
					next = ChooseExtraordinary(orgasms);
					why = std::format("an extraordinary first time ({} orgasms)", orgasms);
				} else if (noOrgasmBefore || a_rt.rough || a_rt.fast) {
					const bool bad = Roll(100) < V::iNegativeStoicChance;
					if (bad) next = P::kStoic;
					why = std::format("a bad first time ({}{}{}): {}", noOrgasmBefore ? "no orgasm before the penetration" : "", a_rt.rough ? (noOrgasmBefore ? ", rough" : "rough") : "",
						a_rt.fast ? ((noOrgasmBefore || a_rt.rough) ? ", too fast too soon" : "too fast too soon") : "", bad ? "it marked her" : "it did not");
				} else {
					why = "an ordinary first time";
				}
				{
					std::scoped_lock l(g_lock);
					Rec(a_slot.id).state = 2;
				}
				g_deflowered.fetch_add(1, std::memory_order_relaxed);
			} else if (a_rt.startedVirgin && rec.state == 1 && orgasms >= 1) {
				// A scene with an orgasm that did not end it.
				if (Roll(100) < V::iBalancedChance) {
					next = P::kBalanced;
					why = "an orgasm and still a virgin: the start of a healthy relationship with her own sexuality";
				}
			}
			if (next >= 0) {
				if (Face::Engine::SetAutomaticPersonality(a_actor, next, why)) Debug::Log(Debug::Cat::Pers, "{}: first times: {}", Debug::Label(a_actor), why);
			} else if (!why.empty()) {
				Debug::Log(Debug::Cat::Pers, "{}: first times: {}", Debug::Label(a_actor), why);
			}
		}
	}

	void OnThreadReady(Scenes::Thread& t)
	{
		if (!V::bEnabled && !V::bMaleVirginRate) return;
		EnsureLoaded();
		std::vector<RE::Actor*> ask;
		{
			std::scoped_lock l(g_lock);
			for (auto& s : t.slots) {
				auto* a = s.Get();
				if (!s.female) {  // a man: only his rate depends on it (the player's too)
					if (V::bMaleVirginRate && a) ask.push_back(a);
					continue;
				}
				if (!V::bEnabled) continue;
				auto& rt = g_rt[s.id];
				rt.orgasmsBase = s.orgasms;
				if (a && !a->IsPlayerRef()) ask.push_back(a);
			}
		}
		// Ask at once: the question waits in the script engine's queue behind everything OSIS and OStim send as a scene starts (seen: 6 to 9 s), and OVirginity ends a virginity itself
		// at the penetration, so a scene that starts on the sex must be asked before that.
		for (auto* a : ask) {
			if (!Face::Engine::IsCompanion(a)) (void)VirginAnswer(a);
		}
	}

	void OnSceneChanged(Scenes::Thread&) {}

	void OnTick(Scenes::Thread& t)
	{
		if (!V::bEnabled) return;
		EnsureLoaded();
		for (auto& s : t.slots) {
			auto* a = s.Get();
			if (!a || !s.female || a->IsPlayerRef()) continue;
			if (Face::Engine::IsCompanion(a)) {  // a Companion's blood is feral, not sentimental: no first time marks her, and nothing an earlier save recorded (broken included) holds
				std::scoped_lock l(g_lock);
				g_records.erase(s.id);
				continue;
			}
			// Broken (full edition): watch what the session does to her.
			Rt* rt;
			{
				std::scoped_lock l(g_lock);
				rt = &g_rt[s.id];
			}
			if (!t.consent || t.toneForced || t.toneRough) rt->negative = true;
			if (!g_present) continue;

			if (!rt->asked) {
				rt->asked = true;
				Ask(a);
			}
			int raw = -2;
			{
				std::scoped_lock l(g_lock);
				if (const auto it = g_answers.find(s.id); it != g_answers.end()) raw = it->second;
			}
			if (raw == -2) continue;
			if (raw == -1) {
				Settle(a);
				continue;
			}
			rt->virgin = raw;
			if (raw != 1 && !rt->startedVirgin) continue;

			// A virgin the scene has just found: her first scene gives her a personality.
			Record rec;
			{
				std::scoped_lock l(g_lock);
				rec = Rec(s.id);
			}
			if (!rt->startedVirgin && rec.state == 0) {
				rt->startedVirgin = true;
				std::string why;
				int pers = -1;
				Record updated = rec;
				if (t.consent) {
					pers = ChooseFirst(a, why);
#if !OSIS_LITE
				} else {
					pers = ChooseTrauma(a, updated, why);
#else
				} else {
					continue;
#endif
				}
				updated.state = 1;
				const bool set = Face::Engine::SetAutomaticPersonality(a, pers, why);
				updated.original = set ? pers : Face::Engine::Archetype(a);
				if (!set) {  // one the player set stays: the first scene changes nothing
					updated.broken = 0;
					updated.pendingCrazed = 0;
				}
				{
					std::scoped_lock l(g_lock);
					Rec(s.id) = updated;
				}
				g_firstTimes.fetch_add(1, std::memory_order_relaxed);
				Debug::Log(Debug::Cat::Pers, "{}: first times: {}{}", Debug::Label(a), why, set ? "" : " (not applied: the personality is hand-set)");
				if (set) logger::info("First times: {} ({:08X}) is {}: {}", a->GetDisplayFullName(), a->GetFormID(), Face::Engine::PersonalityName(Face::Engine::EditionPersonality(pers)), why);
			} else if (rec.state == 1 && raw == 1) {
				rt->startedVirgin = true;
			}

			// Her opening line, once, in a consensual scene: a second after the scene found her a virgin (the face engine, which this runs from, starts several seconds into the scene,
			// so a scene that opens on the sex itself is already penetrating by then; she says it anyway), waiting up to six seconds for a line already on her to end.
			if (V::bOpeners && rt->startedVirgin && !rt->openerDone && t.consent) {
				const float now = Scenes::Now();
				if (rt->openerAt <= 0.0f) {
					rt->openerAt = now + 1.0f;
				} else if (now >= rt->openerAt) {
					if (LipSync::SpokenLine(a) && now < rt->openerAt + 6.0f) {
						// try again next tick
					} else {
						rt->openerDone = true;
						Debug::Log(Debug::Cat::Pers, "{}: virgin opener due ({}{})", Debug::Label(a), rt->penetrated ? "the penetration has begun" : "before the penetration", LipSync::SpokenLine(a) ? ", but a line is still on her: skipped" : "");
						if (!LipSync::SpokenLine(a)) {
							if (auto* tasks = SKSE::GetTaskInterface()) {
								tasks->AddTask([h = a->GetHandle()]() {
									if (const auto p = h.get()) Openers::Say(p.get());
								});
							}
						}
					}
				}
			}

			// The scene that ends it: watch the penetration.
			if (rt->startedVirgin && !rt->penetrated && t.meta && s.pos >= 0 && OStimData::FindAnyActionForTarget(*t.meta, s.pos, VaginalSex()) >= 0) {
				rt->penetrated = true;
				rt->orgasmsAtPenetration = s.orgasms;
				rt->rough = t.toneRough;
				Debug::Log(Debug::Cat::Pers, "{}: first times: the penetration begins ({} orgasm(s) so far, {})", Debug::Label(a), s.orgasms - rt->orgasmsBase, t.toneRough ? "a rough scene" : "not rough");
			}
			if (rt->penetrated && s.orgasms == rt->orgasmsBase && t.speed > V::iSpeedLimit && !rt->fast) {
				rt->fast = true;
				Debug::Log(Debug::Cat::Pers, "{}: first times: the scene went to speed {} before her first orgasm", Debug::Label(a), t.speed);
			}
		}
	}

	void OnOrgasm(Scenes::Thread& t, Scenes::Slot& s)
	{
		if (!V::bEnabled || !s.female) return;
		auto* a = s.Get();
		if (!a || a->IsPlayerRef()) return;
		Record rec;
		{
			std::scoped_lock l(g_lock);
			const auto it = g_records.find(s.id);
			if (it == g_records.end()) return;
			rec = it->second;
		}
#if !OSIS_LITE
		if (rec.pendingCrazed) {
			// Crazed about the first partner who gives her an orgasm: the one doing something to her, or the other actor there.
			RE::FormID partner = 0;
			for (const auto& other : t.slots) {
				if (&other == &s || !other.Get()) continue;
				if (t.meta && other.pos >= 0 && s.pos >= 0 && OStimData::FindAnyActionForTarget(*t.meta, s.pos, OStimData::SplitCSV("vaginalsex,analsex,blowjob,deepthroat,handjob,cunnilingus,vulvaleating,fingering")) >= 0) {
					partner = other.id;
					break;
				}
				if (partner == 0) partner = other.id;
			}
			if (partner != 0) {
				Experience::BecomeObsessed(s.id, partner);
				{
					std::scoped_lock l(g_lock);
					Rec(s.id).pendingCrazed = 0;
				}
				logger::info("First times: {} ({:08X}) is crazed about {:08X}: the first partner to give her an orgasm after a non-consensual first time", a->GetDisplayFullName(), s.id, partner);
			}
		}
		if (rec.broken) {
			// An "orgasm" of a broken woman: nothing shows, her excitement goes back to zero, and what builds it is a little faster.
			std::scoped_lock l(g_lock);
			auto& r = Rec(s.id);
			r.brokenRate += kBrokenStep;
			++g_rt[s.id].cycles;
			Papyrus::ModifyExcitement(a, -100.0f);
		}
#else
		(void)t;
#endif
	}

	void OnSceneEnd(Scenes::Thread& t)
	{
		if (!V::bEnabled && !V::bMaleVirginRate) return;
		for (auto& s : t.slots) {
			auto* a = s.Get();
			if (!a) continue;
			if (!s.female || !V::bEnabled) {  // a man (his rate): what OVirginity says of him is read again next scene, since it ends his virginity too when he takes someone's
				std::scoped_lock l(g_lock);
				g_answers.erase(s.id);
				g_calculating.erase(s.id);
				g_pending.erase(s.id);
				continue;
			}
			Rt rt;
			{
				std::scoped_lock l(g_lock);
				const auto it = g_rt.find(s.id);
				if (it == g_rt.end()) continue;
				rt = it->second;
				g_rt.erase(it);
				g_answers.erase(s.id);  // asked again next scene: a first time ends it
				g_calculating.erase(s.id);
				g_pending.erase(s.id);
			}
			if (a->IsPlayerRef()) continue;
			if (rt.startedVirgin) Resolve(t, s, a, rt);
#if !OSIS_LITE
			Record rec;
			{
				std::scoped_lock l(g_lock);
				rec = Rec(s.id);
			}
			if (rec.broken) {
				Debug::Log(Debug::Cat::Pers, "{}: broken: {} silent orgasm(s) this session{}", Debug::Label(a), rt.cycles, rt.negative ? ", with something rough or not consensual in it" : "");
				if (rt.cycles >= kBrokenCycles && !rt.negative) {
					{
						std::scoped_lock l(g_lock);
						auto& r = Rec(s.id);
						r.broken = 0;
						r.brokenRate = 0.0f;
					}
					Face::Engine::SetAutomaticPersonality(a, P::kStoic, "broken: ten silent orgasms in a session");
					logger::info("First times: {} ({:08X}) is no longer shut down: ten silent orgasms in a session, nothing rough in it", a->GetDisplayFullName(), s.id);
				}
			}
#endif
		}
	}

	int VirginAnswer(RE::Actor* a_actor)
	{
		if (!a_actor) return 0;
		EnsureLoaded();
		if (!g_present) return 0;
		const RE::FormID id = a_actor->GetFormID();
		int raw = -2;
		bool ask = false;
		{
			std::scoped_lock l(g_lock);
			if (const auto it = g_answers.find(id); it != g_answers.end()) raw = it->second;
			if (raw == -2) ask = g_pending.insert(id).second;
		}
		if (raw == 0 || raw == 1) return raw;
		if (ask) {
			Ask(a_actor);
		} else if (raw == -1) {
			Settle(a_actor);  // OVirginity has not worked it out: the config's lists decide at once, or it is asked to roll it
			std::scoped_lock l(g_lock);
			if (const auto it = g_answers.find(id); it != g_answers.end() && (it->second == 0 || it->second == 1)) return it->second;
		}
		return -1;
	}

	bool IsBroken(RE::Actor* a_actor)
	{
#if !OSIS_LITE
		if (!a_actor) return false;
		std::scoped_lock l(g_lock);
		const auto it = g_records.find(a_actor->GetFormID());
		return it != g_records.end() && it->second.broken != 0;
#else
		(void)a_actor;
		return false;
#endif
	}

	float BrokenRate(RE::Actor* a_actor)
	{
		if (!a_actor) return kBrokenBase;
		std::scoped_lock l(g_lock);
		const auto it = g_records.find(a_actor->GetFormID());
		return it != g_records.end() && it->second.brokenRate > 0.0f ? it->second.brokenRate : kBrokenBase;
	}

	std::string Describe(RE::Actor* a_actor)
	{
		if (!a_actor) return "none";
		EnsureLoaded();
		if (!V::bEnabled) return "first times are off";
		if (!g_present) return "OVirginity Reflowered is not installed";
		Record rec;
		Rt rt;
		bool have = false;
		{
			std::scoped_lock l(g_lock);
			if (const auto it = g_records.find(a_actor->GetFormID()); it != g_records.end()) rec = it->second;
			if (const auto it = g_rt.find(a_actor->GetFormID()); it != g_rt.end()) {
				rt = it->second;
				have = true;
			}
		}
		const char* state = rec.state == 0 ? "no first scene yet" : (rec.state == 1 ? "first scene done, first time still to come" : "first time over");
#if !OSIS_LITE
		const std::string shut = rec.broken ? std::format(" | BROKEN, excitement rate x{:.2f}", rec.brokenRate) : (rec.pendingCrazed ? std::string(" | will be crazed about the first partner to give her an orgasm") : std::string());
#else
		const std::string shut;
#endif
		return std::format("OVirginity says {} | {} | {}{}{}{}", have ? (rt.virgin == 1 ? "virgin" : (rt.virgin == 0 ? "not a virgin" : "nothing yet")) : "(not in a scene)", state, FaithName(FaithOf(a_actor)),
			rec.original >= 0 ? std::format(" | her first scene gave her {}", Face::Engine::PersonalityName(rec.original)) : std::string(),
			shut,
			have && rt.penetrated ? std::format(" | penetration began ({} orgasm(s) before, rough {}, too fast {})", rt.orgasmsAtPenetration - rt.orgasmsBase, rt.rough, rt.fast) : std::string());
	}

	std::string Status()
	{
		EnsureLoaded();
		return std::format("OVirginity Reflowered {}; {} first scene(s) given a personality and {} first time(s) this session", g_present ? "found" : "not found", g_firstTimes.load(), g_deflowered.load());
	}

	std::unordered_map<RE::FormID, Record> Snapshot()
	{
		std::scoped_lock l(g_lock);
		return g_records;
	}

	void Restore(std::unordered_map<RE::FormID, Record> a_records)
	{
		std::scoped_lock l(g_lock);
		g_records = std::move(a_records);
	}

	void Clear()
	{
		std::scoped_lock l(g_lock);
		g_records.clear();
		g_rt.clear();
		g_answers.clear();
		g_calculating.clear();
		g_pending.clear();
		g_askedAt.clear();
	}
}
