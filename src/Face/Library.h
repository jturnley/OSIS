// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

#include "OStimData.h"

// OStim's facial expression library, read from the same folder and chosen by the same rules, so
// every installed expression pack applies. OStim plays these through its own writer; the Director
// uses the data and its own seamless player instead (the writer is switched off for it).
//
// The files are Data/SKSE/Plugins/OStim/facial expressions/*.json. Each has a female and a male
// variant and says where it applies: `sets` (named pools such as "default", "openmouth", "tongue"),
// `events` ("moan", "climax", "kiss"...), and `actionActors` / `actionTargets` (the action an actor
// is performing or receiving). A scene node picks per actor, in this order (Node.cpp in OStim):
//   underlying: the actor's `underlyingExpression` set, else the action named by `expressionAction`,
//               else the first action (target role first, then actor) that has a pool, else "default".
//   override:   the actor's `expressionOverride` set, else the first action role (actor, target,
//               performer) whose action definition names an `expressionOverride` set.
// Both choose a pool; OStim then plays a random member of it every few seconds.
namespace Face::Library
{
	// What an expression sets. The numbers are OStim's own (and Skyrim's MFG ids): moods are
	// expression ids, lids {0,1,12,13}, brows {2..7}, eyeballs {8..11}.
	enum Part : int
	{
		kMood = 1 << 0,
		kPhoneme = 1 << 1,
		kLid = 1 << 2,
		kBrow = 1 << 3,
		kBall = 1 << 4,
	};

	// One value in an expression, with OStim's formula:
	//   base + random(0..variance) + speedMult * speed + excitementMult * excitement, clamped to 0..100.
	struct Channel
	{
		int type = 0;
		float base = 0.0f;
		float variance = 0.0f;
		float speedMult = 0.0f;
		float excitementMult = 0.0f;
		int delay = 0;          // ms before it starts moving
		int delayVariance = 0;  // plus up to this much, random
		std::string faction;    // raw JSON of a faction condition (Devious Devices gags); not evaluated yet
		int factionFallback = 0;

		// 0..1. `roll` in 0..1 stands in for OStim's random draw.
		[[nodiscard]] float Value(float a_excitement, float a_speed, float a_roll) const
		{
			const float v = base + variance * std::clamp(a_roll, 0.0f, 1.0f) + speedMult * a_speed + excitementMult * a_excitement;
			return std::clamp(v, 0.0f, 100.0f) / 100.0f;
		}
	};

	// One gender's version of an expression.
	struct Variant
	{
		bool defined = false;  // the file has this gender at all; OStim plays nothing for one it lacks
		float duration = 0.5f; // seconds an event expression is held
		int parts = 0;         // Part bits it touches
		Channel mood;
		std::vector<Channel> lids, brows, balls, phonemes;
		float objectThreshold = -5.0f;      // phoneme value at which the objects are equipped
		std::vector<std::string> objects;   // "tongue" and the like
	};

	struct Expression
	{
		std::string file;
		Variant female;
		Variant male;
		[[nodiscard]] const Variant& For(bool a_female) const { return a_female ? female : male; }
	};

	using Pool = std::vector<const Expression*>;

	// A face as the Director builds it: [0..15] phonemes, [16 + id] modifiers, [30] mood id (-1: none yet), [31] mood strength.
	using State = std::array<float, 32>;
	[[nodiscard]] State EmptyState();

	// Apply one expression to a retained face the way OStim's applyExpression does. Each part the
	// expression has is set in full - a channel it does not list goes to zero - and a part it lacks is
	// left as it was, so the face is the accumulation of recent picks. `a_roll` supplies the 0..1 draw
	// for each channel's variance; excitement is 0..100 and a_relSpeed 0..1 (the node's speed index
	// over its count of speeds), as OStim passes them.
	// `a_skipParts` (Part bits) are left alone even when the expression has them: the parts an override owns.
	void ApplyTo(State& a_state, const Variant& a_variant, float a_excitement, float a_relSpeed, const std::function<float()>& a_roll,
		int a_skipParts = 0);
	// Set the channels of the given parts to rest (the mood to a neutral at zero), or copy them from another state.
	void ZeroParts(State& a_state, int a_parts);
	void CopyParts(State& a_dst, const State& a_src, int a_parts);

	// kDataLoaded, after OStimData::Init (action aliases are needed to read the action keys).
	void Load();

	[[nodiscard]] const Pool* Set(std::string_view a_name);
	[[nodiscard]] const Pool* Event(std::string_view a_name);
	[[nodiscard]] const Pool* ActionActor(std::string_view a_action);
	[[nodiscard]] const Pool* ActionTarget(std::string_view a_action);

	struct Resolved
	{
		const Pool* underlying = nullptr;  // never null once loaded: falls back to "default"
		const Pool* override = nullptr;    // null when nothing overrides
		std::string underlyingWhy;         // how the pool was chosen, for logs
		std::string overrideWhy;
	};

	// A scene node's pools for the actor at a position, by OStim's rules above.
	[[nodiscard]] Resolved Resolve(const OStimData::Scene& a_scene, int a_pos);

	// "underlying 'blowjob' (target, 10), override 'openmouth' (action blowjob, 6)" - for the probe.
	[[nodiscard]] std::string Describe(const OStimData::Scene& a_scene, int a_pos);

	struct Stats
	{
		std::size_t files = 0;
		std::size_t sets = 0;
		std::size_t events = 0;
		std::size_t actionActors = 0;
		std::size_t actionTargets = 0;
	};
	[[nodiscard]] Stats GetStats();
}
