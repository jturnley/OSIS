// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// A starting personality for an NPC by name, from the bios of the SkyrimNet and CHIM NPCs: Data/SKSE/Plugins/OSIS/npc_personalities.txt (shipped; one
// line per NPC, "name|personality|suffix") and npc_personalities_user.txt beside it (the player's own lines, which win). An NPC with a line starts with
// that personality; any other falls through to the voice set, the AI values and the seeded roll (Face::Engine::ResolvePersonality). The files are
// read once, the first time a personality is looked up.
namespace BioPersonality
{
	// The personality id (Face::Engine::Pers) for this NPC, or -1 when the tables say nothing about them. Not the edition's: the caller maps a
	// submissive to a timid in the default edition. Never the player.
	[[nodiscard]] int Lookup(RE::Actor* a_actor);

	// For the debug trace and the menu.
	[[nodiscard]] std::string Describe(RE::Actor* a_actor);
	[[nodiscard]] std::string Status();
}
