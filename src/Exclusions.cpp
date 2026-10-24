// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Exclusions.h"

#include "Settings.h"

namespace Exclusions
{
	namespace
	{
		constexpr auto kFile = "Data/SKSE/Plugins/OSIS/excluded_actors.txt";
		constexpr auto kHeader =
			"# Actors OSIS leaves alone: nothing of OSIS's is done to them (no face, lip-sync, personality, moan rules, body or blush effects).\n"
			"# One per line. The menu (General > Actors OSIS leaves alone) adds and removes lines; you can edit this by hand too:\n"
			"#   Delphine                        an NPC by display name (any capitals)\n"
			"#   Delphine|Skyrim.esm|013478      a label, the plugin and the actor's FormID in it as xEdit shows it, without the load-order digits\n"
			"#   Skyrim.esm|013478               the same without a label\n"
			"# An NPC is matched by the actor itself or by the record it is made from. Lines starting with # or ; are comments.\n";

		struct Entry
		{
			std::string line;        // as written, so a removal takes out exactly this line
			RE::FormID id = 0;       // a plugin|FormID line: the full FormID in this load order, 0 when the plugin is not there
			bool isId = false;
			std::string name;        // a name line: the lowercased name
		};

		std::mutex g_lock;
		std::vector<std::string> g_lines;                // the file as read, comments and blanks included, so a rewrite changes nothing else
		std::vector<Entry> g_entries;
		std::unordered_set<RE::FormID> g_ids;
		std::unordered_set<std::string> g_names;
		struct Verdict
		{
			bool excluded = false;
			std::string why;
		};
		std::unordered_map<RE::FormID, Verdict> g_cache;
		unsigned g_cacheFlags = ~0u;                     // the voice-type settings the cache was made under

		std::string Lower(std::string s)
		{
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		std::string Trim(std::string_view s)
		{
			const auto b = s.find_first_not_of(" \t\r\n");
			if (b == std::string_view::npos) return {};
			const auto e = s.find_last_not_of(" \t\r\n");
			return std::string(s.substr(b, e - b + 1));
		}

		bool LooksLikePlugin(const std::string& s)
		{
			const auto l = Lower(s);
			return l.ends_with(".esp") || l.ends_with(".esm") || l.ends_with(".esl");
		}

		// The base game's files, Creation Club included: a voice type from one of these is not "another mod's".
		bool Official(std::string_view a_file)
		{
			const auto f = Lower(std::string(a_file));
			if (f == "skyrim.esm" || f == "update.esm" || f == "dawnguard.esm" || f == "hearthfires.esm" || f == "dragonborn.esm") return true;
			return f.starts_with("cc") && (f.ends_with(".esm") || f.ends_with(".esl"));
		}

		// Parse one line into an entry; false for a comment or a blank.
		bool Parse(const std::string& a_line, Entry& a_out)
		{
			const std::string s = Trim(a_line);
			if (s.empty() || s[0] == '#' || s[0] == ';') return false;
			std::vector<std::string> parts;
			for (std::size_t from = 0;;) {
				const auto bar = s.find('|', from);
				parts.push_back(Trim(s.substr(from, bar == std::string::npos ? std::string::npos : bar - from)));
				if (bar == std::string::npos) break;
				from = bar + 1;
			}
			std::string file, hex;
			if (parts.size() >= 3) {
				file = parts[1];
				hex = parts[2];
			} else if (parts.size() == 2 && LooksLikePlugin(parts[0])) {
				file = parts[0];
				hex = parts[1];
			}
			a_out = {};
			a_out.line = a_line;
			if (!file.empty() && !hex.empty()) {
				a_out.isId = true;
				try {
					const auto local = static_cast<RE::FormID>(std::stoul(hex, nullptr, 16));
					if (auto* dh = RE::TESDataHandler::GetSingleton()) a_out.id = dh->LookupFormID(local, file);
				} catch (...) {
					a_out.id = 0;
				}
				return true;
			}
			if (parts[0].empty()) return false;
			a_out.name = Lower(parts[0]);
			return true;
		}

		void RebuildSets()  // g_lock held
		{
			g_ids.clear();
			g_names.clear();
			for (const auto& e : g_entries) {
				if (e.isId) {
					if (e.id) g_ids.insert(e.id);
				} else {
					g_names.insert(e.name);
				}
			}
			g_cache.clear();
		}

		bool Matches(const Entry& e, RE::Actor* a)
		{
			if (e.isId) {
				if (!e.id) return false;
				if (a->GetFormID() == e.id) return true;
				auto* base = a->GetActorBase();
				return base && base->GetFormID() == e.id;
			}
			const char* name = a->GetDisplayFullName();
			return name && *name && Lower(name) == e.name;
		}

		bool WriteFile()  // g_lock held
		{
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(kFile).parent_path(), ec);
			std::ofstream f(kFile, std::ios::trunc);
			if (!f) {
				logger::warn("Exclusions: could not write {}", kFile);
				return false;
			}
			for (const auto& l : g_lines) f << l << '\n';
			return true;
		}

