// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "VoiceModel.h"

#include "FsUtil.h"
#include "LipSync.h"
#include "OStimData.h"
#include "Papyrus.h"
#include "Scenes.h"

namespace VoiceModel
{
	namespace
	{
		constexpr auto kRoot = "Data/SKSE/Plugins/OStim/voice sets";
		constexpr auto kTranslations = "Data/Interface/Translations";
		constexpr RE::FormID kPlayerBase = 0x7;  // the player's key in OStim's voice-set selection
		constexpr const char* kGenericDefault = "$ostim_generic_default";  // what OStim reports for "nothing picked"

		std::vector<Set> g_sets;                                      // immutable once loaded
		std::unordered_map<RE::FormID, std::size_t> g_byTarget;       // target and alias form ids -> set (the last file read wins, as in OStim)
		std::unordered_map<std::string, std::size_t> g_byName;        // lowercase translated name -> set (the first wins)
		std::unordered_map<std::string, std::string> g_translations;  // lowercase key -> text, English
		std::size_t g_skipped = 0;                                    // entries whose sound form could not be found

		struct Assigned
		{
			std::string name;
			bool answered = false;
			float askedAt = -100.0f;
		};
		std::mutex g_lock;
		std::unordered_map<RE::FormID, Assigned> g_assigned;  // actor form id -> what OStim says

