// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Hooks.h"

#include "Body.h"
#include "Face/Output.h"
#include "Settings.h"
#if !OSIS_LITE
#	include "Voice.h"
#endif

namespace Hooks
{
	namespace
	{
		std::atomic_bool g_npcHook{ false };
		std::atomic_bool g_playerHook{ false };
		std::atomic_bool g_playerViaJob{ false };

		// After the original has posed the skeleton for this frame, write the face and curl
		// the toes/fingers so neither the animation nor OStim's face updater overwrites them
		// before the frame renders.
		void AfterAnimation(RE::Actor* a_actor, float a_delta)
		{
			Face::Output::Update(a_actor, a_delta);
			Body::Update(a_actor, a_delta);
		}

		// The player is animated on the main thread through PlayerCharacter::UpdateAnimation
		// (vfunc 0x7D).
		struct PlayerUpdateAnimation
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				if (!g_playerViaJob.load(std::memory_order_relaxed)) Face::Output::Reassert(a_this);
				func(a_this, a_delta);
				if (!g_playerViaJob.load(std::memory_order_relaxed)) AfterAnimation(a_this, a_delta);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// NPCs are animated from parallel jobs (RunOneActorAnimationUpdateJob) that call the
		// internal update directly, never through the vtable, so a vfunc hook on Character
		// never sees them. This is the call Precision 1.x and Simple Timed Block hook.
		struct NPCUpdateAnimation
		{
			static void thunk(RE::Actor* a_this, float a_delta)
			{
				if (a_this && !(a_this->IsPlayerRef() && !g_playerViaJob.load(std::memory_order_relaxed))) Face::Output::Reassert(a_this);
				func(a_this, a_delta);
				if (!a_this) return;
				if (a_this->IsPlayerRef() && !g_playerViaJob.exchange(true)) {
					logger::info("The player is animated through the actor job too; using that path for them");
				}
				AfterAnimation(a_this, a_delta);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		void InstallNPC()
		{
			REL::Relocation<std::uintptr_t> job{ RELOCATION_ID(40436, 41453) };
			const auto site = job.address() + 0x74;
			const auto base = REL::Module::get().base();
			const auto op = *reinterpret_cast<const std::uint8_t*>(site);
			if (op != 0xE8) {
				logger::error("NPC animation hook not installed: expected a call at SkyrimSE+{:X}, found opcode {:02X}. "
							  "NPC faces fall back to 20 Hz main-thread writes; NPC toe/finger curl is off",
					site - base, op);
				return;
			}
			const auto target = site + 5 + *reinterpret_cast<const std::int32_t*>(site + 1);
			NPCUpdateAnimation::func = SKSE::GetTrampoline().write_call<5>(site, NPCUpdateAnimation::thunk);
			g_npcHook = true;
			logger::info("Installed NPC animation hook at SkyrimSE+{:X} (was calling {:X})", site - base, target - base);
		}
	}

	bool NPCHooked() { return g_npcHook.load(std::memory_order_relaxed); }
	bool PlayerHooked() { return g_playerHook.load(std::memory_order_relaxed); }

	void Install()
	{
		bool animation;
		{
			std::scoped_lock l(Settings::lock);
			animation = Settings::General::bAnimationHooks;
		}
		// VR is a different binary: the animation update sits at another vtable index, and the
		// NPC job's call site has no VR address at all. Hooking either there would patch the
		// wrong function. The 20 Hz main-thread path covers faces instead; toe and finger curl
		// need the per-frame hook, so they stay off until someone maps the VR addresses.
		if (animation && REL::Module::IsVR()) {
			animation = false;
			logger::warn("Skyrim VR: animation hooks not installed (no VR addresses for them). Faces are written at 20 Hz "
						 "from the main thread; toe and finger curl are off");
		}
		if (animation) {
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
			PlayerUpdateAnimation::func = vtbl.write_vfunc(0x7D, PlayerUpdateAnimation::thunk);
			g_playerHook = true;
			logger::info("Installed player animation hook");
			InstallNPC();
		} else {
			logger::warn("Animation hooks are off (bAnimationHooks): faces are written at 20 Hz from the main thread, toe/finger curl is off");
		}
#if !OSIS_LITE
		bool voice;
		{
			std::scoped_lock l(Settings::lock);
			voice = Settings::Voice::bEnabled;
		}
		// Same reasoning: the sound vtable index is a Special Edition one. The per-tick sweep
		// mutes a victim's moans on its own, a little later than the hook would.
		if (voice && REL::Module::IsVR()) {
			logger::warn("Skyrim VR: the moan-muting hook is not installed; the per-tick sweep handles it instead");
		} else if (voice) {
			Voice::InstallHooks();
		}
#endif
	}
}
