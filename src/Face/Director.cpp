// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

// The Director: OSExpressionFaces.ApplyArc and its helpers. In OSED 2.0 this was the
// dormant "mode 2" face writer (it fought OStim's face updater on a 3 s Papyrus tick and
// every eye/brow write went to a wrong Mfg id). With per-frame output it is the default.

#include "Face/Internal.h"

#include "Face/Buildup.h"

#include "Papyrus.h"
#include "Pulse.h"

namespace Face::Engine::detail
{
	using namespace Scenes;
	using Preset = std::array<float, 32>;

	namespace
	{
		void Add(Preset& e, int i, float d, float hi = 1.0f) { e[i] = ClampF(e[i] + d, 0.0f, hi); }
		void Add2(Preset& e, int i, float d) { Add(e, i, d), Add(e, i + 1, d); }
		void Mul(Preset& e, int i, float k) { e[i] = e[i] * k; }

		Preset Build(int mood, float moodStr, float aah, float oh, float squint, float browUp, float browIn, float browDown)
		{
			Preset e{};
			e[0] = aah;
			e[11] = oh;
			e[18] = e[19] = browDown;
			e[20] = e[21] = browIn;
			e[22] = e[23] = browUp;
			e[28] = e[29] = squint;
			e[30] = static_cast<float>(mood);
			e[31] = moodStr;
			return e;
		}

		float RoleSq(int enj) { return ClampF(static_cast<float>(enj) / 400.0f, 0.0f, 0.25f); }

		int ArchTempo(int arch)
		{
			if (arch == 2) return 12;
			if (arch == 1) return -12;
			return 0;
		}

		float ArchStrength(int arch)
		{
			if (arch == 1) return 0.7f;
			if (arch == 2) return 1.25f;
			if (arch == 4) return 1.1f;
			return 1.0f;
		}

		// ---- the climax clips
		// One climax template made every orgasm look the same. Fifteen faces now, each a distinct combination of eyes (shut tight,
		// narrowed, rolled up, wide), mouth (stretched, round, parted, clenched, pressed) and brows (knitted, lifted, lowered) with a
		// mood to match, and each played as a clip over the length of the orgasm rather than as one held pose. The pick is made once per
		// orgasm (PickClimaxClip), by personality (`suits`) and by whether the orgasms are coming quickly (`intense` ones favoured, `calm`
		// ones held back), never the one before, and the orgasm is long or short too: a standard orgasm (alone, or well after the last)
		// or a rapid one (soon after it), each in a long and a short form, so a face has four clips. The values are the pose at the peak;
		// the pipeline after it (eye scalar, strength) shapes them like any other face.
		enum Settle : int { kSoften, kSmile, kSlack, kHold };  // where the face goes as the orgasm ends: loosens, smiles, goes slack, stays tight

		// What the eyelids do, from not at all to hard shut. Squint narrows the eyes, but only Blink closes the lids, so a face that
		// shuts its eyes writes both: the closure goes from a slow blink that comes and goes (languid), through lids held half down
		// (heavy), shut and still (closed), shut and trembling (flutter), shut with a squeeze (tight), to squeezed as hard as it goes (hard).
		enum Eyes : int { kOpen, kLanguid, kHeavy, kClosed, kFlutter, kTight, kHard };

		struct ClimaxVariant
		{
			const char* name;
			int mood;
			float moodStr;
			float aah, bigAah, oh, ooh, eee, eh, bmp, th;  // phonemes 0, 1, 11, 12, 5, 6, 2, 14
			float squint, browUp, browIn, browDown, lookUp;
			bool wide;     // eyes wide: no squeeze at the start
			bool calm;
			bool intense;
			Settle settle;
			Eyes eyes;
			float tremor;    // how much the face ripples through the aftershocks, 0..1
			float longBias;  // added to the chance this face runs long
			float suits[5];  // weight by personality: none, stoic, bold, shy, fierce
		};

		constexpr ClimaxVariant kClimaxVariants[] = {
			{ "gasp", 10, 0.35f, 0.70f, 0.55f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.85f, 0.15f, 0.55f, 0.00f, 0.00f,
				false, false, true, kSoften, kHard, 0.60f, -0.10f, { 1.00f, 0.80f, 1.20f, 1.00f, 0.90f } },
			{ "cry_out", 12, 0.50f, 0.35f, 0.20f, 0.75f, 0.30f, 0.00f, 0.00f, 0.00f, 0.00f, 0.30f, 0.70f, 0.00f, 0.00f, 0.60f,
				false, false, false, kSoften, kHeavy, 0.50f, 0.05f, { 1.00f, 0.30f, 2.00f, 0.60f, 1.20f } },
			{ "clenched", 8, 0.30f, 0.10f, 0.00f, 0.00f, 0.00f, 0.55f, 0.00f, 0.20f, 0.00f, 0.90f, 0.00f, 0.60f, 0.35f, 0.00f,
				false, false, true, kHold, kHard, 0.70f, 0.00f, { 1.00f, 2.00f, 0.40f, 1.20f, 1.60f } },
			{ "lip_bite", 11, 0.35f, 0.08f, 0.00f, 0.00f, 0.00f, 0.00f, 0.10f, 0.55f, 0.20f, 0.60f, 0.25f, 0.40f, 0.00f, 0.00f,
				false, false, false, kSoften, kTight, 0.30f, 0.00f, { 1.00f, 1.20f, 0.30f, 2.20f, 0.30f } },
			{ "silent", 7, 0.30f, 0.15f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.30f, 0.10f, 0.20f, 0.00f, 0.00f,
				false, true, false, kSlack, kLanguid, 0.20f, -0.20f, { 1.00f, 2.40f, 0.20f, 1.40f, 0.50f } },
			{ "wide_eyed", 12, 1.00f, 0.30f, 0.00f, 0.55f, 0.15f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 1.00f, 0.00f, 0.00f, 0.00f,
				true, false, false, kSmile, kOpen, 0.40f, -0.10f, { 1.00f, 0.60f, 0.80f, 2.00f, 0.40f } },
			{ "eyes_rolled", 10, 0.30f, 0.55f, 0.20f, 0.00f, 0.10f, 0.00f, 0.00f, 0.00f, 0.00f, 0.35f, 0.45f, 0.00f, 0.00f, 0.85f,
				false, false, false, kSlack, kHeavy, 0.50f, 0.10f, { 1.00f, 0.30f, 1.80f, 0.60f, 1.00f } },
			{ "smiling", 10, 0.85f, 0.45f, 0.00f, 0.00f, 0.00f, 0.30f, 0.00f, 0.00f, 0.00f, 0.75f, 0.20f, 0.00f, 0.00f, 0.00f,
				false, false, false, kSmile, kClosed, 0.30f, 0.10f, { 1.00f, 0.50f, 1.80f, 0.50f, 0.80f } },
			{ "intense", 8, 0.35f, 0.25f, 0.00f, 0.00f, 0.00f, 0.10f, 0.00f, 0.00f, 0.00f, 0.55f, 0.00f, 0.35f, 0.45f, 0.00f,
				false, false, false, kHold, kOpen, 0.20f, 0.05f, { 1.00f, 1.40f, 0.60f, 0.30f, 2.20f } },
			{ "long_moan", 11, 0.40f, 0.20f, 0.65f, 0.00f, 0.50f, 0.00f, 0.00f, 0.00f, 0.00f, 0.50f, 0.35f, 0.45f, 0.00f, 0.00f,
				false, false, false, kSoften, kClosed, 0.40f, 0.25f, { 1.00f, 0.20f, 2.20f, 0.50f, 0.80f } },
			{ "overwhelmed", 9, 0.45f, 0.60f, 0.40f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.70f, 0.30f, 0.65f, 0.00f, 0.00f,
				false, false, true, kHold, kFlutter, 0.90f, 0.00f, { 1.00f, 0.60f, 1.00f, 1.80f, 0.80f } },
			{ "pained", 11, 0.55f, 0.35f, 0.00f, 0.00f, 0.00f, 0.15f, 0.35f, 0.00f, 0.00f, 0.70f, 0.55f, 0.60f, 0.00f, 0.00f,
				false, false, true, kSoften, kTight, 0.60f, 0.00f, { 1.00f, 1.20f, 0.50f, 1.80f, 0.50f } },
			{ "breathless", 7, 0.30f, 0.35f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.15f, 0.20f, 0.25f, 0.00f, 0.00f, 0.25f,
				false, true, false, kSlack, kLanguid, 0.30f, -0.10f, { 1.00f, 1.60f, 0.60f, 1.40f, 0.60f } },
			{ "snarl", 14, 0.45f, 0.30f, 0.00f, 0.00f, 0.00f, 0.40f, 0.00f, 0.00f, 0.00f, 0.75f, 0.00f, 0.30f, 0.55f, 0.00f,
				false, false, false, kHold, kOpen, 0.40f, 0.00f, { 1.00f, 0.40f, 0.80f, 0.10f, 2.20f } },
			{ "laughing", 10, 0.70f, 0.65f, 0.25f, 0.00f, 0.00f, 0.20f, 0.00f, 0.00f, 0.00f, 0.70f, 0.50f, 0.00f, 0.00f, 0.00f,
				false, false, false, kSmile, kFlutter, 0.80f, 0.05f, { 1.00f, 0.20f, 1.80f, 0.60f, 0.60f } },
		};
		constexpr int kClimaxVariantCount = static_cast<int>(sizeof(kClimaxVariants) / sizeof(kClimaxVariants[0]));

		enum ClimaxKind : int { kStdLong = 0, kStdShort = 1, kRapidLong = 2, kRapidShort = 3 };

		const char* ClimaxKindName(int kind)
		{
			switch (kind) {
			case kStdLong: return "long";
			case kStdShort: return "short";
			case kRapidLong: return "rapid-long";
			case kRapidShort: return "rapid-short";
			default: return "template";
			}
		}

