#pragma once

// SKSE cosave. Replaces the StorageUtil keys the Papyrus mods used:
//   "SLED_PersOverride" per NPC -> 'PERS' record
//   "OSED_DisabledActors" list  -> 'TKOV' record (actors whose OStim face writer we disabled)
namespace Serialization
{
	void Install();
}
