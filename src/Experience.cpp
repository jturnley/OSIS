// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Experience.h"

#include "Face/Engine.h"
#include "Settings.h"

namespace Experience
{
	namespace
	{
		namespace P = Face::Engine::Pers;
		namespace E = Settings::Evolution;

		std::mutex g_lock;
		std::unordered_map<RE::FormID, Record> g_records;
		std::unordered_set<RE::FormID> g_now;       // actors in a scene with the partner they are obsessed with, this moment
		constexpr std::size_t kMax = 4096;          // a stray run of generated NPCs cannot bloat the cosave
		constexpr std::size_t kMaxPartners = 8;     // partners counted per actor
		std::atomic<std::uint32_t> g_grown{ 0 };    // how many have grown or become obsessed this run

		std::string Plural(int a_n, const char* a_one, const char* a_many) { return std::format("{} {}", a_n, a_n == 1 ? a_one : a_many); }

		std::string NameOf(RE::FormID a_id)
		{
			if (auto* a = RE::TESForm::LookupByID<RE::Actor>(a_id)) return std::format("{} ({:08X})", a->GetDisplayFullName(), a_id);
			return std::format("{:08X}", a_id);
		}
	}

	Rule RuleFor(int a_pers)
	{
		switch (a_pers) {
		case P::kStoic: return { E::iStoicSessions, E::iStoicOrgasms, P::kBalanced };
		case P::kShy: return { E::iShySessions, E::iShyOrgasms, P::kTimid };
		case P::kTimid: return { E::iTimidSessions, E::iTimidOrgasms, P::kBalanced };
		case P::kVocal: return { E::iVocalSessions, E::iVocalOrgasms, P::kWild };
		default: return {};
		}
	}

	Rule ObsessionRule()
	{
		return { E::iCrazedSessions, E::iCrazedOrgasms, P::kCrazed };
	}

	Result OnSessionEnd(RE::FormID a_id, int a_pers, int a_orgasms, const std::vector<RE::FormID>& a_partners)
	{
		Result out;
		if (!E::bEnabled) return out;
		std::scoped_lock l(g_lock);
		auto it = g_records.find(a_id);
		if (it == g_records.end()) {
			if (g_records.size() >= kMax) return out;
			it = g_records.emplace(a_id, Record{}).first;
		}
		auto& r = it->second;
		if (r.pers != a_pers) {  // a different personality (set by hand, or worked out again): what it was is not what it is
			r.pers = a_pers;
			r.qualified = 0;
		}
		++r.sessions;
		r.orgasms += static_cast<std::uint32_t>(std::max(0, a_orgasms));

		// Obsession: sessions of this many orgasms count toward each partner who was there; the first to reach the number is the one. Not for someone who already is.
		const auto obsession = ObsessionRule();
		if (r.obsessedWith == 0 && a_orgasms >= obsession.orgasms) {
			for (const auto partner : a_partners) {
				if (partner == 0 || partner == a_id) continue;
				auto pit = std::ranges::find(r.partners, partner, &std::pair<RE::FormID, std::uint32_t>::first);
				if (pit == r.partners.end()) {
					if (r.partners.size() >= kMaxPartners) {  // make room: forget the partner they have the fewest sessions with
						r.partners.erase(std::ranges::min_element(r.partners, {}, &std::pair<RE::FormID, std::uint32_t>::second));
					}
					r.partners.emplace_back(partner, 0u);
					pit = std::prev(r.partners.end());
				}
				++pit->second;
				if (out.obsessedWith == 0 && pit->second >= static_cast<std::uint32_t>(obsession.sessions)) out.obsessedWith = partner;
			}
			if (out.obsessedWith != 0) {
				r.obsessedWith = out.obsessedWith;
				r.partners.clear();
				out.why = std::format("{} sessions of {} or more orgasms with {}", obsession.sessions, obsession.orgasms, NameOf(out.obsessedWith));
				g_grown.fetch_add(1, std::memory_order_relaxed);
			}
		}

		if (const auto rule = RuleFor(a_pers); rule.target >= 0) {
			if (a_orgasms >= rule.orgasms) ++r.qualified;
			if (r.qualified >= static_cast<std::uint32_t>(rule.sessions)) {
				out.next = rule.target;
				if (!out.why.empty()) out.why += "; ";
				out.why += std::format("{} sessions of {} or more orgasms as {}", rule.sessions, rule.orgasms, Face::Engine::PersonalityName(a_pers));
				r.pers = rule.target;
				r.qualified = 0;
				g_grown.fetch_add(1, std::memory_order_relaxed);
			}
		}
		return out;
	}

