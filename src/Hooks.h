// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

namespace Hooks
{
	void Install();
	// The per-frame animation hooks are in. General::bAnimationHooks (read at startup) can leave
	// them out for a mod whose hook on the same calls conflicts; faces then fall back to 20 Hz
	// writes from the main thread and toe/finger curl stops.
	[[nodiscard]] bool NPCHooked();
	[[nodiscard]] bool PlayerHooked();
}
