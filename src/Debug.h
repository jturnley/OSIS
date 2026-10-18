// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

#include "OStimData.h"

// The debug trace, OSIS-Debug.log, next to OSIS.log (General > Debug logging).
//
// OSIS.log stays what it was: the lines worth reading in a bug report. With debug logging on, this second file gets those same lines and everything else the
// plugin does or decides, one line each, tagged with a category in brackets:
//  [settings] the INI as loaded, and every change made in the menu          [scene]   threads, actors, nodes and the actions in them, tone, speed
//  [pers]     who has which personality, and why                            [face]    the Director's face for each actor: the phase, the pool, every channel
//  [breath]   the breath clock's beat (mouth and lids)                      [mouth]   lip-sync, the mouth hand-over to PPA, who holds the mouth
//  [moan]     the Director's own moans                                      [climax]  orgasms and the climax faces        [control] stalls and permits
//  [body]     curl, genital bend, head tilt, PPA claims                     [arousal] morphs and blush                    [skin]    overlays
//  [event]    OStim and SLED events in                                      [papyrus] calls out to OStim                  [snap]    a line per actor every second
// The [snap] lines are the answer to "what is this actor doing right now": personality, phase, excitement, who owns the face, the values the game is
// rendering, and a watch line: with debug logging on the face is watched frame by frame (what changed between OSIS's writes, jumps and snaps in the rendered
// face, what had to be put back; the old "Probe faces" buttons). Nothing is written, and almost nothing is computed, while debug logging is off.
//
// The file is not touched until debug logging is switched on, and the previous session's file is kept as OSIS-Debug.previous.log (once per launch), so the
// trace of a session that crashed is still there after the game is started again.
namespace Debug
{
	enum class Cat : int
	{
		Settings,
		Scene,
		Pers,
		Face,
		Breath,
		Mouth,
		Moan,
		Climax,
		Control,
		Body,
		Arousal,
		Skin,
		Event,
		Papyrus,
		Snap,
		Count
	};

	// The logger's second sink. It writes nothing until SetEnabled(true).
	[[nodiscard]] spdlog::sink_ptr MakeSink(const std::filesystem::path& a_dir);
	// Opens or closes the file and sets the log level. Safe from any thread.
	void SetEnabled(bool a_on);
	[[nodiscard]] bool On();
	[[nodiscard]] std::string Path();

	void Write(Cat a_cat, const std::string& a_text);

	// Formats and writes only when debug logging is on. Callers whose arguments are costly to build check On() first.
	template <class... A>
	void Log(Cat a_cat, std::format_string<A...> a_fmt, A&&... a_args)
	{
		if (!On()) return;
		Write(a_cat, std::format(a_fmt, std::forward<A>(a_args)...));
	}

	// Main thread, ~20 Hz from the scheduler: the [snap] lines.
	void Tick();
	// A scene was set up or torn down: say where everything stands, whatever the snapshot clock says.
	void SnapshotNow();
	// One line for an actor's RaceMenu overlay slots, body and face: which are in use, by whom (ours or another mod's) and which are free. a_why says what happened (a scene change).
	void OverlaySlots(RE::Actor* a_actor, std::string_view a_why);

	// ---------------------------------------------------------------- formatting helpers
	[[nodiscard]] const char* PhonemeName(int a_id);
	[[nodiscard]] const char* ModifierName(int a_id);
	[[nodiscard]] const char* ExpressionName(int a_id);
	// "Aah=.45 Oh=.20": the entries at or above a_min, named by a_name. Empty when none is.
	[[nodiscard]] std::string Channels(const float* a_values, int a_count, const char* (*a_name)(int), float a_min = 0.02f);
	// A Director preset ([0..15] phonemes, [16..29] modifiers, [30] mood, [31] strength) as text.
	[[nodiscard]] std::string Preset(const std::array<float, 32>& a_preset);
	// An actor named in a trace line. It is turned into "Name(0000ABCD)" only when the line is written, so a call costs next to nothing while debug logging is off.
	struct ActorRef
	{
		RE::FormID id = 0;
		RE::Actor* actor = nullptr;
	};
	[[nodiscard]] ActorRef Label(RE::Actor* a_actor);
	[[nodiscard]] std::string NameOf(const ActorRef& a_ref);
	// A scene node as text: its tags, the actions with who does what to whom, and what each actor's slot says.
	[[nodiscard]] std::string SceneText(const OStimData::ScenePtr& a_scene);
	[[nodiscard]] ActorRef Label(RE::FormID a_id);
}

template <>
struct std::formatter<Debug::ActorRef> : std::formatter<std::string>
{
	auto format(const Debug::ActorRef& a_ref, std::format_context& a_ctx) const
	{
		return std::formatter<std::string>::format(Debug::NameOf(a_ref), a_ctx);
	}
};
