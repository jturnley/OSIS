// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Compat.h"

#include "Papyrus.h"
#include "Settings.h"

namespace Compat
{
	namespace
	{
		std::array<bool, kCount> g_yield{};
		std::array<std::string, kCount> g_reason;
		std::vector<std::string> g_conflicts;
		bool g_ddf = false;
		bool g_notified = false;

		constexpr std::array<const char*, kCount> kModuleName{ "Faces", "Body", "Living Skin", "Lip-Sync", "Arousal" };
	}

	void Detect()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto plugin = [&](const char* name, Module m) {
			if (dh && dh->LookupModByName(name)) {
				g_yield[m] = true;
				g_reason[m] = std::format("stood down: {} is active", name);
				g_conflicts.push_back(std::format("{} is active: the {} module is off", name, kModuleName[m]));
			}
		};
		// The original OSED mods, then this project's Papyrus fallback (renamed so it never
		// overwrites the originals).
		plugin("OStimExpressionDirector.esp", kFace);
		plugin("OSED_Body.esp", kBody);
		plugin("OSED_LivingSkin.esp", kSkin);
		plugin("OSED_LipSync.esp", kLipSync);
		plugin("OSED Reborn Core.esp", kFace);
		plugin("OSED Reborn Body.esp", kBody);
		plugin("OSED Reborn Living Skin.esp", kSkin);
		plugin("OSED Reborn Lip-Sync.esp", kLipSync);
		if (GetModuleHandleW(L"SoftbodyArousal.dll")) {
			g_yield[kArousal] = true;
			g_reason[kArousal] = "stood down: SoftbodyArousal.dll is loaded";
			g_conflicts.push_back("SoftbodyArousal.dll is loaded: the Arousal module is off");
		}
		for (const auto& c : g_conflicts) logger::warn("{} (disable that mod to use the DLL's version)", c);
		// Not an old version of ours, just another mod that moves the mouth; see LipSync::bYieldToDDF.
		g_ddf = dh && dh->LookupModByName("TMS_DynamicDialogue.esp");
		if (g_ddf) logger::info("Dynamic Dialogue Framework is active: Lip-Sync stands down while bYieldToDDF is on");
	}

	// Read without Settings::lock: callers hold other modules' locks, and a stale bool is harmless.
	bool Disabled(Module m) { return g_yield[m] || (m == kLipSync && g_ddf && Settings::LipSync::bYieldToDDF); }

	std::string Reason(Module m)
	{
		if (g_yield[m]) return g_reason[m];
		if (m == kLipSync && g_ddf && Settings::LipSync::bYieldToDDF) return "stood down: Dynamic Dialogue Framework is active";
		return {};
	}

	bool DDFActive() { return g_ddf; }

	std::vector<std::string> Conflicts() { return g_conflicts; }

	void NotifyOnce()
	{
		if (g_notified || g_conflicts.empty()) return;
		g_notified = true;
		Papyrus::Notify(std::format("OSIS: {} old mod(s) still active; see the Status page.", g_conflicts.size()));
	}
}
