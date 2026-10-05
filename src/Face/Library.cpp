// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Face/Library.h"

#include "FsUtil.h"

namespace Face::Library
{
	namespace
	{
		constexpr auto kRoot = "Data/SKSE/Plugins/OStim/facial expressions";

		using Table = std::unordered_map<std::string, Pool>;

		// Filled once at data load and only read afterwards.
		std::vector<std::unique_ptr<Expression>> g_all;
		Table g_sets;
		Table g_events;
		Table g_actors;
		Table g_targets;
		std::size_t g_files = 0;

		std::string Lower(std::string_view s)
		{
			std::string out(s);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		bool In(std::initializer_list<int> list, int v) { return std::ranges::find(list, v) != list.end(); }

		Channel ParseChannel(const json& j)
		{
			Channel c;
			c.type = j.value("type", 0);
			c.base = j.value("baseValue", 0.0f);
			c.variance = j.value("variance", 0.0f);
			c.speedMult = j.value("speedMultiplier", 0.0f);
			c.excitementMult = j.value("excitementMultiplier", 0.0f);
			c.delay = j.value("delay", 0);
			c.delayVariance = j.value("delayVariance", 0);
			c.factionFallback = j.value("factionFallback", 0);
			if (j.contains("faction")) c.faction = j.at("faction").dump();
			return c;
		}

		void ParseVariant(const json& j, Variant& v)
		{
			v.defined = true;
			v.duration = j.value("duration", 0.5f);
			if (j.contains("expression")) {
				v.mood = ParseChannel(j.at("expression"));
				v.parts |= kMood;
			}
			if (j.contains("modifiers")) {
				for (const auto& m : j.at("modifiers")) {
					const auto c = ParseChannel(m);
					if (In({ 0, 1, 12, 13 }, c.type)) {
						v.lids.push_back(c);
						v.parts |= kLid;
					} else if (In({ 2, 3, 4, 5, 6, 7 }, c.type)) {
						v.brows.push_back(c);
						v.parts |= kBrow;
					} else if (In({ 8, 9, 10, 11 }, c.type)) {
						v.balls.push_back(c);
						v.parts |= kBall;
					}
				}
			}
			if (j.contains("phonemes")) {
				for (const auto& p : j.at("phonemes")) v.phonemes.push_back(ParseChannel(p));
				v.parts |= kPhoneme;  // even an empty list: OStim marks the part as set
			}
			v.objectThreshold = j.value("phonemeObjectThreshold", -5.0f);
			if (j.contains("phonemeObjects")) {
				for (const auto& o : j.at("phonemeObjects")) {
					if (o.is_string()) v.objects.push_back(Lower(o.get<std::string>()));
				}
			}
		}

		void AddKeys(Table& table, const json& j, const char* key, const Expression* e, bool actions)
		{
			if (!j.contains(key) || !j.at(key).is_array()) return;
			for (const auto& k : j.at(key)) {
				if (!k.is_string()) continue;
				std::string name = Lower(k.get<std::string>());
				if (actions) name = OStimData::CanonicalAction(name);
				table[std::move(name)].push_back(e);
			}
		}

		// Same folder rules as OStim: only the folder itself, .json only. Listing can fail transiently
		// under a virtual file system, so it is retried before the folder is given up on.
		std::vector<std::filesystem::path> ListFiles()
		{
			namespace fs = std::filesystem;
			std::vector<fs::path> out;
			std::error_code ec;
			for (int attempt = 1; attempt <= 3; ++attempt) {
				out.clear();
				ec.clear();
				for (fs::directory_iterator it{ kRoot, ec }, end; !ec && it != end; it.increment(ec)) {
					std::error_code typeEc;
					if (it->is_regular_file(typeEc) && FsUtil::LowerExt(it->path()) == L".json") out.push_back(it->path());
				}
				if (!ec) break;
				logger::warn("Expression library: listing {} failed (attempt {}/3): {}", kRoot, attempt, ec.message());
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			if (ec) out.clear();
			return out;
		}

		const Pool* Find(const Table& t, std::string_view name)
		{
			const auto it = t.find(Lower(name));
			return it != t.end() && !it->second.empty() ? &it->second : nullptr;
		}
	}

	void Load()
	{
		g_all.clear();
		g_sets.clear();
		g_events.clear();
		g_actors.clear();
		g_targets.clear();

		std::error_code ec;
		if (!std::filesystem::exists(kRoot, ec)) {
			logger::warn("Expression library: no OStim expression folder at {}", kRoot);
		}
		for (const auto& path : ListFiles()) {
			try {
				std::ifstream f(path);
				const auto doc = json::parse(f, nullptr, true, true);
				auto e = std::make_unique<Expression>();
				e->file = FsUtil::Narrow(path.stem()).value_or("?");
				if (doc.contains("female")) ParseVariant(doc.at("female"), e->female);
				if (doc.contains("male")) ParseVariant(doc.at("male"), e->male);
				AddKeys(g_sets, doc, "sets", e.get(), false);
				AddKeys(g_events, doc, "events", e.get(), false);
				AddKeys(g_actors, doc, "actionActors", e.get(), true);
				AddKeys(g_targets, doc, "actionTargets", e.get(), true);
				g_all.push_back(std::move(e));
			} catch (const std::exception& ex) {
				logger::warn("Expression library: {}: {}", FsUtil::Printable(path), ex.what());
			}
		}
		g_files = g_all.size();
		// OStim guarantees a "default" pool, empty if nobody defined one.
		if (!g_sets.contains("default")) {
			auto e = std::make_unique<Expression>();
			e->file = "(generated empty default)";
			g_sets["default"].push_back(e.get());
			g_all.push_back(std::move(e));
		}

		const auto s = GetStats();
		std::string names;
		for (const auto& [k, v] : g_sets) {
			if (!names.empty()) names += ", ";
			names += std::format("{}({})", k, v.size());
		}
		logger::info("Expression library: {} files; {} sets [{}], {} events, {} actions as actor, {} as target", s.files, s.sets, names, s.events,
			s.actionActors, s.actionTargets);
	}

	const Pool* Set(std::string_view a_name) { return Find(g_sets, a_name); }
	const Pool* Event(std::string_view a_name) { return Find(g_events, a_name); }
	const Pool* ActionActor(std::string_view a_action) { return Find(g_actors, OStimData::CanonicalAction(Lower(a_action))); }
	const Pool* ActionTarget(std::string_view a_action) { return Find(g_targets, OStimData::CanonicalAction(Lower(a_action))); }

	Resolved Resolve(const OStimData::Scene& scene, int pos)
	{
		Resolved r;
		const bool haveActor = pos >= 0 && pos < static_cast<int>(scene.actors.size());
		const OStimData::SceneActor* sa = haveActor ? &scene.actors[pos] : nullptr;

		// --- underlying
		if (sa && !sa->underlyingExpression.empty()) {
			if (const auto* p = Set(sa->underlyingExpression)) {
				r.underlying = p;
				r.underlyingWhy = "scene set '" + sa->underlyingExpression + "'";
			}
		}
		const auto tryAction = [&](const OStimData::Action& a, const char* why) {
			if (a.target == pos) {
				if (const auto* p = ActionTarget(a.type)) {
					r.underlying = p;
					r.underlyingWhy = std::format("{}'{}' as target", why, a.type);
					return true;
				}
			}
			if (a.actor == pos) {
				if (const auto* p = ActionActor(a.type)) {
					r.underlying = p;
					r.underlyingWhy = std::format("{}'{}' as actor", why, a.type);
					return true;
				}
			}
			return false;
		};
		if (!r.underlying && sa && sa->expressionAction >= 0 && sa->expressionAction < static_cast<int>(scene.actions.size())) {
			tryAction(scene.actions[sa->expressionAction], "named action ");
		}
		if (!r.underlying) {
			for (const auto& a : scene.actions) {
				if (tryAction(a, "action ")) break;
			}
		}
		if (!r.underlying) {
			r.underlying = Set("default");
			r.underlyingWhy = "default";
		}

		// --- override
		if (sa && !sa->expressionOverride.empty()) {
			if (const auto* p = Set(sa->expressionOverride)) {
				r.override = p;
				r.overrideWhy = "scene set '" + sa->expressionOverride + "'";
			}
		}
		for (std::size_t i = 0; !r.override && i < scene.actions.size(); ++i) {
			const auto& a = scene.actions[i];
			const int roles[3] = { a.actor, a.target, a.performer };
			for (int role = 0; role < 3 && !r.override; ++role) {
				if (roles[role] != pos) continue;
				const std::string name = OStimData::ActionRoleOverride(a.type, role);
				if (name.empty()) continue;
				if (const auto* p = Set(name)) {
					r.override = p;
					r.overrideWhy = std::format("'{}' by action '{}'", name, a.type);
				}
			}
		}
		return r;
	}

	std::string Describe(const OStimData::Scene& scene, int pos)
	{
		const auto r = Resolve(scene, pos);
		std::string out = std::format("pool {} ({})", r.underlyingWhy, r.underlying ? r.underlying->size() : 0);
		if (r.override) out += std::format(", override {} ({})", r.overrideWhy, r.override->size());
		return out;
	}

	Stats GetStats()
	{
		return { g_files, g_sets.size(), g_events.size(), g_actors.size(), g_targets.size() };
	}
}
