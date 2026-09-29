#include "Compat.h"

#include "Papyrus.h"

namespace Compat
{
	namespace
	{
		std::array<bool, kCount> g_yield{};
		std::vector<std::string> g_conflicts;
		bool g_notified = false;

		constexpr std::array<const char*, kCount> kModuleName{ "Faces", "Body", "Living Skin", "Lip-Sync", "Softbody Arousal" };
	}

	void Detect()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto plugin = [&](const char* name, Module m) {
			if (dh && dh->LookupModByName(name)) {
				g_yield[m] = true;
				g_conflicts.push_back(std::format("{} is active: OSED Reborn's {} module is off", name, kModuleName[m]));
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
			g_conflicts.push_back("SoftbodyArousal.dll is loaded: OSED Reborn's Softbody Arousal module is off");
		}
		for (const auto& c : g_conflicts) logger::warn("{} (disable that mod to use the DLL's version)", c);
	}

	bool Disabled(Module m) { return g_yield[m]; }

	std::vector<std::string> Conflicts() { return g_conflicts; }

	void NotifyOnce()
	{
		if (g_notified || g_conflicts.empty()) return;
		g_notified = true;
		Papyrus::Notify(std::format("OSED Reborn: {} old mod(s) still active; see the Status page.", g_conflicts.size()));
	}
}
