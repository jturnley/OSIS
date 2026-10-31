// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Hooks.h"

#include <intrin.h>

#include "Body.h"
#include "Face/Output.h"
#include "FaceWatch.h"
#include "MoanVolume.h"
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
				func(a_this, a_delta);
				if (!a_this) return;
				if (a_this->IsPlayerRef() && !g_playerViaJob.exchange(true)) {
					logger::info("The player is animated through the actor job too; using that path for them");
				}
				AfterAnimation(a_this, a_delta);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// The face node's own update is where the game turns the face keyframes into the values it
		// renders - not the actor's animation update, which is over by then (measured: 0 of 5440 frames
		// changed inside it). OStim writes the same keyframes from its own thread about every 50 ms, so
		// OSIS puts its values back over OStim's here, just before that read, instead of leaving a
		// whole frame for them to be overwritten in.
		struct FaceNodeUpdate
		{
			static void thunk(RE::BSFaceGenNiNode* a_this, RE::NiUpdateData& a_data, std::uint32_t a_arg2)
			{
				auto* data = a_this ? a_this->GetRuntimeData().animationData.get() : nullptr;
				Face::Output::TraceFace(data, 0, a_data.time, a_data.flags.underlying(), a_arg2);
				const float pre = Face::Output::ReassertFace(data);
				Face::Output::TraceFace(data, 1);
				func(a_this, a_data, a_arg2);
				Face::Output::TraceFace(data, 2);
				Face::Output::NoteFaceRead(data, pre);
				// A face this mod owns that was reset under that read (the 1.8.2 probe saw whole faces zeroed for one frame, and a 2.0 debug log shows it every few seconds on an
				// NPC): put it back and let the game read it again, so the frame shows ours and not a neutral face.
				if (Face::Output::FaceResetDuringRead(data)) {
					Face::Output::ReassertFace(data);
					func(a_this, a_data, a_arg2);
					Face::Output::TraceFace(data, 3);
					(void)Face::Output::FaceResetDuringRead(data, true);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// The game's face reset. A 2.0 debug trace caught faces OSIS owns going to zero between two face node updates - after the game had read them correctly and
		// before the next animation update - one channel or several at a time; the reset is the game's way to do that. The reset itself is hooked (the game's calls and
		// any mod's), so the debug log names the caller, and a reset of a face that is OSIS's alone is skipped (it would only show for a frame: OSIS writes the face again next update).
		struct FaceReset
		{
			static void thunk(RE::BSFaceGenAnimationData* a_this, float a_timer, bool a_expression, bool a_modifiers, bool a_custom, bool a_closeEyes)
			{
				if (Face::Output::KeepOnReset(a_this, reinterpret_cast<std::uintptr_t>(_ReturnAddress()), a_expression, a_modifiers)) return;
				func(a_this, a_timer, a_expression, a_modifiers, a_custom, a_closeEyes);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// The reset is hooked at its entry, so a mod calling it through a pointer is caught as well as the game's own calls. Its first instruction (mov [rsp+8], rbx, 5 bytes,
		// seen in 1.6.1170) is replaced by a jump to the thunk; the original runs from a gateway that holds that instruction and jumps back after it. Any other first
		// instruction - another runtime, or another mod hooking the reset already - and nothing is patched.
		void InstallFaceReset()
		{
			REL::Relocation<std::uintptr_t> reset{ RELOCATION_ID(25977, 26586) };
			const auto target = reset.address();
			const auto base = REL::Module::get().base();
			const auto* b = reinterpret_cast<const std::uint8_t*>(target);
			static constexpr std::uint8_t kFirst[5] = { 0x48, 0x89, 0x5C, 0x24, 0x08 };
			if (std::memcmp(b, kFirst, sizeof(kFirst)) != 0) {
				logger::warn("Face reset hook not installed: the reset at SkyrimSE+{:X} begins {:02X} {:02X} {:02X} {:02X} {:02X}, not the instruction expected", target - base, b[0], b[1],
					b[2], b[3], b[4]);
				return;
			}
			auto& tramp = SKSE::GetTrampoline();
			auto* gate = static_cast<std::uint8_t*>(tramp.allocate(sizeof(kFirst) + 14));
			std::memcpy(gate, kFirst, sizeof(kFirst));
			gate[5] = 0xFF;  // jmp [rip+0]
			gate[6] = 0x25;
			std::memset(gate + 7, 0, 4);
			const std::uintptr_t back = target + sizeof(kFirst);
			std::memcpy(gate + 11, &back, sizeof(back));
			FaceReset::func = reinterpret_cast<std::uintptr_t>(gate);
			tramp.write_branch<5>(target, FaceReset::thunk);
			logger::info("Installed face reset hook at its entry, SkyrimSE+{:X}", target - base);
		}

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
			REL::Relocation<std::uintptr_t> faceNode{ RE::VTABLE_BSFaceGenNiNode[0] };
			FaceNodeUpdate::func = faceNode.write_vfunc(0x2C, FaceNodeUpdate::thunk);
			logger::info("Installed face node update hook");
			InstallFaceReset();
			if constexpr (FaceWatch::kEnabled) FaceWatch::Start();
		} else {
			logger::warn("Animation hooks are off (bAnimationHooks): faces are written at 20 Hz from the main thread, toe/finger curl is off");
		}
		// The sound vtable index is a Special Edition one, so there is no hook on VR; MoanVolume::Tick corrects the sounds it sees instead. Installed before Voice's
		// hook, so that hook is the outer one and a mute still has the last word.
		if (!REL::Module::IsVR()) MoanVolume::InstallHook();
		else logger::warn("Skyrim VR: the moan volume hook is not installed; sounds are corrected a moment after they start");
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