	void BecomeObsessed(RE::FormID a_id, RE::FormID a_partner)
	{
		if (a_id == 0 || a_partner == 0 || a_id == a_partner) return;
		std::scoped_lock l(g_lock);
		auto it = g_records.find(a_id);
		if (it == g_records.end()) {
			if (g_records.size() >= kMax) return;
			it = g_records.emplace(a_id, Record{}).first;
		}
		it->second.obsessedWith = a_partner;
		it->second.partners.clear();
		g_grown.fetch_add(1, std::memory_order_relaxed);
	}

	RE::FormID ObsessedWith(RE::FormID a_id)
	{
		std::scoped_lock l(g_lock);
		const auto it = g_records.find(a_id);
		return it != g_records.end() ? it->second.obsessedWith : 0u;
	}

	void SetObsessedNow(RE::FormID a_id, bool a_now)
	{
		std::scoped_lock l(g_lock);
		if (a_now) g_now.insert(a_id);
		else g_now.erase(a_id);
	}

	bool IsObsessedNow(RE::FormID a_id)
	{
		std::scoped_lock l(g_lock);
		return g_now.contains(a_id);
	}

	std::string Describe(RE::FormID a_id, int a_pers, bool a_lockHeld)
	{
		Record r;
		bool now = false;
		{
			std::scoped_lock l(g_lock);
			if (const auto it = g_records.find(a_id); it != g_records.end()) r = it->second;
			now = g_now.contains(a_id);
		}
		Rule own, obsession;
		bool on;
		if (a_lockHeld) {
			own = RuleFor(a_pers);
			obsession = ObsessionRule();
			on = E::bEnabled;
		} else {
			std::scoped_lock sl(Settings::lock);
			own = RuleFor(a_pers);
			obsession = ObsessionRule();
			on = E::bEnabled;
		}
		if (!on) return "personalities do not grow (off)";
		const bool mine = r.pers == a_pers;
		std::string text = std::format("{} so far, {} orgasms", Plural(static_cast<int>(r.sessions), "session", "sessions"), r.orgasms);
		if (own.target >= 0) {
			text += std::format(" | {} -> {}: {} of {} sessions with {}+ orgasms", Face::Engine::PersonalityName(a_pers), Face::Engine::PersonalityName(own.target), mine ? r.qualified : 0u, own.sessions, own.orgasms);
		} else {
			text += std::format(" | {} does not grow into anything else", Face::Engine::PersonalityName(a_pers));
		}
		if (r.obsessedWith != 0) {
			text += std::format(" | obsessed with {}{}", NameOf(r.obsessedWith), now ? " (crazed now: they are together)" : "");
		} else if (r.partners.empty()) {
			text += std::format(" | obsession: nobody yet (needs {} sessions of {}+ orgasms with one partner)", obsession.sessions, obsession.orgasms);
		} else {
			std::string who;
			for (const auto& [id, n] : r.partners) who += std::format("{}{} {}/{}", who.empty() ? "" : ", ", NameOf(id), n, obsession.sessions);
			text += std::format(" | obsession so far: {}", who);
		}
		return text;
	}

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		std::size_t obsessed = 0;
		for (const auto& [id, r] : g_records) obsessed += r.obsessedWith != 0 ? 1 : 0;
		return std::format("{} actor(s) tracked, {} obsessed with a partner, {} changes this session", g_records.size(), obsessed, g_grown.load(std::memory_order_relaxed));
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
		g_now.clear();
	}

	void Clear()
	{
		std::scoped_lock l(g_lock);
		g_records.clear();
		g_now.clear();
	}
}
