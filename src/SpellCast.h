#pragma once

// Scenes started by the player's magic (OStim NPCs' Matchmaker and the like). Such spells run a
// script effect on their targets, and the script starts the OStim scene a few seconds later.
// We watch the player's real casts (not abilities or cloaks, which apply effects without a
// cast) and where their script effects land; a scene whose actors were just hit that way was
// started by the spell.
//
// The NPCs the spell hit are its victims. Anyone else in the scene (a follower who asked to join)
// came of their own accord.
//
// Matchmaker tags its targets one cast at a time and starts the scene only once the player has
// cast on everyone in it, so a later cast of a spell from the same plugin completes an older hit
// (up to 10 minutes). A spell-started thread that is stopped and restarted with its victims
// (Followers Ask To Join adding a follower) stays spell-started. Attacks don't count: hostile
// effects and spells on an enemy fighting the player are ignored.
//
// Asking is not forcing: an NPC who talked with the player after the spell hit them (they came
// to ask, as in ODragonSeed, or the player asked them) is no victim; the scene came out of that
// conversation.
namespace SpellCast
{
	void Init();   // kDataLoaded: event sinks
	void Tick();   // main thread heartbeat: advances the unpaused clock, notes who the player is talking to
	void Clear();  // game load: forget casts and conversations from the previous session

	// A spell-started thread ended. A thread starting within 10 s with one of a_victims, who hasn't
	// talked with the player in between, continues it: they are still victims.
	void ThreadEnded(const std::vector<RE::FormID>& a_victims);

	// The spell's victims among a_actors' NPCs; empty when the thread is not spell-started. A
	// victim received a script effect from a spell the player cast within the last 30 s of
	// unpaused play (or earlier, completed by a later cast from the same plugin within the last
	// 30 s) and hasn't talked with the player (Dialogue Menu) since, or is a victim of a
	// spell-started thread this one continues. Called once, when a new thread's actors are first
	// known. Logs victims and everyone else.
	[[nodiscard]] std::vector<RE::FormID> StartedBySpell(const std::vector<RE::Actor*>& a_actors);
}
