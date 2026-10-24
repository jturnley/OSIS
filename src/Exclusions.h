// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// Actors OSIS leaves entirely alone. An NPC with a voice or a face of another mod's making (a unique voice with its own OStim voice set, say) is
// otherwise given a personality, a face, moan rules and body effects like anyone else, and what that mod made for them is shut out. An actor that
// is left alone is not painted, not lip-synced, has no personality (so no rate, limit or control of climax), is not shaped by the moan rules and
// has no body, blush or arousal effects. The scene itself, and everyone else in it, carry on as usual. The player is never left alone.
//
// Who: the names and the actor bases on the list (Data/SKSE/Plugins/OSIS/excluded_actors.txt, kept by the menu and editable by hand), and, by
// setting, every NPC whose voice type is a unique one of the base game (Delphine, Maven, Astrid...) or comes from another mod.
namespace Exclusions
{
	struct ListEntry
	{
		std::string text;       // the line as written
		bool resolved = true;   // a plugin|FormID line whose plugin is in the load order (a name always is)
	};

	void Load();                                         // data loaded: read the list (made, with its comment, when the file is not there yet)

	[[nodiscard]] bool IsExcluded(RE::Actor* a_actor);   // never the player; cheap, cached
	[[nodiscard]] std::string Why(RE::Actor* a_actor);   // "" when not left alone, else the reason, for the menu and the log

	bool Add(RE::Actor* a_actor);                        // put them on the list (and in the file); false when they already are, or cannot be
	int Remove(RE::Actor* a_actor);                      // take them off it, whichever way they were listed; how many entries went
	[[nodiscard]] std::vector<ListEntry> List();
	bool RemoveAt(std::size_t a_index);                  // the list's own index, for the menu's buttons
	[[nodiscard]] std::string Status();
	[[nodiscard]] const char* FilePath();
}
