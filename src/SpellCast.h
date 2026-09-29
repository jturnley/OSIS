#pragma once

// Scenes started by the player's magic (OStim NPCs' Matchmaker and the like). Such spells run a
// script effect on their targets, and the script starts the OStim scene a few seconds later.
// We watch the player's real casts (not abilities or cloaks, which apply effects without a
// cast) and where their script effects land; a scene whose actors were just hit that way was
// started by the spell.
namespace SpellCast
{
	void Init();   // kDataLoaded: event sinks
	void Tick();   // main thread heartbeat: advances the unpaused clock
	void Clear();  // game load: forget casts from the previous session

	// True when an NPC among a_actors received a script effect from a spell the player cast
	// within the last 30 s of unpaused play. Called once, when a new thread's actors are first
	// known. Logs what matched.
	[[nodiscard]] bool StartedBySpell(const std::vector<RE::Actor*>& a_actors);
}
