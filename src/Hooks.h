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