		std::string Lower(std::string s)
		{
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		// ------------------------------------------------------------ translations
		std::string ToUtf8(const std::wstring& a_wide)
		{
			if (a_wide.empty()) return {};
			const int n = WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()), nullptr, 0, nullptr, nullptr);
			if (n <= 0) return {};
			std::string out(static_cast<std::size_t>(n), '\0');
			WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()), out.data(), n, nullptr, nullptr);
			return out;
		}

		// Skyrim translation files: UTF-16, "$key<TAB>text" per line.
		void LoadTranslationFile(const std::filesystem::path& a_path)
		{
			std::ifstream f(a_path, std::ios::binary);
			if (!f) return;
			const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			std::size_t start = 0;
			if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) start = 2;
			const std::size_t units = (bytes.size() - start) / 2;
			std::wstring wide(units, L'\0');
			std::memcpy(wide.data(), bytes.data() + start, units * 2);
			const std::string text = ToUtf8(wide);

			std::size_t pos = 0;
			while (pos < text.size()) {
				auto end = text.find('\n', pos);
				if (end == std::string::npos) end = text.size();
				std::string line = text.substr(pos, end - pos);
				pos = end + 1;
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (line.empty() || line[0] == ';') continue;
				const auto tab = line.find('\t');
				if (tab == std::string::npos) continue;
				g_translations[Lower(line.substr(0, tab))] = line.substr(tab + 1);
			}
		}

		void LoadTranslations()
		{
			std::error_code ec;
			if (!std::filesystem::exists(kTranslations, ec)) return;
			FsUtil::WalkFiles(kTranslations, [&](const std::filesystem::path& path) {
				auto name = path.filename().wstring();
				std::ranges::transform(name, name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
				if (name.ends_with(L"_english.txt")) LoadTranslationFile(path);
			});
		}

		// "$key{a,b}" is looked up as "$key{}" and each "{}" in the text takes the next argument (OSSO's "{} Female Shy" with "OSSO").
		// Anything that is not a known key stays as it is.
		std::string Translate(const std::string& a_name)
		{
			if (a_name.empty() || a_name[0] != '$') return a_name;
			std::vector<std::string> args;
			std::string key = a_name;
			const auto open = a_name.find('{');
			if (open != std::string::npos && a_name.back() == '}') {
				key = a_name.substr(0, open) + "{}";
				const std::string inner = a_name.substr(open + 1, a_name.size() - open - 2);
				std::size_t from = 0;
				while (from <= inner.size()) {
					const auto comma = inner.find(',', from);
					args.push_back(inner.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
					if (comma == std::string::npos) break;
					from = comma + 1;
				}
			}
			auto it = g_translations.find(Lower(key));
			if (it == g_translations.end()) {
				args.clear();
				it = g_translations.find(Lower(a_name));
				if (it == g_translations.end()) return a_name;
			}
			std::string out = it->second;
			std::size_t pos = 0;
			for (const auto& arg : args) {
				pos = out.find("{}", pos);
				if (pos == std::string::npos) break;
				out.replace(pos, 2, arg);
				pos += arg.size();
			}
			return out;
		}

		// ------------------------------------------------------------ voice-set JSON
		// A form reference is { "mod": "X.esp", "formid": "0x123" }: the id is the plugin's own, without its load-order byte.
		std::optional<RE::FormID> ReadRef(const json& a_ref)
		{
			if (!a_ref.is_object() || !a_ref.contains("formid")) return std::nullopt;
			const auto& f = a_ref.at("formid");
			RE::FormID local = 0;
			if (f.is_string()) local = static_cast<RE::FormID>(std::strtoul(f.get<std::string>().c_str(), nullptr, 16));
			else if (f.is_number_unsigned()) local = f.get<RE::FormID>();
			else return std::nullopt;
			std::string mod;
			if (const auto it = a_ref.find("mod"); it != a_ref.end() && it->is_string()) mod = it->get<std::string>();
			if (mod.empty()) return local;  // a runtime id
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh || !dh->LookupModByName(mod)) return std::nullopt;
			return dh->LookupFormID(local, mod);
		}

		// Entries: a single reference (legacy), or a list of { sound, condition?, expression?, moanIntervalOverride? }.
		void ReadEntries(const json& a_value, std::vector<Entry>& a_out, std::string_view a_defaultExpression)
		{
			auto add = [&](const json& soundRef, const json* element) {
				const auto id = ReadRef(soundRef);
				if (!id || !RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(*id)) {
					++g_skipped;
					return;
				}
				Entry e;
				e.sound = *id;
				e.expression = std::string(a_defaultExpression);
				if (element) {
					if (const auto c = element->find("condition"); c != element->end()) {
						if (const auto cid = ReadRef(*c); cid && RE::TESForm::LookupByID<RE::BGSPerk>(*cid)) e.condition = *cid;
					}
					if (const auto x = element->find("expression"); x != element->end() && x->is_string()) e.expression = x->get<std::string>();
					if (const auto x = element->find("info"); x != element->end() && x->is_string()) e.info = x->get<std::string>();
					if (const auto x = element->find("moanIntervalOverride"); x != element->end() && x->is_number()) e.interval = x->get<float>();
				}
				a_out.push_back(std::move(e));
			};
			if (a_value.is_object()) {
				add(a_value, nullptr);
			} else if (a_value.is_array()) {
				for (const auto& element : a_value) {
					if (element.is_object() && element.contains("sound")) add(element.at("sound"), &element);
				}
			}
		}

		std::size_t CountEntries(const json& a_value)
		{
			return a_value.is_array() ? a_value.size() : (a_value.is_object() ? 1 : 0);
		}

		void AddSounds(const List& a_list, std::vector<RE::FormID>& a_out)
		{
			for (const auto* v : { &a_list.sound, &a_list.muffled }) {
				for (const auto& e : *v) a_out.push_back(e.sound);
			}
		}

		// { sound, soundMuffled, dialogue } of one reaction set.
		void ReadReactionSet(const json& a_json, List& a_out, std::size_t& a_dialogues)
		{
			if (!a_json.is_object()) return;
			if (const auto it = a_json.find("sound"); it != a_json.end()) ReadEntries(*it, a_out.sound, {});
			if (const auto it = a_json.find("soundMuffled"); it != a_json.end()) ReadEntries(*it, a_out.muffled, {});
			if (const auto it = a_json.find("dialogue"); it != a_json.end()) a_dialogues += CountEntries(*it);
		}

		void ReadSet(const std::filesystem::path& a_path)
		{
			std::ifstream f(a_path);
			const auto doc = json::parse(f, nullptr, true, true);
			if (!doc.is_object() || !doc.contains("target")) {
				logger::warn("Voice model: {} has no target", FsUtil::Printable(a_path));
				return;
			}
			const auto target = ReadRef(doc.at("target"));
			// Default male / female sets target Skyrim.esm 0 and 1, which are not forms: LookupFormID gives them back as they are.
			if (!target) {
				logger::warn("Voice model: {}: the target is not in the load order", FsUtil::Printable(a_path));
				return;
			}

			Set s;
			s.file = FsUtil::Printable(a_path.filename());
			s.target = *target;
			std::string rawName = FsUtil::Printable(a_path.stem());
			if (const auto n = doc.find("name"); n != doc.end() && n->is_string() && !n->get<std::string>().empty()) rawName = n->get<std::string>();
			s.name = Translate(rawName);
			if (const auto al = doc.find("aliases"); al != doc.end() && al->is_array()) {
				for (const auto& a : *al) {
					if (const auto id = ReadRef(a)) s.aliases.push_back(*id);
				}
			}

			const bool legacy = doc.contains("moan") && doc.at("moan").is_object() && doc.at("moan").contains("mod");
			if (legacy) {
				// moan / moanMuffled / climax / climaxMuffled are lists of sounds directly on the file.
				auto expr = [&](const char* key, std::string_view fallback) {
					if (const auto it = doc.find(key); it != doc.end() && it->is_string()) return it->get<std::string>();
					return std::string(fallback);
				};
				const std::string moanExpr = expr("moanExpression", "moan");
				const std::string climaxExpr = expr("climaxExpression", "climax");
				if (const auto it = doc.find("moan"); it != doc.end()) ReadEntries(*it, s.moan.sound, moanExpr);
				if (const auto it = doc.find("moanMuffled"); it != doc.end()) ReadEntries(*it, s.moan.muffled, moanExpr);
				if (const auto it = doc.find("climax"); it != doc.end()) ReadEntries(*it, s.climax.sound, climaxExpr);
				if (const auto it = doc.find("climaxMuffled"); it != doc.end()) ReadEntries(*it, s.climax.muffled, climaxExpr);
			} else {
				std::size_t dialogues = 0;
				if (const auto it = doc.find("moan"); it != doc.end()) ReadReactionSet(*it, s.moan, dialogues);
				if (const auto it = doc.find("climax"); it != doc.end()) {
					std::size_t climaxDialogues = 0;
					ReadReactionSet(*it, s.climax, climaxDialogues);
					s.climaxDialogue = climaxDialogues > 0;
					dialogues += climaxDialogues;
				}
				for (const char* key : { "climaxCommentSelf", "climaxCommentOther" }) {
					if (const auto it = doc.find(key); it != doc.end()) {
						List ignored;
						ReadReactionSet(*it, ignored, dialogues);
						AddSounds(ignored, s.otherSounds);
					}
				}
				static constexpr std::pair<const char*, const char*> kEvents[] = { { "eventActorReactions", "actor" },
					{ "eventTargetReactions", "target" }, { "eventPerformerReactions", "performer" } };
				for (const auto& [key, role] : kEvents) {
					const auto it = doc.find(key);
					if (it == doc.end() || !it->is_object()) continue;
					for (const auto& [ev, set] : it->items()) {
						List ignored;
						ReadReactionSet(set, ignored, dialogues);
						AddSounds(ignored, s.otherSounds);
						s.eventReactions.push_back(std::format("{} ({})", ev, role));
					}
				}
				s.dialogues = dialogues;
			}

			const auto index = g_sets.size();
			g_byTarget[s.target] = index;
			for (const auto alias : s.aliases) g_byTarget[alias] = index;
			g_byName.try_emplace(Lower(s.name), index);
			g_byName.try_emplace(Lower(rawName), index);
			g_sets.push_back(std::move(s));
		}

		// ------------------------------------------------------------ resolving
		bool Fulfils(const Entry& a_entry, RE::Actor* a_actor, RE::Actor* a_partner)
		{
			if (!a_entry.condition) return true;
			auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(a_entry.condition);
			if (!perk) return true;  // as OStim: a condition it could not load does not block the entry
			return perk->perkConditions.IsTrue(a_actor, a_partner);
		}

		const Set* SetAt(std::size_t a_index) { return a_index < g_sets.size() ? &g_sets[a_index] : nullptr; }

		const Set* FindTarget(RE::FormID a_id)
		{
			if (!a_id) return nullptr;
			const auto it = g_byTarget.find(a_id);
			return it == g_byTarget.end() ? nullptr : SetAt(it->second);
		}

		std::string EntryText(const Entry& e, int index)
		{
			auto* form = RE::TESForm::LookupByID(e.sound);
			const char* edid = form ? form->GetFormEditorID() : "";
			return std::format("#{} {}{}{}{}", index, e.info.empty() ? "" : e.info + ": ", e.sound ? std::format("{:08X}", e.sound) : "-",
				edid && *edid ? std::format(" {}", edid) : "", e.interval > 0.0f ? std::format(", next after {:.2f} s", e.interval) : "");
		}

		std::string ListText(const char* a_label, const List& a_list, RE::Actor* a_actor, RE::Actor* a_partner, bool a_muffled)
		{
			if (a_list.empty()) return std::format("  {}: none\n", a_label);
			std::string out = std::format("  {}: {} plain, {} muffled", a_label, a_list.sound.size(), a_list.muffled.size());
			const auto c = Select(a_list, a_muffled, a_actor, a_partner);
			if (c.entry) out += std::format(" -> {} {}\n", a_muffled ? "muffled" : "plain", EntryText(*c.entry, c.index));
			else out += std::format(" -> nothing plays ({} list{})\n", a_muffled ? "muffled" : "plain", (a_muffled ? a_list.muffled : a_list.sound).empty() ? " is empty" : ", no condition holds");
			return out;
		}
	}

	const char* FoundName(Found a_how)
	{
		switch (a_how) {
		case Found::kAssigned: return "assigned to this actor";
		case Found::kBase: return "set for this actor base";
		case Found::kVoiceType: return "set for this voice type";
		case Found::kRace: return "set for this race";
		case Found::kDefault: return "default for the sex";
		default: return "none";
		}
	}

	void OnDataLoaded()
	{
		LoadTranslations();
		std::error_code ec;
		if (std::filesystem::exists(kRoot, ec)) {
			const auto stats = FsUtil::WalkFiles(kRoot, [&](const std::filesystem::path& path) {
				if (FsUtil::LowerExt(path) != L".json") return;
				try {
					ReadSet(path);
				} catch (const std::exception& e) {
					logger::warn("Voice model: {}: {}", FsUtil::Printable(path), e.what());
				}
			});
			if (stats.dirErrors) logger::warn("Voice model: {} voice-set folder(s) could not be read", stats.dirErrors);
		}
		std::size_t moans = 0, climaxes = 0;
		for (const auto& s : g_sets) {
			moans += s.moan.sound.size() + s.moan.muffled.size();
			climaxes += s.climax.sound.size() + s.climax.muffled.size();
		}
		// Which files are plain moans (what the Director takeover mutes and replaces) and which belong to anything else (left alone).
		std::vector<RE::FormID> plain, climax, other;
		for (const auto& s : g_sets) {
			AddSounds(s.moan, plain);
			// A set whose climax has dialogue keeps its whole climax with OStim (it picks the line over the sound).
			AddSounds(s.climax, s.climaxDialogue ? other : climax);
			other.insert(other.end(), s.otherSounds.begin(), s.otherSounds.end());
		}
		LipSync::SetPlainMoans(plain, climax, other);
		logger::info("Voice model: {} voice sets, {} moan and {} climax entries, {} translations, {} entries skipped (sound form not found)", g_sets.size(), moans,
			climaxes, g_translations.size(), g_skipped);
	}

	std::size_t SetCount() { return g_sets.size(); }

	void Request(RE::Actor* a_actor)
	{
		if (!a_actor) return;
		const RE::FormID base = a_actor->IsPlayerRef() ? kPlayerBase : (a_actor->GetActorBase() ? a_actor->GetActorBase()->GetFormID() : 0);
		if (!base) return;
		const RE::FormID key = a_actor->GetFormID();
		{
			std::scoped_lock l(g_lock);
			auto& a = g_assigned[key];
			const float now = Scenes::Now();
			if (now - a.askedAt < 10.0f) return;
			a.askedAt = now;
		}
		Papyrus::GetVoiceSetName(base, [key](std::string name) {
			std::scoped_lock l(g_lock);
			auto& a = g_assigned[key];
			a.name = std::move(name);
			a.answered = true;
		});
	}

	void Forget(RE::Actor* a_actor)
	{
		if (!a_actor) return;
		std::scoped_lock l(g_lock);
		g_assigned.erase(a_actor->GetFormID());
	}

	Resolved Resolve(RE::Actor* a_actor)
	{
		Resolved r;
		if (!a_actor || g_sets.empty()) return r;

		std::string assigned;
		{
			std::scoped_lock l(g_lock);
			if (const auto it = g_assigned.find(a_actor->GetFormID()); it != g_assigned.end() && it->second.answered) assigned = it->second.name;
		}
		if (!assigned.empty() && assigned != kGenericDefault) {
			if (const auto it = g_byName.find(Lower(assigned)); it != g_byName.end()) {
				r.set = SetAt(it->second);
				r.how = Found::kAssigned;
				return r;
			}
			// OStim says a set was picked but no file here has that name: fall through to the rules below, as the log line will say.
		}

		auto* base = a_actor->GetActorBase();
		const RE::FormID baseId = a_actor->IsPlayerRef() ? kPlayerBase : (base ? base->GetFormID() : 0);
		if (const auto* s = FindTarget(baseId)) return { s, Found::kBase };
		if (base && base->voiceType) {
			if (const auto* s = FindTarget(base->voiceType->GetFormID())) return { s, Found::kVoiceType };
		}
		auto* race = a_actor->GetRace();
		if (race) {
			if (const auto* s = FindTarget(race->GetFormID())) return { s, Found::kRace };
		}
		// Only people get a default set; creatures with nothing of their own make no sound.
		if (race && race->HasKeywordString("ActorTypeNPC")) {
			const bool female = base && base->GetSex() == RE::SEX::kFemale;
			if (const auto it = g_byTarget.find(female ? 1u : 0u); it != g_byTarget.end()) return { SetAt(it->second), Found::kDefault };
		}
		return r;
	}

	Choice Select(const List& a_list, bool a_muffled, RE::Actor* a_actor, RE::Actor* a_partner)
	{
		// A muffled actor takes the muffled list or nothing: a set without one is silent for them.
		const auto& entries = a_muffled ? a_list.muffled : a_list.sound;
		for (std::size_t i = 0; i < entries.size(); ++i) {
			if (Fulfils(entries[i], a_actor, a_partner)) return { &entries[i], static_cast<int>(i), a_muffled };
		}
		return {};
	}

	SceneContext Context(RE::Actor* a_actor)
	{
		SceneContext c;
		if (!a_actor) return c;
		std::scoped_lock sl(Scenes::Lock());
		auto* t = Scenes::ThreadOf(a_actor);
		if (!t) return c;
		c.inScene = true;
		auto* s = t->Find(a_actor);
		if (!s) return c;
		if (t->meta && t->meta->known) {
			const auto flags = OStimData::ActorSoundFlags(*t->meta, s->pos);
			if (flags.known) {
				c.known = true;
				c.moan = flags.moan;
				c.talk = flags.talk;
				c.muffled = flags.muffled;
			}
		}
		for (const auto& other : t->slots) {
			if (other.id != s->id && other.Get()) {
				c.partner = other.Get();
				break;
			}
		}
		return c;
	}

	std::string Describe(RE::Actor* a_actor)
	{
		if (!a_actor) return "no actor";
		Request(a_actor);
		std::string out = std::format("{}: ", a_actor->GetDisplayFullName());
		std::string assigned;
		bool answered = false;
		{
			std::scoped_lock l(g_lock);
			if (const auto it = g_assigned.find(a_actor->GetFormID()); it != g_assigned.end()) {
				assigned = it->second.name;
				answered = it->second.answered;
			}
		}
		out += std::format("OStim reports \"{}\"{}\n", assigned, answered ? "" : " (no answer yet: ask again in a moment)");

		const auto r = Resolve(a_actor);
		if (!r.set) return out + "  no voice set found: silent in OStim\n";
		std::string events;
		for (const auto& e : r.set->eventReactions) events += (events.empty() ? "" : ", ") + e;
		out += std::format("  set \"{}\" ({}), {}; {} dialogue entries and event reactions [{}] stay with OStim\n", r.set->name, r.set->file, FoundName(r.how),
			r.set->dialogues, events);

		// The scene decides whether they moan and whether it is muffled.
		const auto ctx = Context(a_actor);
		const bool inScene = ctx.inScene, known = ctx.known, moan = ctx.moan, muffled = ctx.muffled;
		RE::Actor* partner = ctx.partner;
		if (!inScene) out += "  not in a scene: showing what the lists would pick if they were, plain\n";
		else if (!known) out += "  the scene's actions have no sound flags known: assuming they may moan, not muffled\n";
		else out += std::format("  the scene's actions {} moaning, {}\n", moan ? "allow" : "do NOT allow", muffled ? "and say muffled" : "not muffled");
		out += ListText("moan", r.set->moan, a_actor, partner, muffled);
		out += ListText("climax", r.set->climax, a_actor, partner, muffled);
		return out;
	}
}
