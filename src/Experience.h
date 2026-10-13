// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// What an actor has been through, and what it makes of them. A session is one scene an actor was in; what it counts is the orgasms the actor had in it.
// Two kinds of change:
//  - A personality that can grow has a rule (Settings::Evolution): so many sessions with so many orgasms each, and the actor becomes another personality
//    for good (stoic to balanced, shy to timid, timid to balanced, vocal to wild).
//  - Obsession: so many sessions of a great many orgasms with the same partner, and the actor is crazed about that one partner, in every scene they are in
//    together, and nobody else. Their own personality is untouched.
// Only a personality OSIS settled on by itself grows or becomes obsessed: what the player chose, and the player's own, stays. The counts are kept in the save
// (Serialization, record EXPR).
namespace Experience
{
	struct Record
	{
		std::int32_t pers = -1;       // the personality the counts below belong to; a different one starts them again
		std::uint32_t qualified = 0;  // sessions that counted toward that personality's own rule
		std::uint32_t sessions = 0;   // every session, for the log
		std::uint32_t orgasms = 0;    // every orgasm in them
		RE::FormID obsessedWith = 0;  // the partner they are crazed about (0: nobody)
		// Sessions of at least the obsession rule's orgasms, by who the other actors in them were: the partner who gets to the rule's number of them first is the one.
		std::vector<std::pair<RE::FormID, std::uint32_t>> partners;
	};

	struct Rule
	{
		int sessions = 0;  // sessions that must qualify
		int orgasms = 0;   // orgasms a session needs to qualify
		int target = -1;   // the personality they become; -1: this personality does not grow
	};

	struct Result
	{
		int next = -1;                // the personality they become, or -1
		RE::FormID obsessedWith = 0;  // a partner they have just become obsessed with, or 0
		std::string why;
	};

	// The rule of a personality, from the settings. The caller holds Settings::lock.
	[[nodiscard]] Rule RuleFor(int a_pers);
	[[nodiscard]] Rule ObsessionRule();  // `target` is the crazed personality they play with that partner

	// A scene is over for this actor, who had `a_orgasms` orgasms in it as `a_pers`, with these other actors. The caller holds Settings::lock.
	[[nodiscard]] Result OnSessionEnd(RE::FormID a_id, int a_pers, int a_orgasms, const std::vector<RE::FormID>& a_partners);

	// A partner is made the one this actor is crazed about without the sessions it normally takes (Virginity: a first time that ended that way).
	void BecomeObsessed(RE::FormID a_id, RE::FormID a_partner);
	// The partner this actor is obsessed with, or 0.
	[[nodiscard]] RE::FormID ObsessedWith(RE::FormID a_id);
	// Whether the actor is in a scene with them right now (set by the scene code), so that they play crazed: Face::Engine::Archetype asks.
	void SetObsessedNow(RE::FormID a_id, bool a_now);
	[[nodiscard]] bool IsObsessedNow(RE::FormID a_id);

	// How far along an actor is, for the debug trace and the menu. Takes Settings::lock unless the caller already holds it.
	[[nodiscard]] std::string Describe(RE::FormID a_id, int a_pers, bool a_lockHeld = false);
	[[nodiscard]] std::string Status();

	// The cosave.
	[[nodiscard]] std::unordered_map<RE::FormID, Record> Snapshot();
	void Restore(std::unordered_map<RE::FormID, Record> a_records);
	void Clear();
}
