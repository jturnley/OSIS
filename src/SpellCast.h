#pragma once

// Scenes started by the player's magic (OStim NPCs' Matchmaker and the like). Such spells run a
// script effect on their targets, and the script starts the OStim scene a few seconds later.
// We watch the player's real casts (not abilities or cloaks, which apply effects without a
// cast) and where their script effects land; a scene whose actors were just hit that way was
// started by the spell.
//
// Matchmaker tags its targets one cast at a time and starts the scene only once the player has
// cast on everyone in it, so a later cast of a spell from the same plugin completes an older hit
// (up to 10 minutes). A spell-started thread that is stopped and restarted with the same NPCs
// (Followers Ask To Join adding a follower) stays spell-started. Attacks don't count: hostile
// effects and spells on an enemy fighting the player are ignored.
//
// Asking is not forcing: when the player talked with one of the scene's NPCs after the spell
// hit (the NPC came to ask, as in ODragonSeed, or the player asked them), the scene came out of
// that conversation and is not spell-started.
namespace SpellCast
{
	void Init();   // kDataLoaded: event sinks
	void Tick();   // main thread heartbeat: advances the unpaused clock, notes who the player is talking to
	void Clear();  // game load: forget casts and conversations from the previous session

	// A spell-started thread ended; a thread with one of a_npcs starting within 10 s, with no
	// conversation in between, continues it.
	void ThreadEnded(const std::vector<RE::FormID>& a_npcs);

	// True when an NPC among a_actors received a script effect from a spell the player cast
	// within the last 30 s of unpaused play (or earlier, completed by a later cast from the same
	// plugin within the last 30 s), and the player hasn't talked with any of them (Dialogue Menu)
	// since; or when the thread continues a spell-started one. Called once, when a new thread's
	// actors are first known. Logs what matched.
	[[nodiscard]] bool StartedBySpell(const std::vector<RE::Actor*>& a_actors);
}
