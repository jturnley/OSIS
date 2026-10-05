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
// Nothing here talks to the plugin. It is found by its module name and its configs are read as text:
// accurate-penetration.toml and every file in ppa-override-configs, to know which mouth preset wins (the
// highest Priority) and what it does to the face. Whether it is playing one for a given actor is seen
// from the face itself (Face::Engine::UpdatePPAMouth).
namespace Face::PPA
{
	// Look for the plugin and read its configs. At data load, when every plugin is in.
	void Init();
	// Read them again if any changed on disk (PPA reloads on a hotkey). At each scene start.
	void Refresh();

	[[nodiscard]] bool Installed();
	// Installed, its expression system on, and a mouth preset that sets phonemes.
	[[nodiscard]] bool DrivesMouth();
	// The winning mouth preset zeroes eyes and brows (OverrideModifiers) or the mood (OverrideExpressions) while
	// it plays, which overwrites what OSIS writes there. Empty when it does not; otherwise says which file and
	// which setting, and what to change.
	[[nodiscard]] std::string FaceWarning();
	// One line for the settings page.
	[[nodiscard]] std::string Status();
}