		bool ContainsNoCase(std::string_view a_text, std::string_view a_word)
		{
			return Lower(std::string(a_text)).find(Lower(std::string(a_word))) != std::string::npos;
		}

		Verdict Evaluate(RE::Actor* a, bool a_unique, bool a_mod)  // g_lock held
		{
			for (const auto& e : g_entries) {
				if (Matches(e, a)) return { true, e.isId ? "it is on your list" : "it is on your list, by name" };
			}
			if (a_unique || a_mod) {
				auto* base = a->GetActorBase();
				auto* vt = base ? base->voiceType : nullptr;
				if (vt) {
					if (a_unique) {
						const char* id = vt->GetFormEditorID();
						if (id && *id && ContainsNoCase(id, "unique")) return { true, std::format("its voice type is {}, a unique voice", id) };
					}
					if (a_mod) {
						if (auto* file = vt->GetFile(0); file && !Official(file->GetFilename())) {
							const char* id = vt->GetFormEditorID();
							return { true, std::format("its voice type{}{} comes from {}", id && *id ? " " : "", id && *id ? id : "", file->GetFilename()) };
						}
					}
				}
			}
			return {};
		}

		Verdict Check(RE::Actor* a)
		{
			bool unique, mod;
			{
				std::scoped_lock l(Settings::lock);
				unique = Settings::Exclude::bSkipUniqueVoice;
				mod = Settings::Exclude::bSkipModVoice;
			}
			const unsigned flags = (unique ? 1u : 0u) | (mod ? 2u : 0u);
			const RE::FormID id = a->GetFormID();
			std::scoped_lock l(g_lock);
			if (g_cacheFlags != flags) {
				g_cache.clear();
				g_cacheFlags = flags;
			} else if (const auto it = g_cache.find(id); it != g_cache.end()) {
				return it->second;
			}
			Verdict v = Evaluate(a, unique, mod);
			if (v.excluded) logger::info("Exclusions: {} ({:08X}) is left alone: {}", a->GetDisplayFullName(), id, v.why);
			if (g_cache.size() > 4000) g_cache.clear();
			g_cache[id] = v;
			return v;
		}
	}

	const char* FilePath() { return kFile; }

	void Load()
	{
		std::scoped_lock l(g_lock);
		g_lines.clear();
		g_entries.clear();
		if (std::ifstream in(kFile); in) {
			for (std::string line; std::getline(in, line);) {
				if (!line.empty() && line.back() == '\r') line.pop_back();
				g_lines.push_back(line);
			}
		} else {
			for (std::size_t from = 0; from < std::char_traits<char>::length(kHeader);) {
				const auto nl = std::string_view(kHeader).find('\n', from);
				g_lines.emplace_back(std::string_view(kHeader).substr(from, nl - from));
				from = nl + 1;
			}
			WriteFile();
		}
		std::size_t unresolved = 0;
		for (const auto& line : g_lines) {
			Entry e;
			if (!Parse(line, e)) continue;
			if (e.isId && !e.id) {
				++unresolved;
				logger::warn("Exclusions: '{}' names a plugin that is not in the load order, or a record it does not have; the line does nothing", Trim(line));
			}
			g_entries.push_back(std::move(e));
		}
		RebuildSets();
		logger::info("Exclusions: {} actor(s) on the list in {} ({} by name, {} by record, {} not found)", g_entries.size(), kFile,
			g_names.size(), g_ids.size(), unresolved);
	}

