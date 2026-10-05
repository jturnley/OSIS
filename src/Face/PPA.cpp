// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Face/PPA.h"

#include "FsUtil.h"

#include <fstream>

namespace Face::PPA
{
	namespace
	{
		constexpr auto kConfig = "Data/SKSE/Plugins/accurate-penetration.toml";
		constexpr auto kOverrides = "Data/SKSE/Plugins/ppa-override-configs";
		// PPA loads override files in reverse alphabetical order and later means higher priority, so this sorts
		// ahead of anything a mod ships ("0SMP_..." and the like). Only a preset's own Priority decides which
		// preset plays; the file order only matters for settings that two files both give.
		constexpr auto kOsisFile = "00_OSIS_PPA_Face.toml";
		constexpr auto kMarker = "# OSIS-PPA-FACE v1";

		// One [[FacialPreset]] that applies to the mouth: no Targets at all matches every orifice, as in PPA.
		struct Preset
		{
			std::string source;                // the file it came from
			double priority = 0.0;             // the highest wins
			bool phonemes = false;             // it sets phonemes (OverridePhonemes, or a Phoneme effect)
			bool overrideModifiers = false;    // zeroes eyes and brows it does not set
			bool overrideExpressions = false;  // zeroes the mood it does not set
			bool otherEffects = false;         // an Expression or Modifier effect of its own
			bool ours = false;                 // from OSIS's own override file
			std::vector<std::string> block;    // its lines as written, header to last effect
		};

		struct Config
		{
			bool installed = false;
			bool readable = false;              // the base config was found and read
			bool expressionSystem = true;       // [General] EnableExpressionSystem
			bool osisFile = false;              // OSIS's override file is among the overrides
			std::vector<Preset> mouthPresets;   // base config first, then the override files in name order
			std::size_t overrideFiles = 0;

			// What PPA plays on a mouth: the highest priority; the first read wins a tie. With a_withOurs false,
			// what it would play without OSIS's own override.
			const Preset* Winner(bool a_withOurs = true) const
			{
				const Preset* best = nullptr;
				for (const auto& p : mouthPresets) {
					if (p.ours && !a_withOurs) continue;
					if (!best || p.priority > best->priority) best = &p;
				}
				return best;
			}
		};

		std::mutex g_lock;
		Config g_cfg;
		std::filesystem::file_time_type g_stamp{};
		std::size_t g_stampFiles = 0;
		bool g_changedThisSession = false;  // OSIS wrote or removed its file: PPA has not necessarily read that yet

		std::string Trim(std::string s)
		{
			const auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
			while (!s.empty() && ws(static_cast<unsigned char>(s.back()))) s.pop_back();
			std::size_t i = 0;
			while (i < s.size() && ws(static_cast<unsigned char>(s[i]))) ++i;
			return s.substr(i);
		}

