// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// Which RaceMenu overlay slots are already spoken for.
//
// RaceMenu gives every actor a fixed set of "Body [Ovl#]" and "Face [Ovl#]" nodes, and any mod
// may write a texture into any of them. Nothing arbitrates: the last writer wins, and two mods
// on one slot is how an overlay ends up black, missing, or wearing someone else's texture.
//
// So rather than trusting a configured slot number, the state is read back off the actor's own
// 3D before claiming anything. That sees every mod's work - distributed by Overlay Distribution
// Framework, applied by an ahegao mod, painted in RaceMenu by hand - because it reads the result
// rather than anybody's configuration. The limit is timing, not authorship: an overlay applied
// after we look is invisible to us until the next rebuild.
namespace Overlays
{
	// The actor's slots, from `a_first` upward, that nothing else is using. Slots already holding
	// one of `a_ours` count as free, since re-claiming our own is what a rebuild does. Returns at
	// most `a_count`, fewer when there are not enough to go round, and falls back to the plain
	// numbering when the actor's 3D cannot be read.
	[[nodiscard]] std::vector<int> Claim(RE::Actor* a_actor, bool a_face, int a_count, int a_first, int a_total,
		const std::vector<std::string>& a_ours);

	// For the Status page: "3 of 12 taken by other mods".
	[[nodiscard]] std::string Report(RE::Actor* a_actor, bool a_face, int a_total, const std::vector<std::string>& a_ours);

	// Whether the named overlay node (e.g. "Body [Ovl7]") on this actor is showing a_texture, which is how a caller checks that what it painted is
	// still there instead of painting it again to be sure. nullopt when the actor's 3D is not loaded (no way to tell); false when the node is
	// gone or shows something else.
	[[nodiscard]] std::optional<bool> NodeHolds(RE::Actor* a_actor, const std::string& a_node, const std::string& a_texture);
}