	bool IsExcluded(RE::Actor* a_actor)
	{
		if (!a_actor || a_actor->IsPlayerRef()) return false;
		return Check(a_actor).excluded;
	}

	std::string Why(RE::Actor* a_actor)
	{
		if (!a_actor || a_actor->IsPlayerRef()) return {};
		const auto v = Check(a_actor);
		return v.excluded ? v.why : std::string();
	}

	bool Add(RE::Actor* a_actor)
	{
		if (!a_actor || a_actor->IsPlayerRef()) return false;
		const char* shown = a_actor->GetDisplayFullName();
		std::string name = shown ? shown : "";
		std::ranges::replace(name, '|', ' ');
		std::scoped_lock l(g_lock);
		for (const auto& e : g_entries) {
			if (Matches(e, a_actor)) return false;
		}
		Entry e;
		auto* base = a_actor->GetActorBase();
		if (auto* file = base ? base->GetFile(0) : nullptr; file && base->GetFormID() < 0xFF000000u) {
			e.isId = true;
			e.id = base->GetFormID();
			e.line = std::format("{}|{}|{:06X}", name.empty() ? "actor" : name, file->GetFilename(), base->GetLocalFormID());
		} else if (!name.empty()) {
			e.name = Lower(name);
			e.line = name;
		} else {
			return false;
		}
		g_lines.push_back(e.line);
		g_entries.push_back(e);
		RebuildSets();
		WriteFile();
		logger::info("Exclusions: {} ({:08X}) added to the list: {}", name, a_actor->GetFormID(), e.line);
		return true;
	}

	int Remove(RE::Actor* a_actor)
	{
		if (!a_actor) return 0;
		std::scoped_lock l(g_lock);
		int removed = 0;
		for (std::size_t i = g_entries.size(); i-- > 0;) {
			if (!Matches(g_entries[i], a_actor)) continue;
			if (const auto it = std::ranges::find(g_lines, g_entries[i].line); it != g_lines.end()) g_lines.erase(it);
			logger::info("Exclusions: {} ({:08X}) taken off the list: {}", a_actor->GetDisplayFullName(), a_actor->GetFormID(), Trim(g_entries[i].line));
			g_entries.erase(g_entries.begin() + static_cast<std::ptrdiff_t>(i));
			++removed;
		}
		if (removed) {
			RebuildSets();
			WriteFile();
		}
		return removed;
	}

	std::vector<ListEntry> List()
	{
		std::scoped_lock l(g_lock);
		std::vector<ListEntry> out;
		for (const auto& e : g_entries) out.push_back({ Trim(e.line), !e.isId || e.id != 0 });
		return out;
	}

	bool RemoveAt(std::size_t a_index)
	{
		std::scoped_lock l(g_lock);
		if (a_index >= g_entries.size()) return false;
		if (const auto it = std::ranges::find(g_lines, g_entries[a_index].line); it != g_lines.end()) g_lines.erase(it);
		logger::info("Exclusions: taken off the list: {}", Trim(g_entries[a_index].line));
		g_entries.erase(g_entries.begin() + static_cast<std::ptrdiff_t>(a_index));
		RebuildSets();
		WriteFile();
		return true;
	}

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		std::size_t missing = 0;
		for (const auto& e : g_entries) missing += (e.isId && !e.id) ? 1 : 0;
		return std::format("{} on the list{}", g_entries.size(), missing ? std::format(" ({} not found in your load order)", missing) : std::string());
	}
}
