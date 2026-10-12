// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// What each personality's moans are (both editions; Personality page > Moans by personality).
//
// OStim has one moan volume for everyone and picks a moan by the scene's speed. Here a personality has a rule for each phase of a scene (the first three stages,
// the fourth stage, the climax): which moans (none at all, as they come, or the quiet, normal or loud third of all the voice files, measured when they were
// decoded: LipSync::LevelOf) and how loud they are played (a volume tier, times the master, never above 1). Audio.rules holds the rules.
//
// OStim's own moans cannot be chosen, only shaped. A hook on BSGameSound::Play (the one the victim mute uses, installed first so a mute still has the last word)
// sets the volume a voice-set sound starts at, so there is no click: to nothing where the rule says no moans; otherwise toward the typical loudness of the class
// asked for (bLevelMoans), times the volume tier and the master. The sound is matched to an actor by walking the nodes it follows up to the actor's 3D, against a
// table of the actors in scenes that Tick keeps. A sound the hook did not see (VR has no hook), or whose volume was set again after Play, is corrected once by Tick.
//
// The Director takeover (Moans) plays its own sounds and does choose: it plays no moan where the rule says none, plays only files of the class asked for (the hook
// mutes a try of the wrong class, PlayDescriptor stops it and tries another file), and uses the plan's volume.
namespace MoanVolume
{
	void InstallHook();  // SKSE load, before Voice::InstallHooks
	void Tick();         // main thread, ~20 Hz from the scheduler
	// OStim says this actor's orgasm has begun (from the event sink, any thread): the climax rule applies at once, before the next Tick sees it.
	void NoteOrgasm(RE::FormID a_actor);

	struct Plan
	{
		bool silent = false;       // the rule says no moan
		unsigned classMask = 0;    // the loudness classes of file the rule asks for: bit 0 quiet, bit 1 normal, bit 2 loud; 0 any file
		float volume = 1.0f;       // the master times the volume tier: the factor on the volume the sound would otherwise play at
		int phase = 0;             // 0 the first three stages, 1 the fourth stage, 2 the climax
	};
	// The rule for this actor at this stage (1 to 4) or at the climax. For the takeover, which knows the voice set's own stage.
	[[nodiscard]] Plan PlanFor(RE::Actor* a_actor, int a_stage, bool a_climax);
	// The stage (1 to 4) the scene is at for this actor: the voice set's own where it has stages (full edition), else the scene's speed in quarters, the fourth
	// stage being the fastest. Main thread.
	[[nodiscard]] int StageOf(RE::Actor* a_actor);
	// The stage (1 to 4) of entry a_index of a staged list of a_count entries: the first is stage 1 and the last stage 4, so a set with fewer than four stages still has a last one.
	[[nodiscard]] int StageOfEntry(int a_index, int a_count);
	// How much wider or narrower the mouth opens for this actor's moans: from the volume tier of the rule in force. Lip-sync multiplies its gain by it.
	[[nodiscard]] float MouthGain(RE::Actor* a_actor);

	// For the debug trace and the menu.
	[[nodiscard]] std::string Describe(RE::Actor* a_actor);
	[[nodiscard]] std::string Status();
}
