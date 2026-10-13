// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

#include "Scenes.h"

// First times. A woman's personality in scenes is greatly influenced by her first encounters, so for the women OVirginity Reflowered says are virgins:
//  - her first consensual scene gives her a personality: shy (40%) or timid (40%), the rest whatever the usual rules give; a religious woman is always shy, and one who worships
//    Dibella is balanced, vocal or wild;
//  - a scene with an orgasm that does not end her virginity may make her balanced (20%), every time until it does;
//  - the scene that ends it leaves her stoic half of the time if it was bad (no orgasm before the penetration, a rough scene, or the speed past the first before she has one), and
//    balanced, vocal or wild if it was extraordinary (five orgasms or more, shifting with each one above);
//  - in the full edition, a non-consensual first time leaves her broken (30%), stoic (30%), shy (25%), submissive (10%) or, 5% of the time, crazed about the first partner to give
//    her an orgasm. A broken woman is shut down during sex (Virginity::IsBroken: the face, the body, the voice and the arousal rate do not respond, whatever else is going on) until a
//    session of ten silent "orgasms", with nothing rough in it, makes her stoic.
// Men have no first time. Only a personality OSIS settled on by itself changes; one the player set stays. The state is kept in the save (Serialization, record VIRG).
namespace Virginity
{
	struct Record
	{
		std::uint8_t state = 0;          // 0: not seen as a virgin in a scene yet; 1: her first scene gave her a personality, and her first time is still to come; 2: over
		std::int32_t original = -1;      // the personality her first scene gave her
		std::uint8_t pendingCrazed = 0;  // crazed about the first partner who gives her an orgasm (the full edition's 5%)
		std::uint8_t broken = 0;         // shut down (the full edition)
		float brokenRate = 0.0f;         // the multiplier on OStim's excitement rate while broken: 0.02, and 0.01 more for every "orgasm"
	};

	// Scene events, from Face::Engine; the caller holds Settings::lock.
	void OnThreadReady(Scenes::Thread& a_thread);
	void OnSceneChanged(Scenes::Thread& a_thread);
	void OnTick(Scenes::Thread& a_thread);
	void OnOrgasm(Scenes::Thread& a_thread, Scenes::Slot& a_slot);
	void OnSceneEnd(Scenes::Thread& a_thread);

	// A broken woman (full edition; false in the default one): nothing shows or sounds, and her excitement builds at exactly BrokenRate, whatever else would change it.
	[[nodiscard]] bool IsBroken(RE::Actor* a_actor);
	[[nodiscard]] float BrokenRate(RE::Actor* a_actor);

	[[nodiscard]] std::string Describe(RE::Actor* a_actor);
	[[nodiscard]] std::string Status();

	// The cosave.
	[[nodiscard]] std::unordered_map<RE::FormID, Record> Snapshot();
	void Restore(std::unordered_map<RE::FormID, Record> a_records);
	void Clear();
}
