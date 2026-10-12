// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// OStim's voice sets as data, and the choices OStim makes from them, reproduced so OSIS can make the same ones.
//
// What OStim does (OStimNG: Sound/SoundTable.cpp, Core/ThreadActor/ThreadActorSound.cpp):
//  1. Which set an actor has: the one picked for them in the MCM (or by a mod such as OSSO's auto-assign), else a set
//     whose target is their actor base, else their voice type, else their race, else the default male / female set.
//  2. Which sound: the entries of a list are tried in file order and the first whose condition (a perk's conditions,
//     tested with the actor and their partner) holds wins; an entry without a condition always holds. That is how a
//     "dynamic" set, with four moans for four speeds, picks its stage: the stage is the condition.
//  3. Which list: a muffled actor (an action that says so for their role, or the "muffle" actor property) uses the
//     muffled list ONLY. A set without one makes no sound for them.
//
// Nothing here plays or mutes anything. It parses, resolves and selects, and can say what it would do (Describe).
namespace VoiceModel
{
	struct Entry
	{
		RE::FormID sound = 0;      // BGSSoundDescriptorForm
		RE::FormID condition = 0;  // BGSPerk whose conditions decide; 0 = always
		std::string expression;
		float interval = 0.0f;     // "moanIntervalOverride" in seconds; 0 = OStim's global moan interval
		std::string info;          // the file's own label ("Speed Threshold 2"), for logs
	};

	struct List
	{
		std::vector<Entry> sound;
		std::vector<Entry> muffled;
		[[nodiscard]] bool empty() const { return sound.empty() && muffled.empty(); }
	};

	struct Set
	{
		std::string file;
		std::string name;      // translated, as OStim shows it in the MCM
		RE::FormID target = 0;
		std::vector<RE::FormID> aliases;
		List moan;             // plain moans: what OSIS would own
		List climax;
		std::vector<std::string> eventReactions;  // event ids with a reaction (spank, ...): left to OStim, listed for the log
		std::vector<RE::FormID> otherSounds;      // sound descriptors of everything that is NOT a plain moan: climax, comments, event reactions
		std::size_t dialogues = 0;                // dialogue entries anywhere in the set (topics, not sounds): left to OStim
		bool climaxDialogue = false;              // the climax list has dialogue: OStim speaks it in preference to the sound, so the whole climax stays with OStim
	};

	enum class Found
	{
		kNone,
		kAssigned,   // picked for this actor (MCM, OSSO auto-assign)
		kBase,
		kVoiceType,
		kRace,
		kDefault,
	};
	[[nodiscard]] const char* FoundName(Found a_how);

	struct Resolved
	{
		const Set* set = nullptr;
		Found how = Found::kNone;
	};

	struct Choice
	{
		const Entry* entry = nullptr;
		int index = -1;       // position in the list, which is the stage for a dynamic set
		bool muffled = false;
	};

	void OnDataLoaded();                          // parse every voice set, load translations
	[[nodiscard]] std::size_t SetCount();

	// Ask OStim which set this actor was given (async, once per few seconds at most). Cheap to call every tick.
	void Request(RE::Actor* a_actor);
	void Forget(RE::Actor* a_actor);              // the assignment may have changed: ask again next time

	// The set OStim would use. Until the answer to Request has arrived, an assigned set is not known and this falls
	// through to the actor / voice type / race / default rules.
	[[nodiscard]] Resolved Resolve(RE::Actor* a_actor);

	// OStim's rule for a list of entries: the first whose condition holds for the actor and partner (null partner allowed).
	[[nodiscard]] Choice Select(const List& a_list, bool a_muffled, RE::Actor* a_actor, RE::Actor* a_partner);

	// What the actor's scene says about their voice right now. known is false when the scene or its actions are not known, in which case
	// moan is true and muffled false (an actor is allowed to moan unless an action says otherwise). partner is the first other actor
	// in the scene, standing in for OStim's primary partner.
	struct SceneContext
	{
		bool inScene = false;
		bool known = false;
		bool moan = true;
		bool talk = true;
		bool muffled = false;
		RE::Actor* partner = nullptr;
	};
	[[nodiscard]] SceneContext Context(RE::Actor* a_actor);

	// What would happen for this actor now, in words: the set and how it was found, what the scene's actions allow
	// (moan / muffled), and the entry each list picks. Main thread. For the log and the menu.
	[[nodiscard]] std::string Describe(RE::Actor* a_actor);
}