		std::string Lower(std::string s)
		{
			for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		bool IsTrue(const std::string& v) { return Lower(Trim(v)).rfind("true", 0) == 0; }

		std::string StripComment(std::string line)
		{
			if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
			return Trim(std::move(line));
		}

		// The parts of one TOML that matter here, read line by line: the General switch (base config only) and, per
		// [[FacialPreset]], its Targets, Priority, Override flags and the types of its effects, with the lines of
		// the preset kept as written. Presets that apply to the mouth are added to the config.
		void Parse(std::istream& in, const std::string& source, Config& c)
		{
			struct Pending
			{
				bool open = false;
				bool hasTargets = false;
				bool targetsMouth = false;
				bool inTargets = false;  // a Targets array that runs over several lines
				Preset p;
				bool overridePhonemes = false;
			} cur;
			enum class Where { kNone, kGeneral, kPreset, kEffect } where = Where::kNone;
			const bool ours = source == kOsisFile;

			const auto flush = [&] {
				if (cur.open && (!cur.hasTargets || cur.targetsMouth)) {
					// Blank lines and the comment banner before the next section are not part of the preset.
					auto& b = cur.p.block;
					while (!b.empty() && StripComment(b.back()).empty()) b.pop_back();
					cur.p.source = source;
					cur.p.ours = ours;
					cur.p.phonemes = cur.p.phonemes || cur.overridePhonemes;
					c.mouthPresets.push_back(std::move(cur.p));
				}
				cur = {};
			};

			std::string raw;
			bool first = true;
			while (std::getline(in, raw)) {
				if (first) {
					first = false;
					if (raw.rfind("\xEF\xBB\xBF", 0) == 0) raw.erase(0, 3);  // a byte-order mark
				}
				while (!raw.empty() && (raw.back() == '\r' || raw.back() == '\n')) raw.pop_back();
				const std::string line = StripComment(raw);

				if (!line.empty() && line.front() == '[' && !cur.inTargets) {
					const auto name = Lower(line);
					if (name == "[[facialpreset]]") {
						flush();
						cur.open = true;
						where = Where::kPreset;
						cur.p.block.push_back(raw);
					} else if (name == "[[facialpreset.effects]]") {
						where = cur.open ? Where::kEffect : Where::kNone;
						if (cur.open) cur.p.block.push_back(raw);
					} else {
						flush();
						where = name == "[general]" ? Where::kGeneral : Where::kNone;
					}
					continue;
				}
				if (cur.open) cur.p.block.push_back(raw);
				if (line.empty()) continue;

				if (cur.inTargets) {
					if (Lower(line).find("mouth") != std::string::npos) cur.targetsMouth = true;
					if (line.find(']') != std::string::npos) cur.inTargets = false;
					continue;
				}

				const auto eq = line.find('=');
				if (eq == std::string::npos) continue;
				const auto key = Lower(Trim(line.substr(0, eq)));
				const auto value = Trim(line.substr(eq + 1));

				switch (where) {
				case Where::kGeneral:
					if (key == "enableexpressionsystem") c.expressionSystem = IsTrue(value);
					break;
				case Where::kPreset:
					if (key == "targets") {
						cur.hasTargets = true;
						cur.targetsMouth = Lower(value).find("mouth") != std::string::npos;
						cur.inTargets = !value.empty() && value.front() == '[' && value.find(']') == std::string::npos;
					} else if (key == "priority") {
						cur.p.priority = std::strtod(value.c_str(), nullptr);
					} else if (key == "overridephonemes") {
						cur.overridePhonemes = IsTrue(value);
					} else if (key == "overridemodifiers") {
						cur.p.overrideModifiers = IsTrue(value);
					} else if (key == "overrideexpressions") {
						cur.p.overrideExpressions = IsTrue(value);
					}
					break;
				case Where::kEffect:
					if (key == "type") {
						const auto type = Lower(value);
						if (type.find("phoneme") != std::string::npos) cur.p.phonemes = true;
						else cur.p.otherEffects = true;  // Expression, Modifier, MFEE
					}
					break;
				default:
					break;
				}
			}
			flush();
		}

		std::filesystem::path OsisPath() { return std::filesystem::path(kOverrides) / kOsisFile; }

		// Whether a file is OSIS's: its first lines carry the marker. A file of another mod that happens to share the
		// name is never touched.
		bool IsOurs(const std::filesystem::path& a_path)
		{
			std::ifstream f(a_path);
			if (!f) return false;
			std::string line;
			for (int i = 0; i < 3 && std::getline(f, line); ++i) {
				if (Trim(line).rfind(kMarker, 0) == 0) return true;
			}
			return false;
		}

		// The files PPA reads, in name order.
		std::vector<std::filesystem::path> OverrideFiles()
		{
			namespace fs = std::filesystem;
			std::vector<fs::path> out;
			std::error_code ec;
			if (!fs::is_directory(kOverrides, ec)) return out;
			fs::directory_iterator it(kOverrides, fs::directory_options::skip_permission_denied, ec);
			if (ec) return out;
			for (const fs::directory_iterator end; it != end; it.increment(ec)) {
				if (ec) break;
				std::error_code entryEc;
				if (it->is_regular_file(entryEc) && FsUtil::LowerExt(it->path()) == L".toml") out.push_back(it->path());
			}
			std::ranges::sort(out);
			return out;
		}

		// The newest write time among the files, so a change to any is noticed.
		void Stamp(const std::vector<std::filesystem::path>& overrides, std::filesystem::file_time_type& newest, std::size_t& files)
		{
			std::error_code ec;
			newest = std::filesystem::last_write_time(kConfig, ec);
			files = 1 + overrides.size();
			for (const auto& f : overrides) {
				if (const auto t = std::filesystem::last_write_time(f, ec); !ec && t > newest) newest = t;
			}
		}

		void Load()
		{
			Config c;
			c.installed = GetModuleHandleW(L"AccuratePenetration.dll") != nullptr;
			std::filesystem::file_time_type newest{};
			std::size_t files = 0;
			std::error_code ec;
			if (c.installed && std::filesystem::exists(kConfig, ec)) {
				std::ifstream f(kConfig);
				if (f) {
					Parse(f, "accurate-penetration.toml", c);
					c.readable = true;
				}
				const auto overrides = OverrideFiles();
				for (const auto& path : overrides) {
					const auto name = FsUtil::Printable(path.filename());
					// A file called ours that is not ours is read like any other, as a mod's.
					const bool isOurs = name == kOsisFile && IsOurs(path);
					std::ifstream of(path);
					if (!of) continue;
					Parse(of, isOurs ? name : name + (name == kOsisFile ? " (not OSIS's)" : ""), c);
					c.osisFile = c.osisFile || isOurs;
					++c.overrideFiles;
				}
				Stamp(overrides, newest, files);
			}
			std::scoped_lock l(g_lock);
			g_cfg = std::move(c);
			g_stamp = newest;
			g_stampFiles = files;
		}

		std::string Warning(const Config& c)
		{
			const auto* w = c.Winner();
			if (!c.installed || !c.readable || !c.expressionSystem || !w || !w->phonemes) return {};
			std::string what;
			if (w->overrideModifiers) what = "OverrideModifiers";
			if (w->overrideExpressions) what += std::string(what.empty() ? "" : " and ") + "OverrideExpressions";
			if (what.empty()) return {};
			return std::format("PPA's mouth preset in {} (priority {:.0f}) sets {} = true: while it plays on a blowjob it zeroes the eyes, brows"
				"{} that OSIS writes. 'Keep OSIS's eyes and brows' on the Face > Mouth page writes an override that switches that off.",
				w->source, w->priority, what, w->overrideExpressions ? " and mood" : "");
		}

		// The text of OSIS's override: a copy of the preset with its two zeroing switches off and a higher Priority.
		std::string BuildOverride(const Preset& a_winner, long long a_priority)
		{
			std::string out;
			out += std::string(kMarker) + " - written by OSIS (OStim Standalone Immersive Sex); delete this file, or use 'Restore PPA's own setting' in\n";
			out += "# OSIS's menu, to put PPA back exactly as your other mods configure it.\n";
			out += "# It copies the mouth preset from " + a_winner.source + " with OverrideModifiers and OverrideExpressions off, so PPA keeps\n";
			out += "# the mouth and its morphs on a blowjob but leaves the eyes, brows and mood to OSIS. PPA reads it at startup or on its reload key.\n";
			out += "Inherits = \"Any\"\n\n";

			// The copy says its own Priority once: in place of the original's line, or straight after the header when
			// the original had none.
			const auto keyOf = [](const std::string& raw) {
				const auto line = StripComment(raw);
				const auto eq = line.find('=');
				if (line.empty() || line.front() == '[' || eq == std::string::npos) return std::string();
				return Lower(Trim(line.substr(0, eq)));
			};
			const bool hasPriority = std::ranges::any_of(a_winner.block, [&](const std::string& raw) { return keyOf(raw) == "priority"; });
			for (std::size_t i = 0; i < a_winner.block.size(); ++i) {
				const auto& raw = a_winner.block[i];
				const auto line = StripComment(raw);
				const auto eq = line.find('=');
				if (i > 0 && !line.empty() && line.front() != '[' && eq != std::string::npos) {
					const auto key = Lower(Trim(line.substr(0, eq)));
					const auto indent = raw.substr(0, raw.find_first_not_of(" \t"));
					if (key == "priority") {
						out += indent + "Priority = " + std::to_string(a_priority) + "\n";
						continue;
					}
					if (key == "overridemodifiers" || key == "overrideexpressions") {
						out += indent + Trim(line.substr(0, eq)) + " = false\n";
						continue;
					}
				}
				out += raw + "\n";
				if (i == 0 && !hasPriority) out += "Priority = " + std::to_string(a_priority) + "\n";
			}
			return out;
		}

		bool UnderModOrganizer() { return GetModuleHandleW(L"usvfs_x64.dll") != nullptr; }

		constexpr auto kReloadNote = "PPA reads its configs at startup and on its reload key (F5 by default): restart the game or press it for this to take effect.";
	}

