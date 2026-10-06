// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Face/Buildup.h"

#include "Face/Internal.h"

namespace Face::Buildup
{
	namespace
	{
		// A build-up face is a combination of four parts: the eyes (how far the lids close, where they look), the brows, the mouth
		// and the mood. Each part is a row below with how much of it shows at full intensity and how well it suits each
		// personality (none, stoic, bold, shy, fierce). The options further down are combinations of them, listed by stage: a plain
		// table, so adding or retuning one is a line. How much of an option shows is set when it is picked, by the stage and the
		// actor's excitement then (Build).
		struct Eyes
		{
			const char* name;
			float squint, blink, down, left, right, up;
			bool slowBlink;  // also blinks slowly now and then (a pulse, since the face is updated too seldom to draw one)
			float aff[5];
		};
		struct Brows
		{
			const char* name;
			float upL, upR, in, down;
			float aff[5];
		};
		struct Mouth
		{
			const char* name;
			float aah, bigAah, oh, ooh, eee, eh, bmp, th, w;
			float aff[5];
		};
		struct Mood
		{
			const char* name;
			int id;
			float strength;
			float aff[5];
		};
		struct Option
		{
			int stage, eyes, brows, mouth, mood;
			float bias;  // a little more or less of everything, so two options with the same parts are not identical
		};

		enum EyesId : int { EyesOpenSoft, EyesEyeContact, EyesWideAttentive, EyesSlowBlink, EyesHalfLid, EyesHeavyLidDown, EyesClosedSoft, EyesSqueezeLight, EyesSqueezeHard, EyesGlanceLeft, EyesGlanceRight, EyesLookUpDrift, EyesLookDownShy };
		enum BrowsId : int { BrowsNeutral, BrowsSoftRaise, BrowsHighRaise, BrowsPleading, BrowsKnitLight, BrowsFurrow, BrowsLowered, BrowsUnevenLeft, BrowsUnevenRight, BrowsWorried };
		enum MouthId : int { MouthClosedRelaxed, MouthLipsParted, MouthPartedWide, MouthOpen, MouthWideGasp, MouthRoundO, MouthOohPout, MouthPressed, MouthLipBite, MouthSmileClosed, MouthSmileOpen, MouthBreathOut, MouthTeethShow, MouthTongueTeeth };
		enum MoodId : int { MoodNeutral, MoodHappy, MoodSad, MoodSurprise, MoodPuzzled, MoodFear, MoodAnger };

		constexpr Eyes kEyes[] = {
			{ "open_soft", 0.08f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, { 1.00f, 1.30f, 0.80f, 1.00f, 1.00f } },
			{ "eye_contact", 0.12f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, { 1.00f, 1.20f, 1.20f, 0.30f, 1.80f } },
			{ "wide_attentive", 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, { 1.00f, 0.80f, 1.20f, 1.00f, 1.00f } },
			{ "slow_blink", 0.15f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, true, { 1.00f, 1.20f, 1.20f, 1.00f, 0.80f } },
			{ "half_lid", 0.40f, 0.20f, 0.00f, 0.00f, 0.00f, 0.00f, false, { 1.00f, 1.00f, 1.40f, 1.00f, 1.00f } },
			{ "heavy_lid_down", 0.30f, 0.35f, 0.20f, 0.00f, 0.00f, 0.00f, false, { 1.00f, 1.00f, 1.20f, 1.20f, 1.00f } },
			{ "closed_soft", 0.15f, 0.85f, 0.00f, 0.00f, 0.00f, 0.00f, false, { 1.00f, 1.00f, 1.00f, 1.50f, 0.70f } },
			{ "squeeze_light", 0.50f, 0.60f, 0.00f, 0.00f, 0.00f, 0.00f, false, { 1.00f, 1.00f, 1.00f, 1.20f, 1.00f } },
			{ "squeeze_hard", 0.85f, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, false, { 1.00f, 1.00f, 1.00f, 1.30f, 0.90f } },
			{ "glance_left", 0.12f, 0.00f, 0.00f, 0.60f, 0.00f, 0.00f, false, { 1.00f, 1.00f, 0.80f, 1.50f, 0.50f } },
			{ "glance_right", 0.12f, 0.00f, 0.00f, 0.00f, 0.60f, 0.00f, false, { 1.00f, 1.00f, 0.80f, 1.50f, 0.50f } },
			{ "look_up_drift", 0.20f, 0.10f, 0.00f, 0.00f, 0.00f, 0.50f, false, { 1.00f, 0.60f, 1.50f, 0.80f, 1.00f } },
			{ "look_down_shy", 0.15f, 0.15f, 0.55f, 0.00f, 0.00f, 0.00f, false, { 1.00f, 0.80f, 0.40f, 2.00f, 0.30f } },
		};

