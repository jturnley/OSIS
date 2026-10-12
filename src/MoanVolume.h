// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// Moan volume by personality (both editions; Personality page > Moan volume).
//
// OStim has one moan volume for everyone. Here each voice-set sound (a moan, a climax, a reaction) is played at OStim's volume times a factor for the
// actor it follows: the master factor (Audio.fMoanMaster, below 1 so that a louder personality has room above a balanced one) times their personality's.
// The result is never above 1, the engine's top.
//
// How it is done. A hook on BSGameSound::Play (the one the victim mute uses, installed first so a mute still has the last word) scales the volume the
// sound is about to start at, so there is no click. The sound is matched to an actor by walking the nodes it follows up to the actor's 3D, against a table
// of the actors in scenes that Tick keeps. A sound the hook did not see (VR has no hook; one that started before the table did), or one whose volume was
// set again after Play, is corrected once by Tick. Sounds OSIS plays itself (the Director takeover) are not touched here: Moans uses Apply.
namespace MoanVolume
{
	void InstallHook();  // SKSE load, before Voice::InstallHooks
	void Tick();         // main thread, ~20 Hz from the scheduler

	// The factor an actor's voice-set sounds are played at: master times personality, 1 when the setting is off.
	[[nodiscard]] float Scale(RE::Actor* a_actor);
	// a_base times Scale, never above 1: the volume for a sound OSIS plays itself.
	[[nodiscard]] float Apply(RE::Actor* a_actor, float a_base);
	// How much wider or narrower the mouth opens for this actor's moans: the personality's volume factor, kept near 1. Lip-sync multiplies its gain by it.
	[[nodiscard]] float MouthGain(RE::Actor* a_actor);

	// For the debug trace and the menu.
	[[nodiscard]] std::string Describe(RE::Actor* a_actor);
	[[nodiscard]] std::string Status();
}