	void Init()
	{
		Load();
		logger::info("PPA: {}", Status());
		if (const auto w = FaceWarning(); !w.empty()) logger::warn("PPA: {}", w);
	}

	void Refresh()
	{
		{
			std::scoped_lock l(g_lock);
			if (!g_cfg.installed) return;
		}
		std::filesystem::file_time_type newest{};
		std::size_t files = 0;
		Stamp(OverrideFiles(), newest, files);
		bool changed;
		{
			std::scoped_lock l(g_lock);
			changed = newest != g_stamp || files != g_stampFiles;
		}
		if (changed) {
			Load();
			logger::info("PPA config changed: {}", Status());
			if (const auto w = FaceWarning(); !w.empty()) logger::warn("PPA: {}", w);
		}
	}

	bool Installed()
	{
		std::scoped_lock l(g_lock);
		return g_cfg.installed;
	}

	bool DrivesMouth()
	{
		std::scoped_lock l(g_lock);
		const auto* w = g_cfg.Winner();
		return g_cfg.installed && g_cfg.readable && g_cfg.expressionSystem && w && w->phonemes;
	}

	std::string FaceWarning()
	{
		std::scoped_lock l(g_lock);
		return Warning(g_cfg);
	}

	bool OsisOverrideInPlace()
	{
		std::scoped_lock l(g_lock);
		return g_cfg.osisFile;
	}