		constexpr Brows kBrows[] = {
			{ "neutral", 0.00f, 0.00f, 0.00f, 0.00f, { 1.00f, 1.80f, 0.60f, 0.70f, 1.00f } },
			{ "soft_raise", 0.25f, 0.25f, 0.00f, 0.00f, { 1.00f, 1.00f, 1.00f, 1.20f, 0.70f } },
			{ "high_raise", 0.60f, 0.60f, 0.00f, 0.00f, { 1.00f, 0.50f, 1.60f, 1.00f, 1.00f } },
			{ "pleading", 0.22f, 0.22f, 0.35f, 0.00f, { 1.00f, 0.50f, 1.00f, 2.00f, 0.30f } },
			{ "knit_light", 0.00f, 0.00f, 0.30f, 0.00f, { 1.00f, 1.20f, 1.00f, 1.00f, 1.20f } },
			{ "furrow", 0.00f, 0.00f, 0.45f, 0.25f, { 1.00f, 1.30f, 0.80f, 0.90f, 1.80f } },
			{ "lowered", 0.00f, 0.00f, 0.00f, 0.35f, { 1.00f, 1.20f, 0.60f, 0.40f, 2.00f } },
			{ "uneven_left", 0.50f, 0.12f, 0.00f, 0.00f, { 1.00f, 0.80f, 1.40f, 1.00f, 1.00f } },
			{ "uneven_right", 0.12f, 0.50f, 0.00f, 0.00f, { 1.00f, 0.80f, 1.40f, 1.00f, 1.00f } },
			{ "worried", 0.40f, 0.40f, 0.25f, 0.00f, { 1.00f, 0.80f, 0.80f, 1.60f, 0.50f } },
		};

		constexpr Mouth kMouth[] = {
			{ "closed_relaxed", 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.10f, 0.00f, 0.00f, { 1.00f, 2.00f, 0.40f, 1.20f, 1.00f } },
			{ "lips_parted", 0.15f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, { 1.00f, 1.30f, 1.00f, 1.00f, 1.00f } },
			{ "parted_wide", 0.30f, 0.10f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, { 1.00f, 0.80f, 1.20f, 1.00f, 1.00f } },
			{ "open", 0.45f, 0.20f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, { 1.00f, 0.60f, 1.80f, 0.80f, 1.20f } },
			{ "wide_gasp", 0.25f, 0.55f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, { 1.00f, 0.40f, 1.80f, 0.60f, 1.20f } },
			{ "round_o", 0.10f, 0.00f, 0.40f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, { 1.00f, 0.70f, 1.50f, 1.00f, 0.80f } },
			{ "ooh_pout", 0.00f, 0.00f, 0.00f, 0.35f, 0.00f, 0.00f, 0.00f, 0.00f, 0.10f, { 1.00f, 0.50f, 1.50f, 1.10f, 0.70f } },
			{ "pressed", 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.45f, 0.00f, 0.00f, { 1.00f, 1.80f, 0.50f, 1.30f, 1.20f } },
			{ "lip_bite", 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.05f, 0.45f, 0.15f, 0.00f, { 1.00f, 1.00f, 0.80f, 2.00f, 0.50f } },
			{ "smile_closed", 0.00f, 0.00f, 0.00f, 0.00f, 0.25f, 0.00f, 0.05f, 0.00f, 0.00f, { 1.00f, 0.90f, 1.30f, 1.00f, 1.00f } },
			{ "smile_open", 0.30f, 0.00f, 0.00f, 0.00f, 0.25f, 0.00f, 0.00f, 0.00f, 0.00f, { 1.00f, 0.60f, 1.80f, 0.70f, 0.90f } },
			{ "breath_out", 0.15f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.25f, 0.00f, { 1.00f, 1.30f, 1.00f, 1.00f, 1.00f } },
			{ "teeth_show", 0.10f, 0.00f, 0.00f, 0.00f, 0.40f, 0.00f, 0.00f, 0.00f, 0.00f, { 1.00f, 0.70f, 1.00f, 0.60f, 1.80f } },
			{ "tongue_teeth", 0.15f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.35f, 0.00f, { 1.00f, 0.60f, 1.40f, 0.80f, 1.00f } },
		};

