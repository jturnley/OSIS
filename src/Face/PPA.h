// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// PPA - Procedural Penis Animations / Penetration Physics (AccuratePenetration.dll). While a penis is in
// an actor's mouth it plays its own facial preset on that actor: by default the phonemes that open the
// mouth, with every other phoneme zeroed (OverridePhonemes). That is the mouth OSIS would otherwise be
// writing, so for a blowjob the Director leaves it to PPA.
//
// Nothing here talks to the plugin. It is found by its module name and its config is read as text, to
// know whether it has a mouth preset to play at all. Whether it is playing one for a given actor is
// seen from the face itself (Face::Engine::UpdatePPAMouth).
namespace Face::PPA
{
	// Look for the plugin and read its config. At data load, when every plugin is in.
	void Init();
	// Read the config again if it changed on disk (PPA reloads it on a hotkey). At each scene start.
	void Refresh();

	[[nodiscard]] bool Installed();
	// Installed, its expression system on, and a facial preset that applies to the mouth.
	[[nodiscard]] bool DrivesMouth();
	// One line for the settings page.
	[[nodiscard]] std::string Status();
}
