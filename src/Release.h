// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

namespace Release
{
	// Shown after the version in the log (so a log or crash report from a tester says which beta it is) and in the
	// README that tools/assemble_mod.py writes, which reads this line. The plugin's version number itself is numeric
	// (set_version in xmake.lua) and cannot say "beta". Empty for a final release.
	inline constexpr std::string_view kLabel = "beta 2 hotfix 1";
}