		constexpr Mood kMoods[] = {
			{ "neutral", 7, 0.30f, { 1.00f, 1.80f, 0.50f, 0.80f, 1.00f } },
			{ "happy", 10, 0.90f, { 1.00f, 0.80f, 1.60f, 0.80f, 0.80f } },
			{ "sad", 11, 0.70f, { 1.00f, 0.80f, 0.60f, 1.80f, 0.40f } },
			{ "surprise", 12, 0.60f, { 1.00f, 0.70f, 1.20f, 1.30f, 0.60f } },
			{ "puzzled", 13, 0.60f, { 1.00f, 1.30f, 0.80f, 1.20f, 0.80f } },
			{ "fear", 9, 0.60f, { 1.00f, 0.80f, 0.50f, 1.80f, 0.40f } },
			{ "anger", 8, 0.45f, { 1.00f, 1.00f, 0.70f, 0.20f, 2.20f } },
		};

		// The options, by stage. 22 to a stage, chosen so that no two in a stage share more than two of their four parts where the
		// stage's palette allows it.
		constexpr Option kOptions[] = {
			// Anticipation
			{ kAnticipation, EyesGlanceRight, BrowsUnevenLeft, MouthBreathOut, MoodFear, -0.15f },
			{ kAnticipation, EyesEyeContact, BrowsWorried, MouthPressed, MoodHappy, 0.08f },
			{ kAnticipation, EyesHalfLid, BrowsPleading, MouthSmileClosed, MoodNeutral, -0.08f },
			{ kAnticipation, EyesOpenSoft, BrowsKnitLight, MouthLipsParted, MoodPuzzled, 0.15f },
			{ kAnticipation, EyesWideAttentive, BrowsUnevenRight, MouthLipBite, MoodSad, 0.00f },
			{ kAnticipation, EyesLookDownShy, BrowsNeutral, MouthSmileOpen, MoodHappy, -0.15f },
			{ kAnticipation, EyesGlanceLeft, BrowsSoftRaise, MouthClosedRelaxed, MoodHappy, 0.08f },
			{ kAnticipation, EyesSlowBlink, BrowsPleading, MouthBreathOut, MoodSad, -0.08f },
			{ kAnticipation, EyesOpenSoft, BrowsWorried, MouthSmileOpen, MoodNeutral, 0.15f },
			{ kAnticipation, EyesOpenSoft, BrowsUnevenLeft, MouthLipBite, MoodHappy, 0.00f },
			{ kAnticipation, EyesEyeContact, BrowsNeutral, MouthLipBite, MoodPuzzled, -0.15f },
			{ kAnticipation, EyesLookDownShy, BrowsUnevenLeft, MouthClosedRelaxed, MoodPuzzled, 0.08f },
			{ kAnticipation, EyesGlanceLeft, BrowsUnevenLeft, MouthPressed, MoodSad, -0.08f },
			{ kAnticipation, EyesHalfLid, BrowsUnevenRight, MouthBreathOut, MoodHappy, 0.15f },
			{ kAnticipation, EyesLookDownShy, BrowsKnitLight, MouthPressed, MoodFear, 0.00f },
			{ kAnticipation, EyesGlanceRight, BrowsPleading, MouthSmileOpen, MoodPuzzled, -0.15f },
			{ kAnticipation, EyesGlanceRight, BrowsSoftRaise, MouthLipBite, MoodNeutral, 0.08f },
			{ kAnticipation, EyesEyeContact, BrowsPleading, MouthClosedRelaxed, MoodFear, -0.08f },
			{ kAnticipation, EyesWideAttentive, BrowsWorried, MouthLipsParted, MoodFear, 0.15f },
			{ kAnticipation, EyesGlanceLeft, BrowsWorried, MouthSmileClosed, MoodPuzzled, 0.00f },
			{ kAnticipation, EyesGlanceLeft, BrowsUnevenRight, MouthLipsParted, MoodNeutral, -0.15f },
			{ kAnticipation, EyesGlanceRight, BrowsNeutral, MouthClosedRelaxed, MoodSad, 0.08f },
			// Warm
			{ kWarm, EyesOpenSoft, BrowsSoftRaise, MouthPartedWide, MoodNeutral, -0.08f },
			{ kWarm, EyesClosedSoft, BrowsPleading, MouthPressed, MoodHappy, 0.15f },
			{ kWarm, EyesGlanceRight, BrowsUnevenRight, MouthBreathOut, MoodSurprise, 0.00f },
			{ kWarm, EyesHeavyLidDown, BrowsKnitLight, MouthSmileClosed, MoodPuzzled, -0.15f },
			{ kWarm, EyesLookDownShy, BrowsWorried, MouthLipBite, MoodSad, 0.08f },
			{ kWarm, EyesSlowBlink, BrowsUnevenLeft, MouthTongueTeeth, MoodNeutral, -0.08f },
			{ kWarm, EyesGlanceLeft, BrowsNeutral, MouthSmileOpen, MoodNeutral, 0.15f },
			{ kWarm, EyesEyeContact, BrowsPleading, MouthLipsParted, MoodPuzzled, 0.00f },
			{ kWarm, EyesHalfLid, BrowsUnevenRight, MouthPartedWide, MoodHappy, -0.15f },
			{ kWarm, EyesLookUpDrift, BrowsNeutral, MouthLipBite, MoodHappy, 0.08f },
			{ kWarm, EyesLookDownShy, BrowsSoftRaise, MouthSmileClosed, MoodSurprise, -0.08f },
			{ kWarm, EyesGlanceLeft, BrowsWorried, MouthTongueTeeth, MoodPuzzled, 0.15f },
			{ kWarm, EyesLookUpDrift, BrowsSoftRaise, MouthTongueTeeth, MoodSad, 0.00f },
			{ kWarm, EyesLookDownShy, BrowsUnevenRight, MouthSmileOpen, MoodPuzzled, -0.15f },
			{ kWarm, EyesEyeContact, BrowsKnitLight, MouthPressed, MoodSad, 0.08f },
			{ kWarm, EyesSlowBlink, BrowsSoftRaise, MouthLipsParted, MoodHappy, -0.08f },
			{ kWarm, EyesLookUpDrift, BrowsKnitLight, MouthBreathOut, MoodNeutral, 0.15f },
			{ kWarm, EyesClosedSoft, BrowsUnevenLeft, MouthBreathOut, MoodPuzzled, 0.00f },
			{ kWarm, EyesGlanceRight, BrowsPleading, MouthLipBite, MoodNeutral, -0.15f },
			{ kWarm, EyesGlanceLeft, BrowsUnevenRight, MouthLipsParted, MoodSad, 0.08f },
			{ kWarm, EyesSlowBlink, BrowsNeutral, MouthPartedWide, MoodSurprise, -0.08f },
			{ kWarm, EyesGlanceRight, BrowsWorried, MouthSmileClosed, MoodHappy, 0.15f },
			// Rising
			{ kRising, EyesHeavyLidDown, BrowsFurrow, MouthBreathOut, MoodNeutral, 0.00f },
			{ kRising, EyesGlanceLeft, BrowsSoftRaise, MouthSmileOpen, MoodHappy, -0.15f },
			{ kRising, EyesGlanceRight, BrowsHighRaise, MouthPartedWide, MoodSurprise, 0.08f },
			{ kRising, EyesSlowBlink, BrowsUnevenRight, MouthOpen, MoodFear, -0.08f },
			{ kRising, EyesClosedSoft, BrowsPleading, MouthRoundO, MoodSad, 0.15f },
			{ kRising, EyesHalfLid, BrowsWorried, MouthOohPout, MoodHappy, 0.00f },
			{ kRising, EyesLookDownShy, BrowsUnevenLeft, MouthTongueTeeth, MoodSurprise, -0.15f },
			{ kRising, EyesLookUpDrift, BrowsKnitLight, MouthTeethShow, MoodFear, 0.08f },
			{ kRising, EyesSqueezeLight, BrowsFurrow, MouthLipBite, MoodSad, -0.08f },
			{ kRising, EyesLookDownShy, BrowsWorried, MouthBreathOut, MoodSad, 0.15f },
			{ kRising, EyesSlowBlink, BrowsWorried, MouthLipBite, MoodSurprise, 0.00f },
			{ kRising, EyesHalfLid, BrowsHighRaise, MouthTongueTeeth, MoodSad, -0.15f },
			{ kRising, EyesSlowBlink, BrowsKnitLight, MouthRoundO, MoodNeutral, 0.08f },
			{ kRising, EyesSqueezeLight, BrowsUnevenRight, MouthBreathOut, MoodHappy, -0.08f },
			{ kRising, EyesLookUpDrift, BrowsSoftRaise, MouthOpen, MoodNeutral, 0.15f },
			{ kRising, EyesLookUpDrift, BrowsFurrow, MouthOohPout, MoodSurprise, 0.00f },
			{ kRising, EyesLookDownShy, BrowsPleading, MouthOpen, MoodHappy, -0.15f },
			{ kRising, EyesGlanceRight, BrowsUnevenRight, MouthLipBite, MoodNeutral, 0.08f },
			{ kRising, EyesSqueezeLight, BrowsUnevenLeft, MouthPartedWide, MoodFear, -0.08f },
			{ kRising, EyesHalfLid, BrowsFurrow, MouthRoundO, MoodFear, 0.15f },
			{ kRising, EyesHeavyLidDown, BrowsUnevenRight, MouthOohPout, MoodSad, 0.00f },
			{ kRising, EyesGlanceLeft, BrowsUnevenLeft, MouthOohPout, MoodNeutral, -0.15f },
			// Heightened
			{ kHeightened, EyesHalfLid, BrowsUnevenLeft, MouthBreathOut, MoodSad, 0.08f },
			{ kHeightened, EyesGlanceRight, BrowsKnitLight, MouthSmileOpen, MoodSurprise, -0.08f },
			{ kHeightened, EyesSqueezeLight, BrowsFurrow, MouthRoundO, MoodAnger, 0.15f },
			{ kHeightened, EyesClosedSoft, BrowsPleading, MouthTeethShow, MoodFear, 0.00f },
			{ kHeightened, EyesGlanceLeft, BrowsWorried, MouthLipBite, MoodHappy, -0.15f },
			{ kHeightened, EyesLookUpDrift, BrowsHighRaise, MouthOohPout, MoodSad, 0.08f },
			{ kHeightened, EyesHeavyLidDown, BrowsLowered, MouthOpen, MoodAnger, -0.08f },
			{ kHeightened, EyesSqueezeHard, BrowsWorried, MouthWideGasp, MoodFear, 0.15f },
			{ kHeightened, EyesHeavyLidDown, BrowsFurrow, MouthPartedWide, MoodSad, 0.00f },
			{ kHeightened, EyesSqueezeHard, BrowsKnitLight, MouthLipBite, MoodAnger, -0.15f },
			{ kHeightened, EyesGlanceRight, BrowsUnevenLeft, MouthPartedWide, MoodFear, 0.08f },
			{ kHeightened, EyesSqueezeHard, BrowsPleading, MouthOpen, MoodSad, -0.08f },
			{ kHeightened, EyesGlanceLeft, BrowsUnevenLeft, MouthOohPout, MoodAnger, 0.15f },
			{ kHeightened, EyesGlanceLeft, BrowsHighRaise, MouthOpen, MoodFear, 0.00f },
			{ kHeightened, EyesLookUpDrift, BrowsUnevenLeft, MouthLipBite, MoodSurprise, -0.15f },
			{ kHeightened, EyesSqueezeLight, BrowsKnitLight, MouthWideGasp, MoodSad, 0.08f },
			{ kHeightened, EyesGlanceLeft, BrowsFurrow, MouthBreathOut, MoodSurprise, -0.08f },
			{ kHeightened, EyesSqueezeLight, BrowsPleading, MouthOohPout, MoodHappy, 0.15f },
			{ kHeightened, EyesHeavyLidDown, BrowsKnitLight, MouthOohPout, MoodFear, 0.00f },
			{ kHeightened, EyesHeavyLidDown, BrowsHighRaise, MouthSmileOpen, MoodHappy, -0.15f },
			{ kHeightened, EyesClosedSoft, BrowsKnitLight, MouthBreathOut, MoodHappy, 0.08f },
			{ kHeightened, EyesGlanceRight, BrowsWorried, MouthTeethShow, MoodSad, -0.08f },
			// Edge
			{ kEdge, EyesClosedSoft, BrowsHighRaise, MouthRoundO, MoodSad, 0.15f },
			{ kEdge, EyesEyeContact, BrowsKnitLight, MouthTeethShow, MoodAnger, 0.00f },
			{ kEdge, EyesSqueezeHard, BrowsPleading, MouthOpen, MoodNeutral, -0.15f },
			{ kEdge, EyesHalfLid, BrowsWorried, MouthWideGasp, MoodFear, 0.08f },
			{ kEdge, EyesLookUpDrift, BrowsLowered, MouthPressed, MoodSurprise, -0.08f },
			{ kEdge, EyesSqueezeLight, BrowsFurrow, MouthLipBite, MoodNeutral, 0.15f },
			{ kEdge, EyesHeavyLidDown, BrowsFurrow, MouthOohPout, MoodSad, 0.00f },
			{ kEdge, EyesHalfLid, BrowsPleading, MouthBreathOut, MoodSad, -0.15f },
			{ kEdge, EyesSqueezeLight, BrowsKnitLight, MouthPressed, MoodSad, 0.08f },
			{ kEdge, EyesSqueezeLight, BrowsPleading, MouthTeethShow, MoodFear, -0.08f },
			{ kEdge, EyesHalfLid, BrowsFurrow, MouthPressed, MoodAnger, 0.15f },
			{ kEdge, EyesLookUpDrift, BrowsWorried, MouthRoundO, MoodNeutral, 0.00f },
			{ kEdge, EyesEyeContact, BrowsLowered, MouthWideGasp, MoodNeutral, -0.15f },
			{ kEdge, EyesHeavyLidDown, BrowsLowered, MouthBreathOut, MoodAnger, 0.08f },
			{ kEdge, EyesClosedSoft, BrowsKnitLight, MouthOohPout, MoodFear, -0.08f },
			{ kEdge, EyesEyeContact, BrowsHighRaise, MouthBreathOut, MoodFear, 0.15f },
			{ kEdge, EyesSqueezeHard, BrowsWorried, MouthLipBite, MoodSad, 0.00f },
			{ kEdge, EyesEyeContact, BrowsWorried, MouthOpen, MoodSurprise, -0.15f },
			{ kEdge, EyesLookUpDrift, BrowsFurrow, MouthOpen, MoodFear, 0.08f },
			{ kEdge, EyesHalfLid, BrowsHighRaise, MouthOohPout, MoodNeutral, -0.08f },
			{ kEdge, EyesEyeContact, BrowsHighRaise, MouthLipBite, MoodSad, 0.15f },
			{ kEdge, EyesHeavyLidDown, BrowsPleading, MouthRoundO, MoodFear, 0.00f },
		};
		constexpr int kOptionCount = static_cast<int>(sizeof(kOptions) / sizeof(kOptions[0]));

