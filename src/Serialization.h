// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// SKSE cosave. Replaces the StorageUtil keys the Papyrus mods used:
//   "SLED_PersOverride" per NPC -> 'PERS' record
//   "OSED_DisabledActors" list  -> 'TKOV' record (actors whose OStim face writer we disabled)
namespace Serialization
{
	void Install();
}
