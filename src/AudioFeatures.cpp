// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "AudioFeatures.h"

// This is the hot loop of the startup decode (every voice-set file, fifty frames a second), so it is built for speed even in the size-optimised build.
#pragma optimize("gt", on)

namespace AudioFeatures
{
	namespace
	{
		constexpr int kN = 256;  // FFT size, in samples at the decimated rate (about 23 ms)
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kWindowSeconds = 0.020f;  // pitch analysis window
		constexpr float kMinHz = 90.0f;
		constexpr float kMaxHz = 800.0f;
		constexpr float kCentroidLo = 150.0f;
		constexpr float kCentroidHi = 4500.0f;
		constexpr float kToneLo = 300.0f;   // tone 0
		constexpr float kToneHi = 3500.0f;  // tone 1
		constexpr float kSemitoneRange = 8.0f;

		struct Tables
		{
			std::array<float, kN / 2> cosT{};
			std::array<float, kN / 2> sinT{};
			std::array<std::uint16_t, kN> rev{};
			std::array<float, kN> hann{};

			Tables()
			{
				for (int i = 0; i < kN / 2; ++i) {
					cosT[i] = std::cos(2.0f * kPi * static_cast<float>(i) / kN);
					sinT[i] = std::sin(2.0f * kPi * static_cast<float>(i) / kN);
				}
				for (int i = 0; i < kN; ++i) {
					int r = 0;
					for (int b = 0; b < 8; ++b) {
						if (i & (1 << b)) r |= 1 << (7 - b);
					}
					rev[i] = static_cast<std::uint16_t>(r);
					hann[i] = 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) / (kN - 1));
				}
			}
		};

		const Tables& Tab()
		{
			static const Tables t;
			return t;
		}

		// In-place radix-2 FFT of kN complex values.
		void FFT(float* re, float* im)
		{
			const auto& t = Tab();
			for (int i = 0; i < kN; ++i) {
				const int j = t.rev[i];
				if (j > i) {
					std::swap(re[i], re[j]);
					std::swap(im[i], im[j]);
				}
			}
			for (int len = 2; len <= kN; len <<= 1) {
				const int half = len >> 1;
				const int step = kN / len;
				for (int i = 0; i < kN; i += len) {
					for (int k = 0; k < half; ++k) {
						const float wr = t.cosT[k * step];
						const float wi = -t.sinT[k * step];
						const int a = i + k;
						const int b = a + half;
						const float xr = re[b] * wr - im[b] * wi;
						const float xi = re[b] * wi + im[b] * wr;
						re[b] = re[a] - xr;
						im[b] = im[a] - xi;
						re[a] += xr;
						im[a] += xi;
					}
				}
			}
		}

		std::uint8_t Byte(float v)
		{
			return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
		}

		struct Pitch
		{
			float hz = 0.0f;
			float voiced = 0.0f;
		};

		// The normalised cross-correlation pitch (as in RAPT) of the window that starts a_start samples into a_z, at rate a_rate. Octave errors are guarded
		// by taking the shortest lag whose peak is nearly as strong as the strongest.
		Pitch EstimatePitch(const std::vector<float>& a_z, float a_rate, std::ptrdiff_t a_start)
		{
			Pitch out;
			const int w = std::clamp(static_cast<int>(kWindowSeconds * a_rate), 16, 200);
			const int lmin = std::max(2, static_cast<int>(a_rate / kMaxHz));
			const int lmax = std::min(static_cast<int>(a_rate / kMinHz) + 1, 160);
			if (lmax <= lmin + 2) return out;
			const int len = w + lmax + 2;
			std::array<float, 400> s{};
			if (len > static_cast<int>(s.size())) return out;
			const auto n = static_cast<std::ptrdiff_t>(a_z.size());
			double mean = 0.0;
			for (int i = 0; i < len; ++i) {
				const std::ptrdiff_t idx = a_start + i;
				s[i] = (idx >= 0 && idx < n) ? a_z[static_cast<std::size_t>(idx)] : 0.0f;
				if (i < w) mean += s[i];
			}
			const auto m = static_cast<float>(mean / w);
			for (int i = 0; i < len; ++i) s[i] -= m;

			// Energy of s[tau .. tau+w) for every tau, by a running sum.
			std::array<float, 400> energy{};
			float e = 0.0f;
			for (int i = 0; i < w; ++i) e += s[i] * s[i];
			energy[0] = e;
			for (int tau = 1; tau <= lmax + 1; ++tau) {
				e += s[tau + w - 1] * s[tau + w - 1] - s[tau - 1] * s[tau - 1];
				energy[tau] = std::max(e, 0.0f);
			}
			const float e0 = energy[0];
			if (e0 < 1.0e-8f * static_cast<float>(w)) return out;  // silence

			std::array<float, 170> nccf{};
			for (int tau = lmin - 1; tau <= lmax + 1; ++tau) {
				float num = 0.0f;
				for (int i = 0; i < w; ++i) num += s[i] * s[i + tau];
				nccf[tau] = num / std::sqrt(e0 * energy[tau] + 1.0e-12f);
			}
			float best = 0.0f;
			for (int tau = lmin; tau <= lmax; ++tau) best = std::max(best, nccf[tau]);
			if (best < 0.30f) {
				out.voiced = std::max(best, 0.0f);
				return out;
			}
			int pick = -1;
			for (int tau = lmin; tau <= lmax; ++tau) {
				if (nccf[tau] >= 0.85f * best && nccf[tau] >= nccf[tau - 1] && nccf[tau] > nccf[tau + 1]) {
					pick = tau;
					break;
				}
			}
			if (pick < 0) return out;
			const float a = nccf[pick - 1], b = nccf[pick], c = nccf[pick + 1];
			const float denom = a - 2.0f * b + c;
			const float delta = std::abs(denom) > 1.0e-6f ? std::clamp(0.5f * (a - c) / denom, -0.5f, 0.5f) : 0.0f;
			out.hz = a_rate / (static_cast<float>(pick) + delta);
			out.voiced = std::clamp(b, 0.0f, 1.0f);
			return out;
		}
	}

	Tracks Analyse(const std::vector<float>& a_mono, std::uint32_t a_rate)
	{
		Tracks out;
		if (a_mono.empty() || a_rate < 4000) return out;

		// Down to about 11 kHz (a box average of D samples: cheap, and plenty for everything below 4.5 kHz), and once more for the pitch.
		const int d = std::max(1, static_cast<int>((a_rate + 5512u) / 11025u));
		const float rate = static_cast<float>(a_rate) / static_cast<float>(d);
		std::vector<float> y(a_mono.size() / static_cast<std::size_t>(d));
		for (std::size_t j = 0; j < y.size(); ++j) {
			float sum = 0.0f;
			for (int k = 0; k < d; ++k) sum += a_mono[j * static_cast<std::size_t>(d) + static_cast<std::size_t>(k)];
			y[j] = sum / static_cast<float>(d);
		}
		if (y.size() < 32) return out;
		std::vector<float> z(y.size() / 2);
		for (std::size_t j = 0; j < z.size(); ++j) z[j] = 0.5f * (y[2 * j] + y[2 * j + 1]);
		const float rateZ = rate * 0.5f;

		const auto frames = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(static_cast<float>(a_mono.size()) / static_cast<float>(a_rate) / kFrameSeconds)));
		std::vector<float> centroid(frames, 0.0f);
		std::vector<float> rms(frames, 0.0f);
		std::vector<float> hz(frames, 0.0f);
		std::vector<float> voiced(frames, 0.0f);

		const float binHz = rate / kN;
		const int klo = std::max(1, static_cast<int>(std::ceil(kCentroidLo / binHz)));
		const int khi = std::min(kN / 2 - 1, static_cast<int>(std::min(kCentroidHi, 0.45f * rate) / binHz));
		const auto& tab = Tab();

		// Two frames per FFT: one in the real part, one in the imaginary, told apart afterwards by the symmetry of a real signal's spectrum.
		std::array<float, kN> re{}, im{};
		for (std::size_t f = 0; f < frames; f += 2) {
			std::array<std::ptrdiff_t, 2> start{};
			std::array<float, 2> energy{};
			for (int half = 0; half < 2; ++half) {
				const std::size_t fi = f + static_cast<std::size_t>(half);
				float* dst = half == 0 ? re.data() : im.data();
				const auto centre = static_cast<std::ptrdiff_t>((static_cast<float>(fi) + 0.5f) * kFrameSeconds * rate);
				start[half] = centre - kN / 2;
				float sq = 0.0f;
				for (int i = 0; i < kN; ++i) {
					const std::ptrdiff_t idx = start[half] + i;
					const float v = (fi < frames && idx >= 0 && idx < static_cast<std::ptrdiff_t>(y.size())) ? y[static_cast<std::size_t>(idx)] : 0.0f;
					sq += v * v;
					dst[i] = v * tab.hann[i];
				}
				energy[half] = sq;
			}
			FFT(re.data(), im.data());
			std::array<double, 2> sumP{}, sumFP{};
			for (int k = klo; k <= khi; ++k) {
				const int nk = kN - k;
				const float xr = re[k], xi = im[k], yr = re[nk], yi = im[nk];
				const float pa = ((xr + yr) * (xr + yr) + (xi - yi) * (xi - yi)) * 0.25f;
				const float pb = ((xi + yi) * (xi + yi) + (xr - yr) * (xr - yr)) * 0.25f;
				sumP[0] += pa;
				sumP[1] += pb;
				sumFP[0] += static_cast<double>(k) * pa;
				sumFP[1] += static_cast<double>(k) * pb;
			}
			for (int half = 0; half < 2; ++half) {
				const std::size_t fi = f + static_cast<std::size_t>(half);
				if (fi >= frames) break;
				rms[fi] = std::sqrt(energy[half] / kN);
				centroid[fi] = sumP[half] > 1.0e-12 ? static_cast<float>(sumFP[half] / sumP[half]) * binHz : 0.0f;
				const auto zs = static_cast<std::ptrdiff_t>(((static_cast<float>(fi) + 0.5f) * kFrameSeconds - 0.5f * kWindowSeconds) * rateZ);
				const auto p = EstimatePitch(z, rateZ, zs);
				hz[fi] = p.hz;
				voiced[fi] = p.voiced;
			}
		}

		// Which frames are sound rather than the noise floor: against the file's own loud parts.
		auto sorted = rms;
		std::ranges::sort(sorted);
		const float p90 = sorted[static_cast<std::size_t>(0.90f * static_cast<float>(sorted.size() - 1))];
		const float gate = std::max(0.15f * p90, 1.0e-4f);
		std::vector<char> active(frames, 0);
		for (std::size_t f = 0; f < frames; ++f) active[f] = rms[f] > gate;

		// The median voiced pitch, and the pitch track smoothed by a three-frame median (a lone octave error is gone).
		std::vector<float> smooth = hz;
		for (std::size_t f = 1; f + 1 < frames; ++f) {
			if (hz[f] <= 0.0f) continue;
			const float lo = hz[f - 1] > 0.0f ? hz[f - 1] : hz[f];
			const float hi = hz[f + 1] > 0.0f ? hz[f + 1] : hz[f];
			smooth[f] = std::max(std::min(lo, hi), std::min(std::max(lo, hi), hz[f]));
		}
		std::vector<float> pool;
		for (std::size_t f = 0; f < frames; ++f) {
			if (active[f] && voiced[f] >= 0.60f && smooth[f] > 0.0f) pool.push_back(smooth[f]);
		}
		float median = 0.0f;
		if (pool.size() >= 3) {
			std::ranges::nth_element(pool, pool.begin() + static_cast<std::ptrdiff_t>(pool.size() / 2));
			median = pool[pool.size() / 2];
		}
		out.medianHz = median;

		out.tone.resize(frames);
		out.pitch.resize(frames);
		out.voiced.resize(frames);
		const float toneSpan = std::log2(kToneHi / kToneLo);
		float tone = 0.5f;
		for (std::size_t f = 0; f < frames; ++f) {
			if (active[f] && centroid[f] > 0.0f) tone = std::clamp(std::log2(std::max(centroid[f], 1.0f) / kToneLo) / toneSpan, 0.0f, 1.0f);
			out.tone[f] = Byte(tone);
			out.voiced[f] = active[f] ? Byte(voiced[f]) : 0;
			float p = 0.5f;
			if (active[f] && median > 0.0f && voiced[f] >= 0.45f && smooth[f] > 0.0f) {
				const float semis = 12.0f * std::log2(smooth[f] / median);
				p = 0.5f + 0.5f * std::clamp(semis / kSemitoneRange, -1.0f, 1.0f);
			}
			out.pitch[f] = static_cast<std::uint8_t>(std::clamp(p * 255.0f + 0.5f, 0.0f, 255.0f));
		}
		return out;
	}
}