		float Intensity(int stage, float excitement)
		{
			// How much of an option shows: it follows the excitement within its stage, and the edge is a little past full.
			static constexpr float lo[kStageCount] = { 0.0f, 25.0f, 50.0f, 75.0f, 85.0f };
			static constexpr float hi[kStageCount] = { 25.0f, 50.0f, 75.0f, 100.0f, 100.0f };
			static constexpr float from[kStageCount] = { 0.30f, 0.45f, 0.60f, 0.80f, 0.95f };
			static constexpr float to[kStageCount] = { 0.50f, 0.65f, 0.80f, 1.00f, 1.15f };
			const int s = std::clamp(stage, 0, kStageCount - 1);
			const float u = std::clamp((excitement - lo[s]) / (hi[s] - lo[s]), 0.0f, 1.0f);
			return from[s] + (to[s] - from[s]) * u;
		}

		float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
	}

	int OptionCount() { return kOptionCount; }

	int OptionCount(int stage)
	{
		int n = 0;
		for (const auto& o : kOptions) n += o.stage == stage ? 1 : 0;
		return n;
	}

	const char* StageName(int stage)
	{
		static constexpr const char* kNames[kStageCount] = { "anticipation", "warm", "rising", "heightened", "edge" };
		return kNames[std::clamp(stage, 0, kStageCount - 1)];
	}

