// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// Debug logging only: who writes a face value. A 2.0 debug trace showed faces OSIS owns zeroed between two of the game's face updates by something that never
// calls the game's face reset, so it writes the values directly. The CPU's four data breakpoints are set, in every thread of the game, on four face values that
// were seen zeroed (the closed-lips mouth shape, keyframe and rendered, of the first two scene faces); a write to one of them stops the writing thread for an
// instant, and the code address that wrote is recorded and logged with its module ([face] "write watch" lines, once a second). Nothing runs while debug
// logging is off.
//
// Off in builds: it found what it was built for (the face's arrays are replaced at stage changes, nothing writes zeros into them), and every watched write
// stops the writing thread. Set this to true to use it again.
namespace FaceWatch
{
	inline constexpr bool kEnabled = false;

	// The four watched addresses: 0/1 the player's (or the first face's) keyframe and rendered value, 2/3 the second face's. 0 clears a slot.
	void Want(int a_slot, const float* a_address, RE::FormID a_who, const char* a_what);
	void Start();
}
