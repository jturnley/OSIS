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

		struct Config
		{
			bool installed = false;
			bool readable = false;
			bool expressionSystem = true;  // [General] EnableExpressionSystem
			int mouthPresets = 0;          // [[FacialPreset]] entries whose Targets include "Mouth"
			bool zeroesPhonemes = false;   // one of them has OverridePhonemes
			bool touchesFaceElsewhere = false;  // it also writes expressions or modifiers: OSIS yields only the phonemes
		};

		std::mutex g_lock;
		Config g_cfg;
		std::filesystem::file_time_type g_stamp{};
		bool g_stampValid = false;

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

		// The parts of the TOML that matter here, read line by line: the General switch and, per
		// [[FacialPreset]], its Targets, Override flags and the types of its effects.
		Config Parse(std::istream& in)
		{
			Config c;
			c.readable = true;

			struct Preset
			{
				bool open = false;
				bool mouth = false;
				bool phonemes = false;
				bool others = false;
			} p;
			enum class Where { kNone, kGeneral, kPreset, kEffect } where = Where::kNone;

			const auto flush = [&] {
				if (p.open && p.mouth) {
					++c.mouthPresets;
					c.zeroesPhonemes = c.zeroesPhonemes || p.phonemes;
					c.touchesFaceElsewhere = c.touchesFaceElsewhere || p.others;
				}
				p = {};
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
						p.open = true;
						where = Where::kPreset;
					} else if (name == "[[facialpreset.effects]]") {
						where = p.open ? Where::kEffect : Where::kNone;
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
					if (key == "targets") p.mouth = Lower(value).find("mouth") != std::string::npos;
					else if (key == "overridephonemes" && IsTrue(value)) p.phonemes = true;
					else if ((key == "overrideexpressions" || key == "overridemodifiers") && IsTrue(value)) p.others = true;
					break;
				case Where::kEffect:
					if (key == "type") {
						const auto type = Lower(value);
						if (type.find("phoneme") != std::string::npos) p.phonemes = true;
						else p.others = true;  // Expression, Modifier, MFEE
					}
					break;
				default:
					break;
				}
			}
			flush();
			return c;
		}

		void Load()
		{
			Config c;
			c.installed = GetModuleHandleW(L"AccuratePenetration.dll") != nullptr;
			std::error_code ec;
			if (c.installed && std::filesystem::exists(kConfig, ec)) {
				std::ifstream f(kConfig);
				if (f) {
					c = Parse(f);
					c.installed = true;
				}
				g_stamp = std::filesystem::last_write_time(kConfig, ec);
				g_stampValid = !ec;
			}
			std::scoped_lock l(g_lock);
			g_cfg = c;
		}
	}

	void Init()
	{
		Load();
		logger::info("PPA: {}", Status());
	}

	void Refresh()
	{
		bool changed = false;
		{
			std::scoped_lock l(g_lock);
			if (!g_cfg.installed) return;
		}
		std::error_code ec;
		const auto now = std::filesystem::last_write_time(kConfig, ec);
		if (!ec && (!g_stampValid || now != g_stamp)) changed = true;
		if (changed) {
			Load();
			logger::info("PPA config changed: {}", Status());
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
		return g_cfg.installed && g_cfg.readable && g_cfg.expressionSystem && g_cfg.mouthPresets > 0;
	}

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		if (!g_cfg.installed) return "not installed";
		if (!g_cfg.readable) return "installed, its config could not be read";
		if (!g_cfg.expressionSystem) return "installed, expression system off: it will not touch the face";
		if (g_cfg.mouthPresets == 0) return "installed, no facial preset for the mouth: it will not touch the face";
		auto s = std::format("installed, {} facial preset(s) for the mouth{}", g_cfg.mouthPresets,
			g_cfg.zeroesPhonemes ? ", it sets the mouth phonemes" : "");
		if (g_cfg.touchesFaceElsewhere) s += "; it also writes expressions or modifiers, which OSIS does not hand over";
		return s;
	}
}
