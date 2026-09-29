#pragma once

namespace Hooks
{
	void Install();
	// The per-frame NPC animation hook is in (the player's always is).
	[[nodiscard]] bool NPCHooked();
}
