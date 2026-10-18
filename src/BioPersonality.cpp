// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "BioPersonality.h"

#if OSIS_BIO_TABLES

#include "Face/Engine.h"

namespace BioPersonality
{
	namespace
	{
		constexpr auto kShipped = "Data/SKSE/Plugins/OSIS/npc_personalities.txt";
		constexpr auto kUser = "Data/SKSE/Plugins/OSIS/npc_personalities_user.txt";

		struct Entry
		{
			int pers;
			std::string suffix;  // the last three hex digits of the base form id, upper case; empty: the name is enough
		};
		using Table = std::unordered_multimap<std::string, Entry>;

		Table g_shipped;
		Table g_user;
		std::once_flag g_once;

		// Lower case, anything but a letter or a digit as one space: "Eryniel's Niece" and "eryniel s niece" are the same name.
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

		int PersonalityId(std::string_view a_name)
		{
			using namespace Face::Engine;
			if (a_name == "balanced") return Pers::kBalanced;
			if (a_name == "stoic") return Pers::kStoic;
			if (a_name == "vocal") return Pers::kVocal;
			if (a_name == "shy") return Pers::kShy;
			if (a_name == "dominant") return Pers::kDominant;
			if (a_name == "timid") return Pers::kTimid;
			if (a_name == "submissive") return Pers::kSubmissive;
			if (a_name == "wild") return Pers::kWild;
			if (a_name == "crazed") return Pers::kCrazed;
			return -1;
		}

		std::size_t Read(const char* a_path, Table& a_into)
		{
			std::ifstream f(a_path);
			if (!f) return 0;
			std::string line;
			std::size_t n = 0;
			while (std::getline(f, line)) {
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (line.empty() || line[0] == ';' || line[0] == '#') continue;
				const auto bar1 = line.find('|');
				if (bar1 == std::string::npos) continue;
				const auto bar2 = line.find('|', bar1 + 1);
				const std::string name = Normalise(std::string_view(line).substr(0, bar1));
				std::string pers = line.substr(bar1 + 1, bar2 == std::string::npos ? std::string::npos : bar2 - bar1 - 1);
				std::string suffix = bar2 == std::string::npos ? std::string() : line.substr(bar2 + 1);
				const auto trim = [](std::string& s) {
					while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
					while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
				};
				trim(pers);
				trim(suffix);
				std::ranges::transform(pers, pers.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				std::ranges::transform(suffix, suffix.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
				const int id = PersonalityId(pers);
				if (name.empty() || id < 0) continue;
				a_into.emplace(name, Entry{ id, suffix });
				++n;
			}
			return n;
		}

		void EnsureLoaded()
		{
			std::call_once(g_once, [] {
				const auto shipped = Read(kShipped, g_shipped);
				const auto user = Read(kUser, g_user);
				logger::info("NPC personalities by name: {} from {}, {} of your own from {}", shipped, kShipped, user, kUser);
			});
		}

		// The entry for this NPC in one table: where several share a name, the one whose suffix is the actor's base form id (or, failing that, its
		// reference's), else one with no suffix (a general entry for the name), else the first.
		const Entry* Find(const Table& a_table, const std::string& a_name, RE::Actor* a_actor)
		{
			const auto [first, last] = a_table.equal_range(a_name);
			if (first == last) return nullptr;
			const auto* base = a_actor->GetActorBase();
			const std::string baseSuffix = std::format("{:03X}", (base ? base->GetFormID() : 0u) & 0xFFFu);
			const std::string refSuffix = std::format("{:03X}", a_actor->GetFormID() & 0xFFFu);
			const Entry* byBase = nullptr;
			const Entry* byRef = nullptr;
			const Entry* general = nullptr;
			const Entry* any = nullptr;
			for (auto it = first; it != last; ++it) {
				const auto& e = it->second;
				if (!any) any = &e;
				if (e.suffix.empty()) {
					if (!general) general = &e;
				} else if (e.suffix == baseSuffix) {
					byBase = &e;
				} else if (e.suffix == refSuffix) {
					byRef = &e;
				}
			}
			if (byBase) return byBase;
			if (byRef) return byRef;
			if (general) return general;
			// Several NPCs of this name, none of them this one by form id: better no entry than someone else's.
			return std::next(first) == last ? any : nullptr;
		}
	}

	int Lookup(RE::Actor* a_actor)
	{
		if (!a_actor || a_actor->IsPlayerRef()) return -1;
		EnsureLoaded();
		const std::string name = Normalise(a_actor->GetDisplayFullName());
		if (name.empty()) return -1;
		if (const auto* e = Find(g_user, name, a_actor)) return e->pers;
		if (const auto* e = Find(g_shipped, name, a_actor)) return e->pers;
		return -1;
	}

	std::string Describe(RE::Actor* a_actor)
	{
		if (!a_actor) return "none";
		const int id = Lookup(a_actor);
		return id >= 0 ? std::format("'{}' -> {}", Normalise(a_actor->GetDisplayFullName()), Face::Engine::PersonalityName(id)) : std::format("'{}' is not in the tables", Normalise(a_actor->GetDisplayFullName()));
	}

	std::string Status()
	{
		EnsureLoaded();
		return std::format("{} NPC(s) by name, {} of your own", g_shipped.size(), g_user.size());
	}
}

#endif  // OSIS_BIO_TABLES
