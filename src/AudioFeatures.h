// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// What a moan sounds like, frame by frame, beyond how loud it is: the pitch it rises and falls through, how bright or dark its
// tone is, and whether it is voiced or just breath. Lip-Sync (LipSync.cpp) runs this on every voice-set file it decodes, and
// Face::Output reads the tracks while the moan plays to shape the mouth and move the brows and eyes. Standalone: the standard
// library only, no engine types.
namespace AudioFeatures
{
	// One value per frame, every kFrameSeconds from the start of the file.
	constexpr float kFrameSeconds = 0.02f;

	struct Tracks
	{
		// The centre of the spectrum (the spectral centroid between 150 Hz and 4.5 kHz) on a log scale: 0 at 300 Hz and below (a closed-mouth hum), 1
		// at 3.5 kHz and above (a thin, spread "ee" or hiss). 255 = 1. Held through the quiet parts, where it would only read noise.
		std::vector<std::uint8_t> tone;
		// The pitch against the file's own median voiced pitch: 128 is the median, 255 is 8 semitones above it, 0 is 8 below. 128 where unvoiced.
		std::vector<std::uint8_t> pitch;
		// How voiced the frame is: the peak of the normalised autocorrelation over 90-800 Hz. 255 is a clean tone, 0 is breath or noise.
		std::vector<std::uint8_t> voiced;
		// The file's median voiced pitch in Hz (0: none found), for the log.
		float medianHz = 0.0f;
	};

	// a_mono: samples in -1..1 at a_rate Hz.
	[[nodiscard]] Tracks Analyse(const std::vector<float>& a_mono, std::uint32_t a_rate);
}