		int PickClimaxVariant(const Slot& s, int arch)
		{
			const bool rapid = s.rapidRun >= 1;
			float weight[kClimaxVariantCount];
			float total = 0.0f;
			for (int i = 0; i < kClimaxVariantCount; ++i) {
				const auto& v = kClimaxVariants[i];
				float w = v.suits[ClampI(arch, 0, 4)];
				if (rapid) w *= v.intense ? 1.8f : (v.calm ? 0.5f : 1.0f);
				// A change, always, and the one before that is held back too: with only the last excluded, a face the personality favours
				// came back every other orgasm (long moan and gasp alternated five times running in the 2.0.5 test).
				if (i == s.lastClimaxVariant) w = 0.0f;
				else if (i == s.lastClimaxVariant2) w *= 0.25f;
				weight[i] = w;
				total += w;
			}
			if (total <= 0.0f) return RandInt(0, kClimaxVariantCount - 1);
			float draw = RandFloat(0.0f, total);
			for (int i = 0; i < kClimaxVariantCount; ++i) {
				if (weight[i] <= 0.0f) continue;
				draw -= weight[i];
				if (draw <= 0.0f) return i;
			}
			return kClimaxVariantCount - 1;
		}

		float Lerp(float a, float b, float t) { return a + (b - a) * t; }
		float Smooth(float t) { t = ClampF(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

		// How far the lids are closed at this moment of the clip, 0..1, for a face's eyes style. `f` follows the clip's intensity, so the lids
		// stay as they are until the face starts to settle and open with it; a rapid clip never opens them fully, the next orgasm is coming.
		float BlinkAmount(Eyes style, float age, float k, float tremor, bool rapid)
		{
			float f = Smooth(k / 0.6f);
			if (rapid) f = 0.6f + 0.4f * f;
			switch (style) {
			case kLanguid: {
				// slow blinks: down over a second, held, up over a second, then open for a while
				const float period = 3.4f;
				const float ph = std::fmod(age, period) / period;
				const float wave = ph < 0.30f ? Smooth(ph / 0.30f) : (ph < 0.42f ? 1.0f : (ph < 0.72f ? 1.0f - Smooth((ph - 0.42f) / 0.30f) : 0.0f));
				return 0.85f * wave * f;
			}
			case kHeavy:
				return (0.50f + 0.08f * std::sin(6.2831853f * age / 4.5f)) * f;  // lids half down, drifting slowly
			case kClosed:
				return 0.95f * Smooth(age / 0.6f) * f;
			case kFlutter:
				// shut, the lids trembling. The face is updated about every 0.8 s, so the period is not a multiple of that: a period of
				// 1.6 s would be sampled at the same point of the wave every time and not tremble at all.
				return (0.78f + (0.10f + 0.12f * tremor) * std::sin(6.2831853f * age / 1.3f)) * Smooth(age / 0.5f) * f;
			case kTight:
				return Smooth(age / 0.4f) * f;
			case kHard:
				return Smooth(age / 0.3f) * f;
			default:
				return 0.0f;
			}
		}

		// The face at the peak of an orgasm: the variant's pose.
		Preset PeakPose(const ClimaxVariant& v, float m)
		{
			Preset e{};
			e[0] = v.aah * m;
			e[1] = v.bigAah * m;
			e[2] = v.bmp * m;
			e[5] = v.eee * m;
			e[6] = v.eh * m;
			e[11] = v.oh * m;
			e[12] = v.ooh * m;
			e[14] = v.th * m;
			e[18] = e[19] = v.browDown;
			e[20] = e[21] = v.browIn;
			e[22] = e[23] = v.browUp;
			e[27] = v.lookUp;
			e[28] = e[29] = v.squint;
			e[30] = static_cast<float>(v.mood);
			e[31] = v.moodStr;
			return e;
		}

		// Where the face is heading as the orgasm ends. After a standard orgasm it settles, the way this variant settles (an afterglow
		// follows). After a rapid one it stays tight, because the next orgasm is already coming.
		Preset RestPose(const ClimaxVariant& v, const Preset& p, bool rapid, float m)
		{
			Preset r{};
			switch (rapid ? kHold : v.settle) {
			case kSoften:
				for (int i = 0; i <= 15; ++i) r[i] = p[i] * 0.35f;
				r[18] = r[19] = p[18] * 0.35f;
				r[20] = r[21] = p[20] * 0.35f;
				r[22] = r[23] = p[22] * 0.5f;
				r[28] = r[29] = p[28] * 0.55f;
				r[30] = p[30];
				r[31] = p[31] * 0.5f;
				break;
			case kSmile:
				r[0] = 0.15f * m;
				r[22] = r[23] = 0.10f;
				r[28] = r[29] = 0.40f;
				r[30] = 10.0f;
				r[31] = 0.5f;
				break;
			case kSlack:
				r[0] = 0.12f * m;
				r[28] = r[29] = 0.50f;
				r[30] = 7.0f;
				r[31] = 0.3f;
				break;
			case kHold:
				for (int i = 0; i <= 29; ++i) r[i] = p[i] * 0.7f;
				r[30] = p[30];
				r[31] = p[31] * 0.8f;
				break;
			}
			return r;
		}

		// The timeline of a clip, as fractions of its length: the squeeze at the start ends at `onset` (none for a rapid orgasm, which starts
		// already tight), the full peak holds to `peak`, the aftershocks ripple to `ripple` (none when it is the same as `peak`), and the
		// face then eases to its rest pose.
		struct ClipShape
		{
			float onset, peak, ripple;
			int ripples;
		};

		constexpr ClipShape ShapeOf(int kind)
		{
			switch (kind) {
			case kStdLong: return { 0.10f, 0.40f, 0.72f, 3 };
			case kStdShort: return { 0.12f, 0.50f, 0.50f, 0 };
			case kRapidLong: return { 0.00f, 0.35f, 0.70f, 2 };
			default: return { 0.00f, 0.45f, 0.45f, 0 };
			}
		}

		// The pick for this orgasm, once, at the orgasm: the face, and whether it runs long or short as a standard or rapid orgasm. Long is
		// likelier for a bold actor, after a long hold at the edge and for faces that suit it (a long moan); a rapid orgasm is likelier short
		// the closer it comes to the last.
		bool ClimaxVariantPose(Thread& t, Slot& s, int arch, float m, Preset& e)
		{
			if (s.climaxVariant < 0 || s.climaxVariantAt != s.climaxStart || s.climaxKind < 0) PickClimaxClip(t, s, arch);
			const auto& v = kClimaxVariants[ClampI(s.climaxVariant, 0, kClimaxVariantCount - 1)];
			const bool rapid = s.climaxKind == kRapidLong || s.climaxKind == kRapidShort;
			const float length = std::max(0.5f, s.climaxUntil - s.climaxStart);
			const float u = ClampF((Scenes::Now() - s.climaxStart) / length, 0.0f, 1.0f);
			const auto shape = ShapeOf(s.climaxKind);
			const Preset peak = PeakPose(v, m);
			const Preset rest = RestPose(v, peak, rapid, m);

			// how much of the peak pose is on the face right now
			float k = 1.0f;
			float tension = 0.0f;
			if (u < shape.onset) {
				const float x = u / shape.onset;
				k = Lerp(0.55f, 1.0f, Smooth(x));
				tension = 0.20f * (1.0f - x);  // the squeeze at the start
			} else if (u < shape.peak) {
				k = 1.0f;
			} else if (u < shape.ripple) {
				const float x = (u - shape.peak) / (shape.ripple - shape.peak);
				k = 0.95f + (0.04f + 0.08f * v.tremor) * std::sin(6.2831853f * static_cast<float>(shape.ripples) * x);
			} else {
				const float x = (u - shape.ripple) / std::max(0.01f, 1.0f - shape.ripple);
				k = (shape.ripple > shape.peak ? 0.95f : 1.0f) * (1.0f - Smooth(x));
			}
			k = ClampF(k, 0.0f, 1.0f);

			for (int i = 0; i <= 29; ++i) e[i] = Lerp(rest[i], peak[i], k);
			e[30] = k >= 0.3f ? peak[30] : rest[30];  // the mood id cannot be blended, only its strength
			e[31] = Lerp(rest[31], peak[31], k);
			e[16] = e[17] = BlinkAmount(v.eyes, ClampF(Scenes::Now() - s.climaxStart, 0.0f, length), k, v.tremor, rapid);
			if (S::bClimaxChoreo) {
				if (!v.wide && tension > 0.0f) Add2(e, 28, tension);
				if (s.rapidRun >= 1) {  // oversensitive: each climax in a rapid run hits harder
					const float over = ClampF(static_cast<float>(s.rapidRun) * 0.12f, 0.0f, 0.35f);
					Add2(e, 28, over);
					Add2(e, 20, over);
				}
			}
			s.climaxFromPool = true;
			return true;
		}

		// The only mood-setter + base shape for the chosen dominant state.
		// Victim mood for a reaction; balanced keeps its sad/fear choice.
		float ReactionMood(Reaction r, float current)
		{
			switch (r) {
			case Reaction::kDefiance: return 8.0f;  // anger
			case Reaction::kNumb: return 11.0f;     // sad
			case Reaction::kFear:
			case Reaction::kPanic: return 9.0f;     // fear
			default: return current == 11.0f ? 11.0f : 9.0f;
			}
		}

		// Non-consensual distress face. The victim's follows their personality (VictimReaction);
		// the other actor's is aggressive.
		Preset DistressPreset(int enj, bool victim, Reaction react, float m)
		{
			const bool peak = enj >= 90;
			if (!victim) {  // aggressor: scowl, narrowed eyes, lowered brows
				if (peak) return Build(8, 0.70f, 0.0f, 0.50f * m, 0.55f, 0.0f, 0.45f, 0.50f);
				return Build(8, 0.45f, 0.0f, 0.25f * m, 0.45f, 0.0f, 0.35f, 0.40f);
			}
			switch (react) {
			case Reaction::kDefiance:  // resists: anger, jaw set, glaring
				return Build(8, peak ? 0.75f : 0.55f, 0.0f, 0.0f, peak ? 0.65f : 0.55f, 0.0f, 0.40f, peak ? 0.65f : 0.55f);
			case Reaction::kFear:  // freezes: wide fearful brows, small mouth
				if (peak) return Build(9, 0.90f, 0.30f * m, 0.0f, 0.35f, 0.45f, 0.55f, 0.10f);
				return Build(9, enj < 45 ? 0.70f : 0.80f, 0.10f * m, 0.0f, 0.20f, 0.40f, 0.55f, 0.10f);
			case Reaction::kPanic:  // cries out
				return Build(9, peak ? 0.95f : 0.75f, (peak ? 0.55f : 0.35f) * m, 0.0f, 0.45f, 0.35f, 0.55f, 0.20f);
			case Reaction::kNumb:  // endures: restrained sadness, closed mouth
				return Build(11, peak ? 0.45f : 0.30f, 0.0f, 0.0f, 0.20f, 0.05f, 0.30f, 0.20f);
			default:  // balanced: sadness, turning to fear as excitement rises
				if (peak) return Build(9, 0.85f, 0.55f * m, 0.0f, 0.65f, 0.30f, 0.55f, 0.45f);
				if (enj < 45) return Build(11, 0.45f, 0.10f * m, 0.0f, 0.35f, 0.10f, 0.55f, 0.40f);
				return Build(9, 0.60f, 0.30f * m, 0.0f, 0.55f, 0.20f, 0.55f, 0.45f);
			}
		}

		// Afterglow used to be one face for the whole scene. These share it, one per beat and
		// per actor, with equal weight - nothing here is rarer than its neighbours. Mood 10 is
		// Happy, 7 Neutral; squint is the main tiredness lever, since Skyrim has no eyelid
		// morph of its own and Blink is left alone so natural blinking keeps working.
		Preset AfterglowPreset(int which, float m)
		{
			switch (which) {
			case 1:  // a broader smile: the mouth spreads, the eyes crease with it
			{
				auto e = Build(10, 0.70f, 0.05f * m, 0.0f, 0.50f, 0.15f, 0.0f, 0.0f);
				e[5] = 0.35f * m;  // Eee spreads the lips
				return e;
			}
			case 2:  // relief: brows let go, a long breath out, eyes soft
			{
				auto e = Build(10, 0.45f, 0.22f * m, 0.0f, 0.35f, 0.30f, 0.0f, 0.0f);
				e[24] = 0.10f;  // LookDown: the gaze settles
				return e;
			}
			case 3:  // tired: heavy lids, head-down gaze, mouth slightly open
			{
				auto e = Build(10, 0.25f, 0.18f * m, 0.0f, 0.70f, 0.0f, 0.10f, 0.15f);
				e[24] = 0.30f;
				return e;
			}
			case 4:  // spent: barely holding the eyes open, no expression left to give
			{
				auto e = Build(7, 0.30f, 0.26f * m, 0.0f, 0.85f, 0.0f, 0.15f, 0.25f);
				e[24] = 0.45f;
				return e;
			}
			case 5:  // relief smiling into it: brows let go and the mouth goes with them
			{
				auto e = Build(10, 0.60f, 0.16f * m, 0.0f, 0.40f, 0.32f, 0.0f, 0.0f);
				e[5] = 0.28f * m;  // Eee
				e[24] = 0.08f;
				return e;
			}
			case 6:  // too tired to do much with it, but still smiling
			{
				auto e = Build(10, 0.50f, 0.20f * m, 0.0f, 0.72f, 0.05f, 0.08f, 0.10f);
				e[5] = 0.22f * m;
				e[24] = 0.28f;
				return e;
			}
			default:  // the settled half-smile this always was
				return Build(10, 0.35f, 0.10f * m, 0.0f, 0.45f, 0.20f, 0.0f, 0.0f);
			}
		}

		// How well a mood suits a personality, as a weight on picking an expression that has it. Moods are Skyrim's
		// expression ids: 0-6 the dialogue set (angry, fear, happy, sad, surprise, puzzled, disgust), 7 neutral,
		// 8-14 the same seven as moods. The pools are OStim's and say nothing about who is wearing them, so a pick was
		// random: in the 1.9.2 test a bold actor spent 59% of a scene on Puzzled and 40% on dSad, and a fierce one 56%
		// on dFear and 37% on dSad. Personalities: 0 none (everything equally likely), 1 stoic, 2 bold, 3 shy, 4 fierce.
		float MoodAffinity(int arch, int mood)
		{
			if (mood == 7) return arch == 1 ? 1.5f : 1.0f;
			const int kind = mood >= 8 ? mood - 8 : mood;  // 0 angry 1 fear 2 happy 3 sad 4 surprise 5 puzzled 6 disgust (mood ids 8-14 are the dialogue ids 0-6 plus 8)
			//                            angry fear  happy sad   surprise puzzled disgust
			static constexpr float kStoic[7]  = { 0.8f, 0.4f, 1.0f, 0.6f, 0.6f, 1.0f, 0.4f };
			static constexpr float kBold[7]   = { 1.0f, 0.3f, 2.0f, 0.3f, 1.6f, 0.5f, 0.3f };
			static constexpr float kShy[7]    = { 0.15f, 1.3f, 1.5f, 1.2f, 1.2f, 1.5f, 0.15f };
			static constexpr float kFierce[7] = { 2.0f, 0.15f, 1.2f, 0.15f, 1.2f, 0.3f, 0.6f };
			if (kind < 0 || kind > 6) return 1.0f;
			switch (arch) {
			case 1: return kStoic[kind];
			case 2: return kBold[kind];
			case 3: return kShy[kind];
			case 4: return kFierce[kind];
			default: return 1.0f;
			}
		}

		// A random member of the pool, other than the one picked last when there is a choice, weighted by how well its mood
		// suits the personality. An expression with no mood of its own (brows only, mouth only) is weighted 1: it leaves the
		// mood as it was, so it neither fits nor clashes.
		const Library::Expression* PickWeighted(const Library::Pool& pool, bool female, const void* last, int arch)
		{
			std::vector<float> weight(pool.size(), 0.0f);
			float total = 0.0f;
			for (std::size_t i = 0; i < pool.size(); ++i) {
				const auto& v = pool[i]->For(female);
				if (!v.defined || v.parts == 0) continue;  // OStim plays nothing for this gender
				if (pool[i] == last && pool.size() > 1) continue;
				weight[i] = (v.parts & Library::kMood) ? MoodAffinity(arch, v.mood.type) : 1.0f;
				total += weight[i];
			}
			if (total <= 0.0f) return nullptr;
			float draw = RandFloat(0.0f, total);
			for (std::size_t i = 0; i < pool.size(); ++i) {
				if (weight[i] <= 0.0f) continue;
				draw -= weight[i];
				if (draw <= 0.0f) return pool[i];
			}
			for (std::size_t i = pool.size(); i-- > 0;) {
				if (weight[i] > 0.0f) return pool[i];  // rounding at the very end of the range
			}
			return nullptr;
		}

		// OStim's own expression pool for what this actor is doing, played as the Director's base pose. A
		// pick is made when the pool changes (a new act) and then every few seconds; it is applied part by
		// part to the face built up so far (see Library::ApplyTo), so what shows is the accumulation of recent
		// picks, as in OStim itself. The output eases to each new target from wherever the face is, which is what
		// keeps the transitions seamless. Returns false when there is nothing to play, and the caller falls back
		// to the built-in templates.
		bool LibraryPose(Thread& t, Slot& s, RE::Actor* a, int raw, int arch, int stage, Preset& out)
		{
			if (!a || !t.meta || s.pos < 0) return false;
			const auto resolved = Library::Resolve(*t.meta, s.pos);
			const Library::Pool* pool = S::bDirectorLibrary ? resolved.underlying : nullptr;
			const bool havePool = pool && !pool->empty();
			const bool ownOn = S::bBuildupFaces;
			if (!havePool && !ownOn) return false;
			const bool female = ActorSex(a) == 1;
			// A scene whose data says nothing about what this actor is doing - an idle or transition node, or a scene
			// whose author defined no actions - gets OStim's "default" pool, which is the mild idle one: in the 1.9.0
			// test, rendered mood averaged 0.25-0.33 on it against 0.43-0.55 on an action pool, and a whole solo scene
			// never passed 0.34. When the actor is clearly aroused in a node that is neither an idle nor a transition,
			// borrow the pool OStim has for being stimulated (the same files its self-stimulation actions use) instead.
			// In at 22 excitement, out again below 12, so it does not flicker between the two.
			if (havePool && resolved.underlyingWhy == "default") {
				const bool idleNode = OStimData::HasAnySceneTag(*t.meta, OStimData::TagList{ "idle" }) || !t.meta->destination.empty();
				s.libFallback = !idleNode && raw >= (s.libFallback ? 12 : 22);
				if (s.libFallback) {
					const auto* alt = Library::ActionTarget(female ? "femalemasturbation" : "malemasturbation");
					if (alt && !alt->empty()) pool = alt;
					else s.libFallback = false;
				}
			} else {
				s.libFallback = false;
			}
			const float now = Scenes::Now();
			// A pick is made when the pool changes, every few seconds, and when the build-up moves to a new stage (the own faces are
			// per stage). It is one of the Director's own faces some of the time, and one of OStim's pool the rest, or only the own
			// faces where the scene gives no pool.
			if (s.libPool != pool || now >= s.libNextPick || (ownOn && s.buildStage != stage)) {
				s.libPool = pool;
				s.libNextPick = now + RandFloat(2.5f, 5.0f);
				s.buildStage = stage;
				const bool own = ownOn && (!havePool || RandFloat(0.0f, 1.0f) < ClampF(S::fBuildupShare, 0.0f, 1.0f));
				int ownOption = -1;
				if (own) {
					ownOption = Buildup::Pick(stage, arch, s.buildRecent.data(), static_cast<int>(s.buildRecent.size()));
					if (ownOption >= 0) {
						Buildup::Pose face;
						Buildup::Build(ownOption, static_cast<float>(ClampI(raw, 0, 100)), MouthGate(), face);
						// Every part of it, but what an override owns (the mouth, in an oral act) is not ours to set. The lids carry the blink too,
						// and the mouth the Th and W phonemes, which OStim's own expressions never touch.
						const int skip = s.libOvrMask;
						if (!(skip & Library::kPhoneme)) for (int i = 0; i <= 15; ++i) s.libState[i] = face[i];
						if (!(skip & Library::kBrow)) for (int i = 18; i <= 23; ++i) s.libState[i] = face[i];
						if (!(skip & Library::kBall)) for (int i = 24; i <= 27; ++i) s.libState[i] = face[i];
						if (!(skip & Library::kLid)) {
							s.libState[16] = face[16];
							s.libState[17] = face[17];
							s.libState[28] = face[28];
							s.libState[29] = face[29];
						}
						if (!(skip & Library::kMood)) {
							s.libState[30] = face[30];
							s.libState[31] = face[31];
						}
						s.buildOption = ownOption;
						s.buildRecent[static_cast<std::size_t>(s.buildRecentPos++ % static_cast<int>(s.buildRecent.size()))] = ownOption;
						s.libLast = nullptr;
						s.libLastName = "Own/" + Buildup::Name(ownOption);
						s.libHave = true;
					}
				}
				if (ownOption < 0 && havePool) {
					const Library::Expression* pick = PickWeighted(*pool, female, s.libLast, arch);
					if (pick) {
						const float rel = t.maxSpeed >= 0 ? static_cast<float>(t.speed) / static_cast<float>(t.maxSpeed + 1) : 0.0f;
						// An own face left the Th and W phonemes and the lids' blink set, which OStim's expressions have no part for: clear
						// them, or they would stay as they were until the next own face.
						if (!(s.libOvrMask & Library::kPhoneme)) s.libState[14] = s.libState[15] = 0.0f;
						if (!(s.libOvrMask & Library::kLid)) s.libState[16] = s.libState[17] = 0.0f;
						Library::ApplyTo(s.libState, pick->For(female), static_cast<float>(ClampI(raw, 0, 100)), rel, [] { return RandFloat(0.0f, 1.0f); },
							s.libOvrMask);  // what an override owns is not the underlying pool's to set
						s.libLast = pick;
						s.libLastName = pick->file;
						s.buildOption = -1;
						s.libHave = true;
					}
				}
				// A slow blink now and then, on top of whatever face it is: always for the faces that blink slowly, and one pick in four
				// for the rest. The face is updated every few seconds, so the output layer draws the blink itself.
				if ((ownOption >= 0 && Buildup::SlowBlink(ownOption)) || RandFloat(0.0f, 1.0f) < 0.25f) {
					Output::PulseBlink(a, RandFloat(0.60f, 0.95f), RandFloat(0.9f, 1.5f), RandFloat(0.2f, 1.8f));
					if (ownOption >= 0 && Buildup::SlowBlink(ownOption)) Output::PulseBlink(a, RandFloat(0.60f, 0.95f), RandFloat(0.9f, 1.5f), RandFloat(2.2f, 3.4f));
				}
			}
			if (!s.libHave) return false;
			out = s.libState;
			return true;
		}

		Preset BasePreset(Thread& t, Slot& s, int dom, int enj, bool victim, int arch, int seed, int role, int tone)
		{
			const float m = MouthGate();
			if (dom == kClimax) {
				s.climaxFromPool = false;
				if (S::bClimaxPool && t.consent) {
					Preset v{};
					if (ClimaxVariantPose(t, s, arch, m, v)) return v;
				}
				auto e = Build(12, 1.0f, 0.6f * m, 0.4f * m, 0.75f, 0.65f, 0.0f, 0.0f);
				if (S::bClimaxChoreo) {
					// Timed from this actor's own orgasm: tension first, the eyes rolling up at the peak, then the aftershocks. One that
					// came quickly after the last skips the tension - the face is still tight from it.
					const float age = Scenes::Now() - s.climaxStart;
					const bool repeat = RapidFactor(s) > 0.0f;
					if (!repeat && age < 1.5f) {
						e[28] = e[29] = 0.85f;  // tension: hard squint
					} else if (age < (repeat ? 3.0f : 4.5f)) {
						e[27] = 0.6f;  // eyes roll up at the peak
						e[22] = e[23] = 0.8f;
					}
					// Caught out by it: eyes wide, brows up hard, surprise instead of the usual
					// climax mood. Shy actors read this way often, everyone else now and then.
					// Skyrim has no widen-the-eye morph, so "wide" is squint at zero, brows up
					// and the surprise mood, whose own morph lifts the lids. Blink is left
					// alone on purpose - ApplyPreset never writes it, so the actor keeps
					// blinking naturally through the beat.
					if ((seed + t.tick) % (arch == 3 ? 3 : 9) == 0) {
						e[28] = e[29] = 0.0f;
						e[22] = e[23] = 1.0f;
						e[30] = 12.0f;  // surprise
						e[31] = 1.0f;
					}
					if (s.rapidRun >= 1) {  // oversensitive: each climax in a rapid run hits harder
						const float over = ClampF(static_cast<float>(s.rapidRun) * 0.12f, 0.0f, 0.35f);
						Add2(e, 28, over);
						Add2(e, 20, over);
					}
				}
				if (!t.consent && victim) {  // forced climax: the victim's own reaction colors it
					e[30] = ReactionMood(VictimReaction(arch), 9.0f);
					Add2(e, 20, 0.20f);
				}
				return e;
			}
			if (dom == kAfterglow) return AfterglowPreset((t.tick / 2 + seed) % 7, m);
			if (dom == kDistress) return DistressPreset(enj, victim, VictimReaction(arch), m);
			if (dom == kPlateau) return Build(8, 0.40f, 0.20f * m, 0.0f, 0.55f, 0.45f, 0.30f, 0.20f);
			if (dom == kAnticipation) return Build(7, 0.30f, 0.10f * m, 0.0f, 0.15f, 0.10f, 0.0f, 0.0f);

			// kPleasure: consensual arc by interaction role + enjoyment phase
			if (role == 1) return Build(7, 0.45f, 0.0f, 0.0f, 0.45f + RoleSq(enj), 0.30f, 0.20f, 0.0f);
			if (role == 2) return Build(10, 0.45f, 0.0f, 0.0f, 0.30f, 0.30f, 0.0f, 0.0f);
			switch (tone) {
			case 1: return Build(10, 0.50f, 0.20f * m, 0.0f, 0.30f, 0.30f, 0.0f, 0.0f);   // tender
			case 2: return Build(12, 0.65f, 0.50f * m, 0.0f, 0.50f, 0.45f, 0.0f, 0.0f);   // lust
			case 3: return Build(10, 0.45f, 0.10f * m, 0.0f, 0.45f, 0.30f, 0.0f, 0.0f);   // playful / pride
			case 4: return Build(10, 0.55f, 0.25f * m, 0.0f, 0.60f, 0.35f, 0.0f, 0.0f);   // surrender
			case 5: return Build(7, 0.20f, 0.05f * m, 0.0f, 0.15f, 0.05f, 0.0f, 0.0f);    // detached
			default: break;
			}
			if (enj < 50) {
				if (PickSeed(t, seed, 2) == 0) return Build(10, 0.40f, 0.25f * m, 0.0f, 0.30f, 0.25f, 0.0f, 0.0f);
				return Build(12, 0.35f, 0.0f, 0.20f * m, 0.25f, 0.30f, 0.0f, 0.0f);
			}
			if (enj < 80) {
				const float extra = role == 3 ? 0.10f : 0.0f;
				if (PickSeed(t, seed, 2) == 0) return Build(10, 0.60f, 0.45f * m, 0.0f, 0.55f + extra, 0.40f, 0.20f, 0.0f);
				return Build(12, 0.55f, 0.0f, 0.40f * m, 0.50f + extra, 0.40f, 0.30f, 0.0f);
			}
			return Build(12, 0.80f, 0.0f, 0.55f * m, 0.65f, 0.50f, 0.0f, 0.0f);  // near peak
		}

		void CapFlavors(Preset& e)
		{
			for (int i = 18; i <= 29; ++i) e[i] = std::min(e[i], 0.9f);
		}

		int PleasureTone(Thread& t, RE::Actor* a, RE::Actor* partner, int enj, int arch, int role, int posRole)
		{
			if (role == 1 || role == 2) return 0;
			if (partner && RelationshipRank(a, partner) >= 3) return 1;
			if (enj >= 70 && posRole == -1) return 4;
			if (posRole == 1 && (arch == 4 || arch == 2)) return 3;
			if (partner && RelationshipRank(a, partner) <= 0 && enj >= 60) return 2;
			if (enj < 30 && SceneTime(t) > 30.0f) return 5;
			return 0;
		}

		void ClimaxType(Preset& e, int arch)
		{
			if (arch == 1 || arch == 3) {
				Mul(e, 0, 0.3f);
				Mul(e, 11, 0.3f);
				Add2(e, 20, 0.20f);
				Add2(e, 28, 0.10f);
			} else if (arch == 2) {
				Add(e, 0, 0.25f);
				Add(e, 11, 0.20f);
			} else if (arch == 4) {
				Mul(e, 0, 0.6f);
				Mul(e, 11, 0.6f);
			}
		}

		void ColorByRelationship(Preset& e, RE::Actor* a, RE::Actor* partner)
		{
			if (!S::bNaturalDetail || !partner) return;
			const int rank = RelationshipRank(a, partner);
			if (rank >= 3) {
				Mul(e, 18, 0.5f);
				Mul(e, 19, 0.5f);
				if (e[30] == 12.0f) e[30] = 10.0f;
			} else if (rank <= -3) {
				Add(e, 20, 0.15f);
				Add(e, 18, 0.10f);
			}
		}

		void ToneColor(Thread& t, Preset& e)
		{
			if (t.toneLoving) {
				if (e[30] == 12.0f) e[30] = 10.0f;
				Mul(e, 20, 0.7f);
				Mul(e, 21, 0.7f);
			} else if (t.toneRough || t.toneForced || !t.consent) {
				Add2(e, 20, 0.15f);
				Add2(e, 28, 0.10f);
				Add2(e, 18, 0.10f);
			}
		}

		void ActFlavor(Thread& t, Slot& s, Preset& e)
		{
			if (ActorHasAnyAction(t, s, T().deepthroat)) {
				Add2(e, 18, 0.20f);
				Add2(e, 28, 0.20f);
			} else if (SceneHasAnyAction(t, T().actionAnal)) {
				Add2(e, 22, 0.10f);
			}
		}

		void PositionalFlavor(Preset& e, int posRole)
		{
			if (posRole == 1) {
				Mul(e, 0, 0.7f);
				Mul(e, 11, 0.7f);
			} else if (posRole == -1) {
				Add(e, 22, 0.10f);
				Add2(e, 28, 0.10f);
			}
		}

		void PartnerReact(Preset& e)
		{
			Add2(e, 22, 0.15f);
			Add(e, 0, 0.10f);
		}

		float ExposureStrength(Thread& t, int enjEff, int arch)
		{
			if (enjEff >= 65) return 0.0f;
			float s = static_cast<float>(65 - enjEff) / 65.0f;
			const float time = SceneTime(t);
			if (time > 0.0f) s *= ClampF(1.0f - time / 40.0f, 0.0f, 1.0f);
			if (arch == 3) s *= 1.5f;
			else if (arch == 2 || arch == 4) s *= 0.4f;
			else if (arch == 1) s *= 0.7f;
			return ClampF(s, 0.0f, 1.0f);
		}

		void ExposureFlavor(Preset& e, float s)
		{
			if (s <= 0.0f) return;
			Add2(e, 20, 0.18f * s);
			Add2(e, 28, 0.12f * s);
		}

		void WellingEyes(Preset& e)
		{
			Add2(e, 20, 0.20f);
			Add(e, 18, 0.10f);
			Add2(e, 28, 0.15f);
		}

		void Asymmetry(Preset& e, int seed)
		{
			const float d = 0.06f + static_cast<float>(seed % 4) * 0.02f;
			Add(e, 22, d);
			Add(e, 23, -d * 0.5f);
			Add(e, 28, -d * 0.5f);
			Add(e, 29, d * 0.5f);
		}

		void MicroTic(Thread& t, Preset& e, bool pleasant)
		{
			if (RandInt(0, 5) == 0) {
				const int which = RandInt(0, 2);
				if (which == 0) Add(e, 22, 0.15f);
				else if (which == 1) Add(e, 20, 0.15f);
				else Add(e, 0, 0.10f);
			} else if (pleasant && t.consent && RandInt(0, 7) == 0) {
				e[30] = 10.0f;
				e[31] = std::max(e[31], 0.40f);
				Add2(e, 22, 0.10f);
			}
		}

		void GenderColor(Preset& e, int sex)
		{
			if (sex == 1) {
				Add2(e, 22, 0.05f);
			} else {
				Add(e, 22, -0.05f);
				Add(e, 20, 0.05f);
			}
		}

		void ExhaustionLids(Thread& t, Preset& e)
		{
			const float ex = ClampF(SceneTime(t) / 600.0f, 0.0f, 0.30f);
			Add2(e, 28, ex * 0.5f);
		}

		void GaspBeat(Preset& e)
		{
			Add2(e, 22, 0.25f);
			Add(e, 0, 0.15f);
		}

		void ExcitementGradientFlavor(Thread& t, Preset& e, int raw)
		{
			if (raw >= 75) {
				Add2(e, 28, 0.08f);
				Add2(e, 22, 0.06f);
			} else if (raw < 25 && SceneTime(t) > 20.0f) {
				Add2(e, 20, 0.06f);
			}
		}

		void SpeedFlavor(Thread& t, Preset& e)
		{
			if (t.maxSpeed <= 0) return;
			const float s = static_cast<float>(t.speed) / static_cast<float>(t.maxSpeed);
			if (s >= 0.75f) Add2(e, 28, 0.06f);
			else if (s <= 0.25f && SceneTime(t) > 8.0f) Add2(e, 22, 0.04f);
		}

		void ApplyArchetype(Preset& e, int arch)
		{
			if (arch == 3) {
				Add2(e, 20, 0.15f);
			} else if (arch == 4) {
				Mul(e, 0, 0.6f);
				Mul(e, 11, 0.6f);
			}
		}

		void Gag(Preset& e)
		{
			e[0] = e[11] = 0.0f;
			Add2(e, 20, 0.30f);
			Add2(e, 28, 0.20f);
		}

		void GagRing(Preset& e)
		{
			Add(e, 0, 0.60f);
			Add(e, 11, 0.25f);
			Add2(e, 18, 0.20f);
			Add2(e, 28, 0.20f);
		}

		void Blindfold(Preset& e) { Add2(e, 28, 0.45f); }

		void ApplyScenarioCycler(Preset& e, int scenario, int phase, int seed)
		{
			if (!S::bScenarioCycler) return;
			const float amp = 0.04f + StyleValue() * 0.025f;
			const float tiny = static_cast<float>((seed + phase + scenario) % 3);
			const float variance = (tiny - 1.0f) * 0.015f;
			switch (scenario) {
			case 0:
				e[0] *= 0.72f + amp;
				Add2(e, 22, amp * 0.45f);
				break;
			case 1:
			case 5:
				if (phase == 1 || phase == 2) {
					Add(e, 0, amp + variance);
					Add2(e, 28, amp * 0.7f);
				} else if (phase == 4) {
					e[0] *= 0.65f;
				}
				break;
			case 2:
			case 3:
				Add2(e, 20, amp * 0.75f);
				if (phase == 2) {
					Add(e, 0, amp * 1.2f);
					Add2(e, 28, amp);
				}
				break;
			case 4:
				Add2(e, 28, amp * 1.2f);
				break;
			case 6:
				Add2(e, 22, amp);
				Add(e, 0, amp * 0.55f);
				break;
			case 7:
				Add2(e, 18, amp);
				Add2(e, 20, amp);
				e[0] *= 0.45f;
				break;
			case 8:
				e[0] *= 0.55f;
				e[11] *= 0.55f;
				Add2(e, 28, amp * 0.45f);
				break;
			case 9:
				e[0] *= 0.35f;
				e[11] *= 0.35f;
				e[31] = ClampF(e[31] * 0.75f, 0.0f, 1.0f);
				break;
			default: break;
			}
		}

		float UpdateOverwhelmMeter(Thread& t, Slot& s, RE::Actor* a, int dom, int rawEnj, int arch, int posRole, bool yieldMouth)
		{
			float cur = s.overwhelm;
			const bool blocked = !S::bOverwhelmFace || !t.consent || (S::bNoDistressOverwhelm && dom == kDistress) || yieldMouth ||
			                     HeadCommittedToAnimation(t, s, a) || SceneTime(t) < 18.0f;
			if (blocked) {
				s.overwhelm = ClampF(cur - 0.20f, 0.0f, 1.0f);
				return s.overwhelm;
			}
			int threshold = 90;
			if (arch == 2 || arch == 3) threshold -= 5;
			else if (arch == 1 || arch == 4) threshold += 5;
			if (posRole == -1) threshold -= 3;
			else if (posRole == 1) threshold += 3;
			if (dom == kClimax && rawEnj >= 82) cur += 0.30f + StyleValue() * 0.04f;
			else if (rawEnj >= threshold || dom == kPlateau) cur += 0.13f + StyleValue() * 0.03f;
			else if (dom == kAfterglow) cur -= 0.18f;
			else cur -= 0.10f;
			s.overwhelm = ClampF(cur, 0.0f, 1.0f);
			return s.overwhelm;
		}

		void ApplyOverwhelmFace(Thread& t, Preset& e, float meter, int phase, bool yieldMouth)
		{
			if (!S::bOverwhelmFace || meter < 0.55f || !t.consent) return;
			const float s = ClampF((meter - 0.45f) * (0.80f + StyleValue() * 0.25f), 0.0f, 0.75f);
			Add2(e, 20, 0.08f * s);
			Add2(e, 22, 0.10f * s);
			Add2(e, 28, 0.16f * s);
			if (!yieldMouth) {
				Add(e, 0, 0.14f * s, 0.72f + StyleValue() * 0.10f);
				Add(e, 11, 0.08f * s, 0.55f + StyleValue() * 0.10f);
			}
			if (StyleValue() >= 1.0f && phase == 2) Add(e, 27, (StyleValue() - 0.75f) * 0.10f * s, 0.42f);
		}

		void ApplyGroupConductor(Thread& t, Preset& e, int idx, int role, int posRole, bool sub)
		{
			if (!S::bGroupConductor || t.PaintedCount() < 3) return;
			const bool focal = sub || posRole == -1 || role == 3 || idx == 0;
			const float scale = focal ? 1.0f + 0.08f + StyleValue() * 0.025f : 0.92f;
			for (int i : { 0, 11, 22, 23, 28, 29 }) e[i] = ClampF(e[i] * scale, 0.0f, 1.0f);
		}

		void ApplyPhraseEnvelope(Preset& e, int phase, int dom, bool yieldMouth)
		{
			float mouth = 1.0f;
			float eyes = 1.0f;
			if (phase == 0) mouth = 0.72f, eyes = 0.82f;
			else if (phase == 2) mouth = StyleAmp(), eyes = StyleAmp();
			else if (phase == 3) mouth = 0.86f, eyes = 0.90f;
			else if (phase == 4) mouth = 0.48f, eyes = 0.74f;
			if (yieldMouth) mouth = 0.0f;
			if (dom == kDistress) {
				mouth = ClampF(mouth, 0.0f, 0.62f);
				eyes = std::max(eyes, 0.95f);
			}
			for (int i = 0; i <= 15; ++i) e[i] = ClampF(e[i] * mouth, 0.0f, 1.0f);
			for (int i = 22; i <= 29; ++i) e[i] = ClampF(e[i] * eyes, 0.0f, 1.0f);
		}

		// Non-consensual scenes only, and by role. The victim's face is held to their reaction
		// (see VictimReaction); the other actor's to an aggressive one. Before this was role-aware,
		// every actor got the victim's fear and averted eyes.
		void ApplyConsentGuardrails(Thread& t, Preset& e, int phase, int dom, bool victim, Reaction react)
		{
			if (t.consent) return;
			if (S::bNoDistressOverwhelm) {  // no gaping mouth or rolled-up eyes for anyone
				e[1] = 0.0f;
				e[27] = 0.0f;
				e[31] = ClampF(e[31], 0.20f, 0.85f);
			}
			if (!victim) {
				// Aggressor: scowl, narrowed eyes fixed on the victim; nothing fearful or sad.
				for (int i = 0; i <= 15; ++i) e[i] = ClampF(e[i], 0.0f, 0.45f);
				Add(e, 18, 0.15f, 0.85f);
				Add(e, 19, 0.15f, 0.85f);
				Add(e, 20, 0.10f, 0.80f);
				Add(e, 21, 0.10f, 0.80f);
				e[22] = ClampF(e[22], 0.0f, 0.10f);
				e[23] = ClampF(e[23], 0.0f, 0.10f);
				e[24] = e[25] = e[26] = 0.0f;
				Add(e, 28, 0.12f, 0.80f);
				Add(e, 29, 0.12f, 0.80f);
				if (dom != kAfterglow && dom != kClimax) e[30] = 8.0f;
				return;
			}
			const bool defiant = react == Reaction::kDefiance;
			const bool panic = react == Reaction::kPanic;
			for (int i = 0; i <= 15; ++i) e[i] = ClampF(e[i] * (panic ? 0.60f : 0.35f), 0.0f, panic ? 0.40f : 0.22f);
			Add(e, 18, 0.18f, 0.85f);
			Add(e, 19, 0.18f, 0.85f);
			Add(e, 20, 0.25f, 0.90f);
			Add(e, 21, 0.25f, 0.90f);
			// Raised brows read as fear; a defiant face keeps them down.
			const float browUpCap = defiant ? 0.05f : (react == Reaction::kFear || panic ? 0.45f : 0.25f);
			e[22] = ClampF(e[22], 0.0f, browUpCap);
			e[23] = ClampF(e[23], 0.0f, browUpCap);
			if (defiant) {
				e[24] = e[25] = e[26] = 0.0f;  // eyes forward: glaring, not averted
			} else {
				Add(e, 24, 0.18f, 0.55f);
				if ((phase % 2) == 0) Add(e, 25, 0.12f, 0.45f);
				else Add(e, 26, 0.12f, 0.45f);
			}
			Add(e, 28, 0.16f, 0.80f);
			Add(e, 29, 0.16f, 0.80f);
			if (dom != kAfterglow) e[30] = ReactionMood(react, e[30]);
		}

		void ApplyEyeScalar(Preset& e)
		{
			const float eye = EyeScale();
			const float brow = BrowScale();
			e[22] = ClampF(e[22] * brow, 0.0f, 1.0f);
			e[23] = ClampF(e[23] * brow, 0.0f, 1.0f);
			for (int i = 24; i <= 29; ++i) e[i] = ClampF(e[i] * eye, 0.0f, 1.0f);
			if (StyleValue() < 0.5f) {
				e[27] = ClampF(e[27], 0.0f, 0.22f);
				e[28] = ClampF(e[28], 0.0f, 0.72f);
				e[29] = ClampF(e[29], 0.0f, 0.72f);
			} else if (StyleValue() < 1.5f) {
				e[27] = ClampF(e[27], 0.0f, 0.42f);
			}
		}

		void ApplyV2Controls(Thread& t, Preset& e, int phase, int dom, bool yieldMouth, bool victim, Reaction react)
		{
			if (S::bPhraseGrammar) ApplyPhraseEnvelope(e, phase, dom, yieldMouth);
			if (S::bConsentGuardrails) ApplyConsentGuardrails(t, e, phase, dom, victim, react);
			ApplyEyeScalar(e);
		}

		void ApplyEyeSquint(RE::Actor* a, int eye, int seed)
		{
			const int off = (seed % 3) + 1;
			SetMod(a, 28, EyeValue(ClampI(eye + off, 0, 95)), 0.55f);
			SetMod(a, 29, EyeValue(ClampI(eye - off, 0, 95)), 0.55f);
		}

		void ClimaxMouth(Thread& t, Slot& s, RE::Actor* a, int arch)
		{
			SetOwners(s, s.faceOwner, "Climax mouth", "Climax eyes", s.headOwner);
			const int tremor = (t.tick % 2) * 6;
			if (arch == 1 || arch == 3) {  // stoic / shy: clenched, bitten
				ResetPh(a, 0.5f);
				SetPh(a, 2, 18 + tremor, 0.5f);
				SetMod(a, 28, EyeValue(ClampI(82 + tremor, 0, 95)), 0.4f);
				SetMod(a, 29, EyeValue(ClampI(82 - tremor, 0, 95)), 0.4f);
				return;
			}
			int wide = arch == 4 ? 60 : (arch == 2 ? 88 : 80);
			wide = ClampI(wide + tremor, 0, 92);
			ResetPh(a, 0.4f);
			SetPh(a, 1, wide, 0.4f);
			SetMod(a, 28, EyeValue(ClampI(78 + tremor, 0, 95)), 0.4f);
			SetMod(a, 29, EyeValue(ClampI(78 - tremor, 0, 95)), 0.4f);
		}

		RE::TESObjectREFR* Marker(Slot& s)
		{
			if (auto m = s.marker.get()) return m.get();
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* dh = RE::TESDataHandler::GetSingleton();
			auto* xm = dh ? dh->LookupForm<RE::TESBoundObject>(0x3B, "Skyrim.esm") : nullptr;  // XMarker
			if (!xm) xm = dh ? dh->LookupForm<RE::TESBoundObject>(0x34, "Skyrim.esm") : nullptr;
			if (!player || !xm) return nullptr;
			auto ref = player->PlaceObjectAtMe(xm, false);
			if (!ref) return nullptr;
			s.marker = ref->GetHandle();
			return ref.get();
		}
	}

	// ------------------------------------------------------------------ gaze / head
	void ClearLook(RE::Actor* a)
	{
		if (a) Papyrus::ClearLookAt(a);
	}

	void LookAt(RE::Actor* a, RE::Actor* target)
	{
		if (a && target) Papyrus::SetLookAt(a, target);
	}

	void LookAtOffset(Slot& s, RE::Actor* a, float x, float y, float z)
	{
		auto* m = Marker(s);
		if (!m || !a) return;
		m->SetPosition(a->GetPosition() + RE::NiPoint3{ x, y, z });
		Papyrus::SetLookAt(a, m);
	}

	void SetGaze(Thread& t, Slot& s, RE::Actor* a, RE::Actor* partner)
	{
		if (t.orgasm) return ClearLook(a);  // break gaze, lose focus at climax
		if (!t.consent) {
			// A frightened, panicked or numb victim never looks at the other actor; a defiant one
			// may glare. The aggressor keeps normal gaze on the victim.
			if (FaceVictim(t, s) && VictimReaction(Archetype(a)) != Reaction::kDefiance) return ClearLook(a);
			if (S::bGazeConsentOnly) return ClearLook(a);
		}
		if (Archetype(a) == 3) return ClearLook(a);  // shy: avert
		if (partner && NaturalGazeAngle(a, partner)) {
			if (RandInt(0, 4) == 0) ClearLook(a);  // ~20%: a natural glance away
			else LookAt(a, partner);
		} else {
			ClearLook(a);  // behind/awkward: don't owl-neck
		}
	}

	void ClearGazeAll(Thread& t)
	{
		for (auto& s : t.slots) ClearLook(s.Get());
	}

	void ApplyHeadflow(Thread& t, Slot& s, RE::Actor* a, int idx, int dom, int phrase, RE::Actor*, int arch, int, int scenario, float overwhelm, bool yieldMouth)
	{
		if (!S::bHeadflow || !a) return;
		if (HeadCommittedToAnimation(t, s, a)) {
			ClearLook(a);
			s.headOwner = "Animation head";
			Pulse::Emit("SLED_Headflow", t.id, a, 0.0f);
			return;
		}
		const float side = ((idx + Seed(a)) % 2) == 0 ? -55.0f : 55.0f;
		const float style = StyleValue();
		if (dom == kDistress) {
			// Only a victim who isn't defiant turns away; the aggressor and a defiant victim
			// keep facing the other actor (gaze decides where they look).
			if (FaceVictim(t, s) && VictimReaction(arch) != Reaction::kDefiance) {
				LookAtOffset(s, a, side, 0.0f, 72.0f + style * 4.0f);
				s.headOwner = "Brace aversion";
				Pulse::Emit("SLED_Headflow", t.id, a, 1.0f);
			} else {
				s.headOwner = FaceVictim(t, s) ? "Defiant" : "Aggressor";
			}
		} else if (dom == kAfterglow) {
			LookAtOffset(s, a, side * 0.25f, 0.0f, 68.0f + style * 6.0f);
			s.headOwner = "Afterglow drop";
			Pulse::Emit("SLED_Headflow", t.id, a, 2.0f);
		} else if (dom == kClimax && t.consent && !yieldMouth) {
			const float z = phrase >= 3 ? 96.0f + style * 8.0f : 122.0f + style * 18.0f;
			LookAtOffset(s, a, 0.0f, 0.0f, z);
			s.headOwner = "Throat arch";
			Pulse::Emit("SLED_Headflow", t.id, a, 3.0f);
		} else if (S::bOverwhelmFace && overwhelm >= 0.72f && t.consent && !yieldMouth) {
			LookAtOffset(s, a, side * 0.20f, 0.0f, 108.0f + style * 12.0f);
			s.headOwner = "Overwhelm unfocus";
			Pulse::Emit("SLED_Headflow", t.id, a, 5.0f);
		} else if (t.consent && arch == 3 && (phrase == 0 || phrase == 4 || scenario == 6)) {
			LookAtOffset(s, a, side, 0.0f, 84.0f + style * 6.0f);
			s.headOwner = "Shy turn-away";
			Pulse::Emit("SLED_Headflow", t.id, a, 4.0f);
		}
	}

	void BodyDemo(Thread& t, Slot& s, RE::Actor* a, int dom)
	{
		if (dom == kClimax && t.consent && !HeadCommittedToAnimation(t, s, a)) {
			LookAtOffset(s, a, 0.0f, 0.0f, 170.0f);  // just above the head: throat arch
			s.headOwner = "BodyDemo";
		}
	}

	// ------------------------------------------------------------------ ApplyArc
	// Composes one coherent face via 5-stage arbitration: one dominant state owns the mood,
	// flavors layer on top (capped), mouth arbiter, v2 controls, then emit + gaze.
	// OStim plays an override pool for whoever is doing an oral or kiss action (the action definition names it:
	// `openmouth` for a blowjob or cunnilingus, `tongue` for licking and French kissing). With OStim's overrides
	// off for us, the Director plays it: a pick every few seconds, applied part by part to its own face. The parts
	// the current pick has are the override's for as long as it lasts - the underlying pool leaves them alone,
	// as in OStim - and the tongue is put out when the pick says so. Returns true while one is playing.
	bool UpdateOralOverride(Thread& t, Slot& s, RE::Actor* a, int raw)
	{
		if (!a || !OverridesAreOurs() || !t.meta || s.pos < 0 || s.ppaYield) {
			EndOralOverride(s, a);  // PPA's mouth preset opens the mouth for a blowjob; this would only compete
			return false;
		}
		const auto resolved = Library::Resolve(*t.meta, s.pos);
		const Library::Pool* pool = resolved.override;
		if (!pool || pool->empty()) {
			EndOralOverride(s, a);
			return false;
		}
		const bool female = ActorSex(a) == 1;
		const float now = Scenes::Now();
		if (s.libOvrPool != pool || now >= s.libOvrNext) {
			const Library::Expression* pick = nullptr;
			for (int attempt = 0; attempt < 8 && !pick; ++attempt) {
				const auto* c = (*pool)[static_cast<std::size_t>(RandInt(0, static_cast<int>(pool->size()) - 1))];
				if (!c->For(female).defined || c->For(female).parts == 0) continue;
				if (c == s.libOvrLast && pool->size() > 1) continue;
				pick = c;
			}
			s.libOvrPool = pool;
			s.libOvrNext = now + RandFloat(2.5f, 5.0f);
			if (pick) {
				const auto& v = pick->For(female);
				const float excitement = static_cast<float>(ClampI(raw, 0, 100));
				const float rel = t.maxSpeed >= 0 ? static_cast<float>(t.speed) / static_cast<float>(t.maxSpeed + 1) : 0.0f;
				Library::ApplyTo(s.libOvr, v, excitement, rel, [] { return RandFloat(0.0f, 1.0f); });
				s.libOvrMask = v.parts;
				s.libOvrLast = pick;
				s.libOvrName = pick->file;
				// Objects it equips (the tongue) go on while the excitement is at or above its threshold, and come
				// off when a pick has none - as OStim's phoneme objects do.
				const bool wantTongue = std::ranges::find(v.objects, std::string("tongue")) != v.objects.end() && excitement >= v.objectThreshold;
				if (wantTongue != s.libTongue) {
					s.libTongue = wantTongue;
					SetOSEDTongue(s, a, wantTongue);
				}
			}
		}
		return s.libOvrMask != 0;
	}

	// Whoever was doing the oral act has stopped: the parts it owned go back to the underlying pool, whose
	// state was kept while it held them, and the tongue is taken back.
	void EndOralOverride(Slot& s, RE::Actor* a)
	{
		if (!s.libOvrPool && !s.libOvrMask && !s.libTongue) return;
		s.libOvrPool = nullptr;
		s.libOvrLast = nullptr;
		s.libOvrMask = 0;
		s.libOvrName.clear();
		s.libNextPick = 0.0f;  // a fresh underlying pick straight away
		if (s.libTongue) {
			s.libTongue = false;
			SetOSEDTongue(s, a, false);
		}
	}

	void OverlayOralOverride(const Slot& s, std::array<float, 32>& e)
	{
		if (s.libOvrMask) Library::CopyParts(e, s.libOvr, s.libOvrMask);
	}

	void PickClimaxClip(const Thread& t, Slot& s, int arch)
	{
		s.climaxVariant = PickClimaxVariant(s, arch);
		s.lastClimaxVariant2 = s.lastClimaxVariant;
		s.lastClimaxVariant = s.climaxVariant;
		s.climaxVariantAt = s.climaxStart;
		const auto& v = kClimaxVariants[s.climaxVariant];
		const float r = RapidFactor(s);
		float pLong;
		if (r > 0.0f) {
			pLong = ClampF(0.65f - 0.5f * r, 0.15f, 0.60f);  // the closer to the last orgasm, the shorter
		} else {
			static constexpr float kBase[5] = { 0.50f, 0.30f, 0.65f, 0.40f, 0.55f };  // none, stoic, bold, shy, fierce
			pLong = kBase[ClampI(arch, 0, 4)] + (t.plateau >= 3 ? 0.20f : 0.0f);      // held at the edge for a while: a long one
		}
		pLong = ClampF(pLong + v.longBias, 0.08f, 0.92f);
		const bool isLong = RandFloat(0.0f, 1.0f) < pLong;
		s.climaxKind = r > 0.0f ? (isLong ? kRapidLong : kRapidShort) : (isLong ? kStdLong : kStdShort);
		s.climaxName = std::string(v.name) + "/" + ClimaxKindName(s.climaxKind);
	}

	void ApplyArc(Thread& t, Slot& s, RE::Actor* a, int idx, bool yieldMouth)
	{
		ReleaseOStimFace(s, a);
		const int enjEff = EffectiveIntensity(t, a);
		const int seed = Seed(a);
		std::string archSource;
		const int arch = Archetype(a, &archSource);
		s.arch = arch;
		s.archSource = archSource;
		RE::Actor* partner = PrimaryPartner(t, s);
		const bool sub = IsSubmissive(t, s);
		const bool victim = FaceVictim(t, s);
		const Reaction react = VictimReaction(arch);
		const int role = ActRole(t, s, a);

		// 1) DOMINANT
		const int rawEnj = Raw(a);
		const int dom = SelectDominant(t, s, enjEff, rawEnj);
		const int enjPhase = ClampI(enjEff + ArchTempo(arch), 0, 130);
		const int posRole = S::bPositionalDomSub ? PositionRole(t, s) : 0;
		const int tone = S::bRichEmotions && dom == kPleasure ? PleasureTone(t, a, partner, enjPhase, arch, role, posRole) : 0;
		const int phrase = PhrasePhase(t, idx, enjEff);
		const int scenario = ScenarioCode(t, dom, enjEff, role, tone, posRole);
		const float overwhelm = UpdateOverwhelmMeter(t, s, a, dom, rawEnj, arch, posRole, yieldMouth);
		// In the phases where OStim's own faces are the thing to copy, the base pose is the pool OStim has for
		// what this actor is doing. Consensual scenes only for now; everything else keeps the built-in grammar.
		// OStim's override pool (open mouth, tongue) for whoever is doing the oral act, played by us. Updated first:
		// the parts it owns are not the underlying pool's to set.
		const bool overriding = UpdateOralOverride(t, s, a, rawEnj);
		Preset e{};
		const bool usingLib = (S::bDirectorLibrary || S::bBuildupFaces) && t.consent && (dom == kPleasure || dom == kAnticipation || dom == kPlateau) &&
				LibraryPose(t, s, a, rawEnj, arch, Buildup::StageFor(dom == kAnticipation, dom == kPlateau, enjPhase), e);
		if (!usingLib) e = BasePreset(t, s, dom, enjPhase, victim, arch, seed, role, tone);
		// The edge of the climax is played from the pool like the rest of the build-up, with the tension on top: eyes
		// squeezed, brows drawn together. The template this phase used (Anger mood 0.4, mouth 0.2) rendered at about a
		// third of the strength of the faces leading into it - mood 0.28 against 0.57-0.96, mouth 0.15 against 0.57-0.98
		// in the 1.9.1 test - so the build-up fell away just before the peak.
		if (usingLib && dom == kPlateau) {
			Add2(e, 28, 0.18f);  // squint
			Add2(e, 20, 0.15f);  // brows in
		}
		// A run of rapid orgasms leaves the face tense: squint and brows drawn in, growing with each orgasm in the run and fading
		// over the rapid window. It sits on top of whatever the pool or template gave, so the build-up between orgasms reads as
		// the same actor, over-sensitised, not as a fresh start.
		if (const float sens = Sensitivity(s, Scenes::Now()); sens > 0.0f && t.consent && dom != kClimax && dom != kAfterglow && dom != kDistress) {
			Add2(e, 28, sens);         // squint
			Add2(e, 20, sens * 0.8f);  // brows in
		}
		if (dom == kClimax && !s.climaxFromPool) ClimaxType(e, arch);  // the pool's variants are already weighted by personality

		// 2) FLAVORS
		const bool pleasant = dom == kPleasure || dom == kAnticipation;
		// The act and relationship flavors are for the built-in templates: the pool is already specific to the
		// act, and a relationship should colour the face, not replace it (it used to: see 1.8.1's notes).
		if (pleasant && !usingLib) {
			ColorByRelationship(e, a, partner);
			if (S::bRoleAware) ToneColor(t, e);
			if (S::bActTypeAware) ActFlavor(t, s, e);
			if (S::bPositionalDomSub) PositionalFlavor(e, posRole);
			if (S::bRichEmotions && t.orgasm) PartnerReact(e);
			if (S::bExposureAware && t.consent && IsNude(a)) ExposureFlavor(e, ExposureStrength(t, enjEff, arch));
		}
		// An eye-roll beat in the pleasure arc: the eyes drift up and the lids lower with them.
		// One of the ordinary beats, no more likely than its neighbours, and held back until
		// there is enough excitement for it to read as pleasure rather than boredom.
		if (dom == kPleasure && t.consent && enjPhase >= 55 && (seed + t.tick) % 5 == 0) {
			Add(e, 27, 0.55f);        // LookUp
			Add2(e, 28, 0.20f);       // lids follow
			Add2(e, 22, 0.15f);       // brows lift a little with them
		}
		if (S::bNaturalDetail) {
			// Welling eyes: near the peak in consensual scenes; in distress only for a victim who isn't defiant.
			const bool welling = dom == kDistress ? (victim && react != Reaction::kDefiance) : enjEff >= 88;
			if (dom != kClimax && dom != kAfterglow && welling) WellingEyes(e);
			Asymmetry(e, seed);
			if (!yieldMouth && !usingLib) MicroTic(t, e, pleasant);  // it can swap the mood, which the pool has chosen
			GenderColor(e, ActorSex(a));
			ExhaustionLids(t, e);
			if (t.gasp && dom != kClimax && dom != kAfterglow) GaspBeat(e);
		}
		if (S::bExcitementGradient) ExcitementGradientFlavor(t, e, rawEnj);
		if (S::bSpeedSync) SpeedFlavor(t, e);
		ApplyArchetype(e, arch);

		// 3) ANTI-SATURATION (climax exempt)
		if (dom != kClimax) CapFlavors(e);

		// 4) MOUTH ARBITER: gags override; blindfold closes the eyes
		const bool gagC = S::bDeviceAware && IsGagClosed(a);
		const bool gagR = S::bDeviceAware && IsGagRing(a);
		const bool blind = S::bDeviceAware && IsBlind(a);
		if (gagC) Gag(e);
		else if (gagR) GagRing(e);
		if (blind) Blindfold(e);
		if (!usingLib) ApplyScenarioCycler(e, scenario, phrase, seed);
		ApplyOverwhelmFace(t, e, overwhelm, phrase, yieldMouth);
		if (!usingLib) ApplyGroupConductor(t, e, idx, role, posRole, sub);
		if (usingLib) ApplyEyeScalar(e);
		else ApplyV2Controls(t, e, dom == kClimax && s.climaxFromPool ? 2 : phrase, dom, yieldMouth, victim, react);  // a clip ripples on its own

		// Director owns the whole face, but its pleasure presets were tuned as an overlay on OStim's
		// own face, not as one: OStim's expression files run mood 0.7-1.0, brows 0.4-1.0 and mouth
		// 0.5-1.0 at 60-100 excitement, against Director's mood 0.4-0.5, brows 0.2-0.4 and mouth up to
		// 0.45 (face probe, 1.7.3). Left alone the faces read as flat. These ratios bring the channels
		// to OStim's level at fDirectorGain 1; the eyelids already match, so squint is not scaled. The
		// climax is above OStim's already, and plateau, afterglow and distress are deliberate low or
		// extreme poses of their own.
		if (dom == kPleasure && S::fDirectorGain > 0.0f && !usingLib) {
			const float g = S::fDirectorGain;
			const float mood = 1.0f + 0.4f * g;
			const float mouth = 1.0f + 0.6f * g;
			const float brow = 1.0f + 0.5f * g;
			e[31] = ClampF(e[31] * mood, 0.0f, 1.0f);
			for (int i = 0; i < 16; ++i) e[i] = ClampF(e[i] * mouth, 0.0f, 1.0f);
			for (int i = 18; i <= 23; ++i) e[i] = ClampF(e[i] * brow, 0.0f, 1.0f);
		}

		// 5) EMIT + gaze
		const float prof = ProfileScale() * ArchStrength(arch);
		const float jit = RandFloat(0.92f, 1.08f);
		// A pool's values are OStim's own, so Strength is taken relative to its default (0.85): the default
		// plays them at their authored size, and the slider still scales them.
		const float strength = usingLib ? S::fGlobalStrength / 0.85f : S::fGlobalStrength;
		if (usingLib && S::bBreathing) {
			for (int i = 0; i < 16; ++i) e[i] = 0.0f;  // the breath clock holds the mouth
		}
		// The open mouth and the tongue belong to the override pool for as long as it plays, whatever phase this is.
		if (overriding) OverlayOralOverride(s, e);
		const float eStr = ClampF(strength * prof * jit, 0.0f, 2.0f);
		const float mStr = ClampF(strength * prof * PersonalityMod(seed), 0.0f, 2.0f);
		// The breath clock holds the mouth itself at climax (ClimaxMouth) and on a ring gag, so the
		// preset leaves the mouth to it then. This used to read !bBreathing - true exactly when the
		// breath clock is off and nothing else holds the mouth - so with default settings the
		// climax preset's own open mouth, and a ring gag's, were thrown away and never shown.
		const bool clenched = dom == kClimax && (arch == 1 || arch == 3);
		const bool breathHoldsMouth = !yieldMouth && S::bBreathing && ((dom == kClimax && !gagC && !clenched) || gagR);
		// A pool changes slowly, so it also moves slowly: a longer ease than the templates need.
		const float ease = usingLib ? std::max(S::fTransition, 0.9f) : (dom == kClimax && s.climaxFromPool ? std::max(S::fTransition, 0.8f) : S::fTransition);
		Output::ApplyPreset(a, e, breathHoldsMouth || yieldMouth, eStr, mStr, strength, ease);
		SetOwners(s, usingLib ? "Library/" + s.libLastName + (s.libFallback ? " (no act in the scene: stimulation pool)" : "")
				: (dom == kClimax && s.climaxFromPool ? "Climax/" + s.climaxName : std::string(DomName(dom)) + "/" + ScenarioName(scenario)),
				yieldMouth ? MouthOwnerLabel(t, s, a, true) : (overriding ? "Library override/" + s.libOvrName : std::string("OSED arc")),
			"Phrase " + std::to_string(phrase), "Pending gaze");
		PulseActor(t, s, a, dom, phrase, enjEff);
		Pulse::Emit("SLED_Overwhelm", t.id, a, overwhelm);

		// Anime style: the OSED 1.0 climax accent rides on top of the Director face.
		if (OSEDShouldAnime(t, s, a, rawEnj, yieldMouth)) ApplyOSEDAnimeAccent(t, s, a, rawEnj, yieldMouth);
		else ClearOSEDAnimeAccent(s, a);

		if (blind) {
			ClearLook(a);
			s.headOwner = "Blindfold";
		} else if (yieldMouth) {
			ClearLook(a);
			s.headOwner = MouthOwnerLabel(t, s, a, true);
		} else if (S::bGaze) {
			SetGaze(t, s, a, partner);
			s.headOwner = "Gaze";
		} else {
			s.headOwner = "Idle";
		}
		if (S::bHeadflow) ApplyHeadflow(t, s, a, idx, dom, phrase, partner, arch, posRole, scenario, overwhelm, yieldMouth);
		else if (S::bBodyDemo) BodyDemo(t, s, a, dom);
	}

	// ------------------------------------------------------------------ Breathe (Director breath clock)
	void Breathe(Thread& t, Slot& s, RE::Actor* a, int idx)
	{
		SetOwners(s, s.faceOwner, "Breath clock", "Breath clock", s.headOwner);
		if (S::bDeviceAware) {
			if (IsGagClosed(a)) {
				SetPh(a, 0, 3, 0.5f);
				SetMod(a, 28, EyeValue(35), 0.5f);
				SetMod(a, 29, EyeValue(35), 0.5f);
				SetOwners(s, s.faceOwner, "Closed gag", "Gag strain", s.headOwner);
				return;
			}
			if (IsGagRing(a)) {
				SetPh(a, 0, 60, 0.5f);
				SetOwners(s, s.faceOwner, "Ring gag", s.eyeOwner, s.headOwner);
				return;
			}
		}
		const int enjEff = EffectiveIntensity(t, a);
		const int seed = Seed(a);
		const int arch = Archetype(a);

		if (s.climaxing) return ClimaxMouth(t, s, a, arch);

		if (t.gasp && !t.orgasm) {  // sharp inhale on a stage change; eyes widen
			ResetPh(a, 0.3f);
			SetPh(a, 0, 38, 0.3f);
			SetMod(a, 28, 0, 0.3f);
			SetMod(a, 29, 0, 0.3f);
			SetOwners(s, s.faceOwner, "Gasp", "Gasp", s.headOwner);
			return;
		}
		if (S::bCinematic && !t.orgasm && t.afterglow == 0 && enjEff >= 85 && enjEff < 95) {  // breath-hold tell
			SetPh(a, 0, 12 + RandInt(0, 4), 0.7f);
			SetMod(a, 28, EyeValue(45), 0.6f);
			SetMod(a, 29, EyeValue(45), 0.6f);
			SetOwners(s, s.faceOwner, "Breath hold", "Breath hold", s.headOwner);
			return;
		}

		int cyc = 4;
		if (enjEff >= 92) cyc = 2;
		else if (enjEff >= 72) cyc = 3;
		else if (enjEff < 40) cyc = 5;
		const int base = t.tick + idx * 2 + seed;
		const int p = base % cyc;
		const int cycleIdx = base / cyc;

		const int vchance = std::max(0, enjEff + 8);
		bool vocal = ((seed * 7 + cycleIdx * 13) % 100) < vchance;
		if (arch == 2 || arch == 4) vocal = vocal || ((seed * 5 + cycleIdx * 11) % 100) < 35;
		else if ((arch == 1 || arch == 3) && vocal && ((seed * 3 + cycleIdx * 7) % 100) < 35) vocal = false;

		float af = 0.45f;
		if (p == 0) af = 0.06f;
		else if (p == 1) af = 0.80f;
		else if (p == 2) af = 1.00f;

		int val = 0;
		if (vocal) {
			const int basePeak = 16 + (enjEff * 5) / 10;
			const int varr = ((seed + cycleIdx) % 5) * 7;
			const int peak = ClampI(basePeak - 12 + varr, 14, 72);
			val = ClampI(static_cast<int>(static_cast<float>(peak) * af), 0, 72);
			if (enjEff >= 85) val = ClampI(val + (t.tick % 2) * 5, 0, 76);  // quiver near climax
		} else {
			val = ClampI(static_cast<int>(6.0f * af), 0, 8);
		}

		const int lidFloor = t.consent && enjEff >= 50 ? ClampI(12 + (enjEff - 50) / 3, 0, 38) : 0;

		if (!S::bMouthVariety || !t.consent) {
			const int dv = !t.consent ? ClampI(val + (t.tick % 2) * 3, 0, 70) : val;
			SetPh(a, 0, dv, 0.6f);
			return;
		}

		if (vocal) {
			// "ahh" on the rise, rounded "oww" on the peak/fall; only the size scales.
			int ph = (p == 2 || p == 3) ? 11 : 0;
			if (enjEff >= 85 && (p == 1 || p == 2)) ph = 1;
			if (S::bActTypeAware && ActorHasAnyAction(t, s, T().actionOral)) ph = 11;
			if (p == 0) ResetPh(a, 0.6f);
			if (ph == 1) {
				SetPh(a, 1, val, 0.6f);
				SetPh(a, 11, val / 3, 0.6f);
				SetPh(a, 0, 0, 0.6f);
			} else if (ph == 11) {
				SetPh(a, 11, val, 0.6f);
				SetPh(a, 0, val / 4, 0.6f);
				SetPh(a, 1, 0, 0.6f);
			} else {
				SetPh(a, 0, val, 0.6f);
				SetPh(a, 11, val / 4, 0.6f);
				SetPh(a, 1, 0, 0.6f);
			}
			int eye = ph == 1 ? ClampI((val * 12) / 10, 0, 92) : ClampI((val * 9) / 10, 0, 85);
			if (val < 8) eye = 0;
			eye = std::max(eye, lidFloor);
			ApplyEyeSquint(a, eye, seed);
			if (p == 2 && val >= 45) {
				const int bp = ClampI((val - 45) / 2, 0, 16);
				SetMod(a, 22, BrowValue(bp), 0.55f);
				SetMod(a, 23, BrowValue(bp), 0.55f);
			}
		} else {
			const int br = (seed + cycleIdx) % 4;
			if ((arch == 1 || arch == 3 || enjEff < 45) && br == 0) {
				SetPh(a, 2, 14, 0.6f);  // lip-press / bite
			} else if (br == 1) {
				ResetPh(a, 0.55f);
				SetPh(a, 0, ClampI(6 + enjEff / 12, 0, 16), 0.55f);  // tiny breath between moans
			} else {
				ResetPh(a, 0.6f);
			}
			ApplyEyeSquint(a, ClampI(std::max(lidFloor, 10), 0, 40), seed);
		}
	}
}