	int StageFor(bool anticipation, bool plateau, int excitement)
	{
		if (anticipation) return kAnticipation;
		if (plateau) return kEdge;
		if (excitement < 50) return kWarm;
		if (excitement < 75) return kRising;
		return kHeightened;
	}

	int Pick(int stage, int arch, const int* recent, int recentCount)
	{
		const int a = std::clamp(arch, 0, 4);
		std::vector<std::pair<int, float>> candidates;
		float total = 0.0f;
		for (int pass = 0; pass < 2 && total <= 0.0f; ++pass) {
			candidates.clear();
			for (int i = 0; i < kOptionCount; ++i) {
				const auto& o = kOptions[i];
				if (o.stage != stage) continue;
				// Nothing seen in the last few picks; if that leaves nothing, anything in the stage.
				if (pass == 0 && std::find(recent, recent + recentCount, i) != recent + recentCount) continue;
				// How well the parts suit the personality, tempered: four parts multiply, and one very unlikely part
				// should lower an option, not remove it.
				const float w = std::sqrt(kEyes[o.eyes].aff[a] * kBrows[o.brows].aff[a] * kMouth[o.mouth].aff[a] * kMoods[o.mood].aff[a]);
				candidates.emplace_back(i, w);
				total += w;
			}
		}
		if (candidates.empty() || total <= 0.0f) return -1;
		float draw = Engine::detail::RandFloat(0.0f, total);
		for (const auto& [i, w] : candidates) {
			draw -= w;
			if (draw <= 0.0f) return i;
		}
		return candidates.back().first;
	}

