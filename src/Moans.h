// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// The Director's own moans.
//
// With Voice > bDirectorOwnMoans on and the face in Director mode, OStim's plain moans are muted on every actor OSIS paints a face on
// (Voice.cpp, only the plain-moan files: comments and event reactions such as a spank still play) and this module plays them instead, from the same voice sets and by the same rules OStim uses (VoiceModel):
//  - the set the actor has, and the entry whose condition holds (a dynamic set's speed stage);
//  - the muffled list, and only that, while an action of theirs says muffled; no moan at all where no action lets them;
//  - the entry's own "next moan after" time, else a random interval, counted from the end of the moan as OStim does.
// Each moan is a normal engine sound following the actor, so Lip-Sync moves the mouth with it.
//
// With bDirectorOwnClimax (on by default) the climax is taken over too: at each orgasm OStim reports (ostim_actor_orgasm) the voice set's
// climax list is used the same way, the muffled list while an action says muffled, and OStim's climax sound is muted. A moan of ours still
// sounding gives way to it. A set whose climax has dialogue stays entirely with OStim, which speaks the line in preference to the sound.
// Comments after a climax and event reactions (a spank) are never touched.
//
// It waits while the actor is speaking, while their climax or a reaction is playing, in their own orgasm, and while another mod holds
// their mouth (SLED_LipSyncMouth).
namespace Moans
{
	void Tick();    // main thread, ~20 Hz from the scheduler
	void OnOrgasm(RE::Actor* a_actor);  // main thread: OStim says this actor climaxed
	void Clear();   // game load
	[[nodiscard]] std::string Status();
}
