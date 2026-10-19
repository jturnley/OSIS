// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// A virgin's opening line. Before a scene gets going, a woman OVirginity Reflowered says is a virgin (and who is not one of the Companions, who are left out of the
// first-time rules) tells her partner so: "Please be gentle with me. I've never done this before." There are five lines for each personality, recorded for each of the
// base game's female voice types, as loose FUZ files (the audio with the lip data the game moves the mouth by, made with the Creation Kit's own tools) in the OSIS Virgin Openers voice pack:
//     Data/Sound/OSIS/Openers/<voice type>/<personality>/<name>.fuz
// The actor speaks one of them through the console's SpeakSound, so it is a line spoken in the actor's voice (the moans give way to it, the spoken-lines volume applies to
// it). The voice type is the actor's own, else her race's generic voice, else FemaleEvenToned. Nothing happens, and nothing is logged as an error, where the pack is not installed.
namespace Openers
{
	// Say one of her lines. Main thread (the caller queues it as a task). Returns what happened, for the log and the menu's test button.
	std::string Say(RE::Actor* a_actor);

	[[nodiscard]] std::string Status();
}
