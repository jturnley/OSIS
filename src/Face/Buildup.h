// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// The Director's own faces for the build-up to an orgasm, to play beside OStim's expression pools so that nothing looks like it is
// repeating: 22 of them in each of five stages, each a combination of eyes, brows, mouth and mood (Buildup.cpp has the table).
//
// Stages: anticipation (under 25 excitement, or the lead-in), warm (25-49), rising (50-74), heightened (75 and over) and the edge
// (held near the peak: the plateau). An option is picked now and then, weighted by how well its parts suit the personality and never
// one of the last few; how much of it shows is set at the pick, by the stage and the excitement.
namespace Face::Buildup
{
	enum Stage : int
	{
		kAnticipation,
		kWarm,
		kRising,
		kHeightened,
		kEdge,
		kStageCount
	};

	using Pose = std::array<float, 32>;  // the Director's preset layout: phonemes 0-15, modifiers 16-29, mood id 30, mood strength 31

	[[nodiscard]] int OptionCount();
	[[nodiscard]] int OptionCount(int a_stage);
	[[nodiscard]] const char* StageName(int a_stage);
	[[nodiscard]] int StageFor(bool a_anticipation, bool a_plateau, int a_excitement);
	// An option of the stage for this personality, other than any of the `a_recentCount` in `a_recent`; -1 if there is none.
	[[nodiscard]] int Pick(int a_stage, int a_arch, const int* a_recent, int a_recentCount);
	// The face for an option at this excitement. `a_mouthGate` is the Director's mouth gate (0 leaves the mouth closed).
	void Build(int a_option, float a_excitement, float a_mouthGate, Pose& a_out);
	[[nodiscard]] std::string Name(int a_option);
	// Whether the option's eyes blink slowly now and then.
	[[nodiscard]] bool SlowBlink(int a_option);
}
