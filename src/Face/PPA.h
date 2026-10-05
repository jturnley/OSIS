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
//
// A preset can also zero the eyes and brows (OverrideModifiers) or the mood (OverrideExpressions) while it
// plays, which overwrites what OSIS writes there. PPA's override configs append their presets to the pool and
// the highest Priority wins, so OSIS can keep the face by writing an override of its own: a copy of the winning
// preset with those two switched off and a Priority one higher. It never touches another mod's file, and
// deleting its file puts PPA back exactly as it was.
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
	// it plays. Empty when it does not; otherwise says which file and which setting, and what to do about it.
	[[nodiscard]] std::string FaceWarning();
	// One line for the settings page.
	[[nodiscard]] std::string Status();

	// OSIS's own override file is there (and says it is OSIS's).
	[[nodiscard]] bool OsisOverrideInPlace();
	// The file name, for the menu.
	[[nodiscard]] const char* OsisOverrideName();

	struct Result
	{
		bool ok = false;
		std::string message;  // for the player: what happened, and when PPA will notice
	};
	// Write OSIS's override: a copy of the mouth preset PPA would otherwise play, with OverrideModifiers and
	// OverrideExpressions off and a Priority one higher. PPA reads it at startup or on its reload key.
	Result KeepFace();
	// Delete OSIS's override, leaving PPA exactly as the other mods configured it. Refuses a file that is not OSIS's.
	Result RestoreDefault();
}
