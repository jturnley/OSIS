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

		// One [[FacialPreset]] that applies to the mouth: no Targets at all matches every orifice, as in PPA.
		struct Preset
		{
			std::string source;            // the file it came from
			double priority = 0.0;         // the highest wins
			bool phonemes = false;         // it sets phonemes (OverridePhonemes, or a Phoneme effect)
			bool overrideModifiers = false;   // zeroes eyes and brows it does not set
			bool overrideExpressions = false; // zeroes the mood it does not set
			bool otherEffects = false;     // an Expression or Modifier effect of its own
		};

		struct Config
		{
			bool installed = false;
			bool readable = false;              // the base config was found and read
			bool expressionSystem = true;       // [General] EnableExpressionSystem
			std::vector<Preset> mouthPresets;   // base config first, then the override files in name order
			std::size_t overrideFiles = 0;

			// What PPA plays on a mouth: the highest priority; the first read wins a tie.
			const Preset* Winner() const
			{
				const Preset* best = nullptr;
				for (const auto& p : mouthPresets) {
					if (!best || p.priority > best->priority) best = &p;
				}
				return best;
			}
		};

		std::mutex g_lock;
		Config g_cfg;
		std::filesystem::file_time_type g_stamp{};
		std::size_t g_stampFiles = 0;

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

		// The parts of one TOML that matter here, read line by line: the General switch (base config only) and, per
		// [[FacialPreset]], its Targets, Priority, Override flags and the types of its effects. Presets that apply
		// to the mouth are added to the config.
		void Parse(std::istream& in, const std::string& source, Config& c)
		{
			struct Pending
			{
				bool open = false;
				bool hasTargets = false;
				bool targetsMouth = false;
				Preset p;
				bool overridePhonemes = false;
			} cur;
			enum class Where { kNone, kGeneral, kPreset, kEffect } where = Where::kNone;

			const auto flush = [&] {
				if (cur.open && (!cur.hasTargets || cur.targetsMouth)) {
					cur.p.source = source;
					cur.p.phonemes = cur.p.phonemes || cur.overridePhonemes;
					c.mouthPresets.push_back(cur.p);
				}
				cur = {};
			};

			std::string line;
			while (std::getline(in, line)) {
				if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
				line = Trim(line);
				if (line.empty()) continue;

				if (line.front() == '[') {
					const auto name = Lower(line);
					if (name == "[[facialpreset]]") {
						flush();
						cur.open = true;
						where = Where::kPreset;
					} else if (name == "[[facialpreset.effects]]") {
						where = cur.open ? Where::kEffect : Where::kNone;
					} else {
						flush();
						where = name == "[general]" ? Where::kGeneral : Where::kNone;
					}
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

		// The files PPA reads, with the newest write time among them, so a change to any is noticed.
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
					std::ifstream of(path);
					if (!of) continue;
					Parse(of, FsUtil::Printable(path.filename()), c);
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
				"{} that OSIS writes. Set it to false in that file to keep OSIS's.",
				w->source, w->priority, what, w->overrideExpressions ? " and mood" : "");
		}
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

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		if (!g_cfg.installed) return "not installed";
		if (!g_cfg.readable) return "installed, its config could not be read";
		if (!g_cfg.expressionSystem) return "installed, expression system off: it will not touch the face";
		const auto* w = g_cfg.Winner();
		if (!w) return "installed, no facial preset for the mouth: it will not touch the face";
		return std::format("installed, {} facial preset(s) for the mouth across {} override file(s); the one that plays is in {} (priority {:.0f}){}",
			g_cfg.mouthPresets.size(), g_cfg.overrideFiles, w->source, w->priority,
			w->phonemes ? ", it sets the mouth phonemes" : ", it does not set phonemes, so OSIS keeps the mouth");
	}
}
