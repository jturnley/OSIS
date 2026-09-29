#include "Hooks.h"

#include "Body.h"
#include "Face/Output.h"

namespace Hooks
{
	namespace
	{
		// Actor::UpdateAnimation (vfunc 0x7D). After the original has posed the skeleton
		// for this frame, write the face and curl the toes/fingers so neither the
		// animation nor OStim's face updater overwrites them before the frame renders.
		template <class T>
		struct UpdateAnimation
		{
			static void thunk(T* a_actor, float a_delta)
			{
				func(a_actor, a_delta);
				Face::Output::Update(a_actor, a_delta);
				Body::Update(a_actor, a_delta);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <class T>
		void Install(REL::VariantID a_vtbl)
		{
			REL::Relocation<std::uintptr_t> vtbl{ a_vtbl };
			UpdateAnimation<T>::func = vtbl.write_vfunc(0x7D, UpdateAnimation<T>::thunk);
		}
	}

	void Install()
	{
		Install<RE::Character>(RE::VTABLE_Character[0]);
		Install<RE::PlayerCharacter>(RE::VTABLE_PlayerCharacter[0]);
		logger::info("Installed animation update hooks");
	}
}
