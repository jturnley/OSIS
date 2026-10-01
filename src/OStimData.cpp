// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "OStimData.h"

#include "FsUtil.h"

namespace OStimData
{
	namespace
	{
		constexpr auto kSceneRoot = "Data/SKSE/Plugins/OStim/scenes";
		constexpr auto kActionRoot = "Data/SKSE/Plugins/OStim/actions";

		std::mutex g_lock;
		std::unordered_map<std::string, std::filesystem::path> g_sceneFiles;  // lowercase id -> file
		std::unordered_map<std::string, ScenePtr> g_scenes;                   // parsed cache
		std::unordered_map<std::string, TagList> g_actionTags;                // canonical type -> tags
		std::unordered_map<std::string, std::string> g_actionAlias;           // alias -> canonical type

		std::string Lower(std::string_view s)
		{
			std::string out(s);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		TagList ReadTags(const json& j)
		{
			TagList out;
			if (!j.is_array()) return out;
			for (const auto& t : j) {
				if (t.is_string()) out.push_back(Lower(t.get<std::string>()));
			}
			return out;
		}

		void IndexScenes()
		{
			std::error_code ec;
			if (!std::filesystem::exists(kSceneRoot, ec)) {
				logger::warn("No OStim scene folder at {}", kSceneRoot);
				return;
			}
			std::size_t unmappable = 0;
			const auto stats = FsUtil::WalkFiles(kSceneRoot, [&](const std::filesystem::path& p) {
				if (FsUtil::LowerExt(p) != L".json") return;
				// Scene ids are ASCII in practice; a file whose name the code page can't hold can't be one.
				const auto stem = FsUtil::Narrow(p.stem());
				if (!stem) {
					++unmappable;
					return;
				}
				g_sceneFiles.emplace(Lower(*stem), p);
			});
			if (stats.dirErrors || unmappable) {
				logger::warn("OStim scenes: skipped {} unreadable folder(s) and {} file(s) with unmappable names", stats.dirErrors, unmappable);
			}
		}

		void LoadActions()
		{
			std::error_code ec;
			if (!std::filesystem::exists(kActionRoot, ec)) return;
			const auto stats = FsUtil::WalkFiles(kActionRoot, [&](const std::filesystem::path& path) {
				if (FsUtil::LowerExt(path) != L".json") return;
				const auto stem = FsUtil::Narrow(path.stem());
				if (!stem) return;
				const std::string type = Lower(*stem);
				try {
					std::ifstream f(path);
					const auto doc = json::parse(f, nullptr, true, true);
					g_actionTags[type] = ReadTags(doc.value("tags", json::array()));
					for (const auto& alias : doc.value("aliases", json::array())) {
						if (alias.is_string()) g_actionAlias[Lower(alias.get<std::string>())] = type;
					}
				} catch (const std::exception& e) {
					logger::warn("OStim action {}: {}", FsUtil::Printable(path), e.what());
				}
			});
			if (stats.dirErrors) logger::warn("OStim actions: skipped {} unreadable folder(s)", stats.dirErrors);
		}

		std::string Canonical(std::string type)
		{
			if (auto it = g_actionAlias.find(type); it != g_actionAlias.end()) return it->second;
			return type;
		}

		int ReadIndex(const json& j, const char* key)
		{
			if (!j.contains(key)) return -1;
			const auto& v = j.at(key);
			if (v.is_number_integer()) return v.get<int>();
			if (v.is_number()) return static_cast<int>(v.get<double>());
			return -1;
		}

		ScenePtr Parse(const std::string& id, const std::filesystem::path& file)
		{
			auto s = std::make_shared<Scene>();
			s->id = id;
			try {
				std::ifstream f(file);
				const auto doc = json::parse(f, nullptr, true, true);
				s->tags = ReadTags(doc.value("tags", json::array()));
				for (const auto& a : doc.value("actors", json::array())) {
					SceneActor sa;
					sa.tags = ReadTags(a.value("tags", json::array()));
					sa.intendedSex = Lower(a.value("intendedSex", std::string{}));
					s->actors.push_back(std::move(sa));
				}
				for (const auto& a : doc.value("actions", json::array())) {
					Action act;
					act.type = Canonical(Lower(a.value("type", std::string{})));
					act.actor = ReadIndex(a, "actor");
					act.target = ReadIndex(a, "target");
					act.performer = ReadIndex(a, "performer");
					if (act.performer < 0) act.performer = act.actor;  // OStim's default
					if (act.target < 0) act.target = act.actor;
					s->actions.push_back(std::move(act));
				}
				const auto speeds = doc.value("speeds", json::array());
				s->maxSpeed = speeds.is_array() && !speeds.empty() ? static_cast<int>(speeds.size()) - 1 : 0;
				s->defaultSpeed = doc.value("defaultSpeed", 0);
				if (const auto d = doc.find("destination"); d != doc.end() && d->is_string()) s->destination = Lower(d->get<std::string>());
				s->known = true;
			} catch (const std::exception& e) {
				logger::warn("OStim scene {}: {}", FsUtil::Printable(file), e.what());
			}
			return s;
		}
	}

