// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// Double-driving guard. If the original OSED Papyrus mods or the standalone Softbody
// Arousal DLL are still active, two engines would drive the same faces, bones and
// morphs. The overlapping module here stands down for the session and says why.
namespace Compat
{
	enum Module : int
	{
		kFace = 0,   // OStimExpressionDirector.esp, OSED Reborn Core.esp
		kBody,       // OSED_Body.esp, OSED Reborn Body.esp
		kSkin,       // OSED_LivingSkin.esp, OSED Reborn Living Skin.esp
		kLipSync,    // OSED_LipSync.esp, OSED Reborn Lip-Sync.esp
		kArousal,    // SoftbodyArousal.dll
		kCount
	};

	void Detect();                        // kDataLoaded
	[[nodiscard]] bool Disabled(Module a_module);
	[[nodiscard]] std::string Reason(Module a_module);  // why it stood down, empty when it didn't
	[[nodiscard]] bool DDFActive();       // Dynamic Dialogue Framework: Lip-Sync stands down unless told not to
	[[nodiscard]] std::vector<std::string> Conflicts();
	void NotifyOnce();                    // first game load: in-game notification
}