	const char* OsisOverrideName() { return kOsisFile; }

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		if (!g_cfg.installed) return "not installed";
		if (!g_cfg.readable) return "installed, its config could not be read";
		if (!g_cfg.expressionSystem) return "installed, expression system off: it will not touch the face";
		const auto* w = g_cfg.Winner();
		if (!w) return "installed, no facial preset for the mouth: it will not touch the face";
		auto s = std::format("installed, {} facial preset(s) for the mouth across {} override file(s); the one that plays is in {} (priority {:.0f}){}",
			g_cfg.mouthPresets.size(), g_cfg.overrideFiles, w->source, w->priority,
			w->phonemes ? ", it sets the mouth phonemes" : ", it does not set phonemes, so OSIS keeps the mouth");
		if (g_changedThisSession) s += "; OSIS changed its override this session - PPA picks that up on its reload key or after a restart";
		return s;
	}

	Result KeepFace()
	{
		Config c;
		{
			std::scoped_lock l(g_lock);
			c = g_cfg;
		}
		if (!c.installed || !c.readable) return { false, "PPA is not installed, or its config could not be read." };
		const auto* w = c.Winner(false);
		if (!w || !w->phonemes) return { false, "PPA has no mouth preset to adjust." };
		if (!w->overrideModifiers && !w->overrideExpressions) {
			return { false, std::format("PPA's mouth preset ({}) does not take the eyes or brows, so there is nothing to change.", w->source) };
		}
		if (w->block.empty()) return { false, "Could not read that preset's lines." };
		if (w->priority >= 2147483000.0) return { false, "That preset's Priority is too high to outrank." };
		const long long priority = static_cast<long long>(std::floor(w->priority)) + 1;

		const auto path = OsisPath();
		std::error_code ec;
		if (std::filesystem::exists(path, ec) && !IsOurs(path)) {
			return { false, std::format("{} already exists and is not OSIS's; it was left alone.", kOsisFile) };
		}
		std::filesystem::create_directories(path.parent_path(), ec);
		const auto text = BuildOverride(*w, priority);
		{
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out) return { false, "Could not write the override file." };
			out << text;
			if (!out) return { false, "Could not write the override file." };
		}
		{
			std::scoped_lock l(g_lock);
			g_changedThisSession = true;
		}
		Load();
		logger::info("PPA: wrote {} - a copy of the mouth preset in {} (priority {} -> {}) with OverrideModifiers and OverrideExpressions off", kOsisFile, w->source,
			static_cast<long long>(w->priority), priority);
		return { true, std::format("OSIS now keeps the eyes and brows on blowjobs. {}{}", kReloadNote,
						UnderModOrganizer() ? " Mod Organizer put the file in your overwrite folder." : "") };
	}

	Result RestoreDefault()
	{
		const auto path = OsisPath();
		std::error_code ec;
		if (!std::filesystem::exists(path, ec)) return { false, "OSIS has no override file to remove." };
		if (!IsOurs(path)) return { false, std::format("{} is not OSIS's; it was left alone.", kOsisFile) };
		if (!std::filesystem::remove(path, ec) || ec) return { false, "Could not delete the override file." };
		{
			std::scoped_lock l(g_lock);
			g_changedThisSession = true;
		}
		Load();
		logger::info("PPA: removed {}; PPA is as its other mods configure it", kOsisFile);
		return { true, std::format("OSIS's override is gone; PPA is as your other mods set it. {}", kReloadNote) };
	}
}