	void Init()
	{
		std::scoped_lock l(g_lock);
		const auto t0 = std::chrono::steady_clock::now();
		g_sceneFiles.clear();
		g_scenes.clear();
		g_actionTags.clear();
		g_actionAlias.clear();
		LoadActions();
		IndexScenes();
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
		logger::info("OStim metadata: {} scene files, {} action types indexed in {} ms", g_sceneFiles.size(), g_actionTags.size(), ms);
	}

	std::size_t SceneCount()
	{
		std::scoped_lock l(g_lock);
		return g_sceneFiles.size();
	}

	std::size_t ActionCount()
	{
		std::scoped_lock l(g_lock);
		return g_actionTags.size();
	}

	ScenePtr GetScene(std::string_view a_id)
	{
		const std::string id = Lower(a_id);
		std::scoped_lock l(g_lock);
		if (auto it = g_scenes.find(id); it != g_scenes.end()) return it->second;
		ScenePtr s;
		if (auto f = g_sceneFiles.find(id); f != g_sceneFiles.end()) {
			s = Parse(id, f->second);
		} else {
			auto blank = std::make_shared<Scene>();
			blank->id = id;
			s = blank;
		}
		g_scenes.emplace(id, s);
		return s;
	}

	TagList SplitCSV(std::string_view a_csv)
	{
		TagList out;
		std::size_t start = 0;
		while (start <= a_csv.size()) {
			const auto comma = a_csv.find(',', start);
			auto part = a_csv.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
			while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
			while (!part.empty() && part.back() == ' ') part.remove_suffix(1);
			if (!part.empty()) out.push_back(Lower(part));
			if (comma == std::string_view::npos) break;
			start = comma + 1;
		}
		return out;
	}

	bool HasAny(const TagList& a_have, const TagList& a_want)
	{
		for (const auto& w : a_want) {
			if (std::ranges::find(a_have, w) != a_have.end()) return true;
		}
		return false;
	}

	bool HasAnySceneTag(const Scene& a_scene, const TagList& a_tags) { return HasAny(a_scene.tags, a_tags); }

	bool HasAnyActorTag(const Scene& a_scene, int a_pos, const TagList& a_tags)
	{
		if (a_pos < 0 || a_pos >= static_cast<int>(a_scene.actors.size())) return false;
		return HasAny(a_scene.actors[a_pos].tags, a_tags);
	}

	bool ActionHasAnyTag(const Action& a_action, const TagList& a_tags)
	{
		std::scoped_lock l(g_lock);
		auto it = g_actionTags.find(a_action.type);
		return it != g_actionTags.end() && HasAny(it->second, a_tags);
	}

	bool HasAnyActionTagOnAny(const Scene& a_scene, const TagList& a_tags)
	{
		return std::ranges::any_of(a_scene.actions, [&](const Action& a) { return ActionHasAnyTag(a, a_tags); });
	}

	namespace
	{
		template <class Pred>
		int FindIf(const Scene& a_scene, Pred&& a_pred)
		{
			for (int i = 0; i < static_cast<int>(a_scene.actions.size()); ++i) {
				if (a_pred(a_scene.actions[i])) return i;
			}
			return -1;
		}

		bool TypeIn(const Action& a, const TagList& types) { return std::ranges::find(types, a.type) != types.end(); }
	}

	int FindAnyAction(const Scene& s, const TagList& t)
	{
		return FindIf(s, [&](const Action& a) { return TypeIn(a, t); });
	}

	int FindAnyActionForActor(const Scene& s, int p, const TagList& t)
	{
		return FindIf(s, [&](const Action& a) { return a.actor == p && TypeIn(a, t); });
	}

	int FindAnyActionForTarget(const Scene& s, int p, const TagList& t)
	{
		return FindIf(s, [&](const Action& a) { return a.target == p && TypeIn(a, t); });
	}

	int FindAnyActionForPerformer(const Scene& s, int p, const TagList& t)
	{
		return FindIf(s, [&](const Action& a) { return a.performer == p && TypeIn(a, t); });
	}

	int FindActionTaggedForActor(const Scene& s, int p, const TagList& tags)
	{
		return FindIf(s, [&](const Action& a) { return a.actor == p && ActionHasAnyTag(a, tags); });
	}

	int FindActionTaggedForTarget(const Scene& s, int p, const TagList& tags)
	{
		return FindIf(s, [&](const Action& a) { return a.target == p && ActionHasAnyTag(a, tags); });
	}
}