	void Build(int option, float excitement, float mouthGate, Pose& e)
	{
		e = {};
		if (option < 0 || option >= kOptionCount) return;
		const auto& o = kOptions[option];
		const auto& E = kEyes[o.eyes];
		const auto& B = kBrows[o.brows];
		const auto& M = kMouth[o.mouth];
		const auto& Mo = kMoods[o.mood];
		const float i = Intensity(o.stage, excitement);
		const float k = std::clamp(1.0f + o.bias, 0.6f, 1.4f);
		const float mi = i * k * mouthGate;
		e[0] = Clamp01(M.aah * mi);
		e[1] = Clamp01(M.bigAah * mi);
		e[2] = Clamp01(M.bmp * (0.6f + 0.4f * i) * mouthGate);  // lips pressed together keep their hold at low intensity
		e[5] = Clamp01(M.eee * mi);
		e[6] = Clamp01(M.eh * mi);
		e[11] = Clamp01(M.oh * mi);
		e[12] = Clamp01(M.ooh * mi);
		e[14] = Clamp01(M.th * mi);
		e[15] = Clamp01(M.w * mi);
		e[18] = e[19] = Clamp01(B.down * i * k);
		e[20] = e[21] = Clamp01(B.in * i * k);
		e[22] = Clamp01(B.upL * i * k);
		e[23] = Clamp01(B.upR * i * k);
		e[16] = e[17] = Clamp01(E.blink * (0.7f + 0.3f * i));
		e[24] = Clamp01(E.down * (0.6f + 0.4f * i));
		e[25] = Clamp01(E.left * (0.6f + 0.4f * i));
		e[26] = Clamp01(E.right * (0.6f + 0.4f * i));
		e[27] = Clamp01(E.up * (0.6f + 0.4f * i));
		e[28] = e[29] = Clamp01(E.squint * (0.5f + 0.5f * i) * k);
		e[30] = static_cast<float>(Mo.id);
		e[31] = Clamp01(Mo.strength * i * k);
	}

	std::string Name(int option)
	{
		if (option < 0 || option >= kOptionCount) return "none";
		const auto& o = kOptions[option];
		return std::string(StageName(o.stage)) + ":" + kEyes[o.eyes].name + "+" + kBrows[o.brows].name + "+" + kMouth[o.mouth].name + "+" + kMoods[o.mood].name;
	}

	bool SlowBlink(int option)
	{
		return option >= 0 && option < kOptionCount && kEyes[kOptions[option].eyes].slowBlink;
	}
}
