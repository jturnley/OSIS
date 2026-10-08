// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Papyrus.h"

namespace Papyrus
{
	namespace
	{
		using Callback = RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>;

		class Functor final : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit Functor(Result a_fn) : fn(std::move(a_fn)) {}

			void operator()(RE::BSScript::Variable a_result) override
			{
				if (fn) fn(a_result);
			}

			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			Result fn;
		};

		Callback MakeCallback(Result a_done)
		{
			return a_done ? Callback{ new Functor(std::move(a_done)) } : Callback{};
		}

		RE::TESObjectREFR* Ref(RE::Actor* a) { return static_cast<RE::TESObjectREFR*>(a); }

		float AsFloat(const RE::BSScript::Variable& v)
		{
			if (v.IsFloat()) return v.GetFloat();
			if (v.IsInt()) return static_cast<float>(v.GetSInt());
			return 0.0f;
		}

		std::int32_t AsInt(const RE::BSScript::Variable& v)
		{
			if (v.IsInt()) return v.GetSInt();
			if (v.IsFloat()) return static_cast<std::int32_t>(v.GetFloat());
			return 0;
		}

		bool AsBool(const RE::BSScript::Variable& v)
		{
			if (v.IsBool()) return v.GetBool();
			return AsInt(v) != 0;
		}

		std::string AsString(const RE::BSScript::Variable& v)
		{
			if (!v.IsString()) return {};
			return std::string(v.GetString());
		}

		std::vector<std::string> AsStrings(const RE::BSScript::Variable& v)
		{
			std::vector<std::string> out;
			if (!v.IsArray()) return out;
			const auto arr = v.GetArray();
			if (!arr) return out;
			for (std::uint32_t i = 0; i < arr->size(); ++i) {
				if (const auto& e = (*arr)[i]; e.IsString()) out.emplace_back(e.GetString());
			}
			return out;
		}
	}

	void CallStatic(const char* a_class, const char* a_fn, RE::BSScript::IFunctionArguments* a_args, Result a_done)
	{
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) {
			delete a_args;
			return;
		}
		auto cb = MakeCallback(std::move(a_done));
		vm->DispatchStaticCall(a_class, a_fn, a_args, cb);
	}

	void CallMethod(RE::TESForm* a_self, const char* a_class, const char* a_fn, RE::BSScript::IFunctionArguments* a_args, Result a_done)
	{
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm || !a_self) {
			delete a_args;
			return;
		}
		auto* policy = vm->GetObjectHandlePolicy();
		const auto handle = policy->GetHandleForObject(a_self->GetFormType(), a_self);
		if (handle == policy->EmptyHandle()) {
			delete a_args;
			return;
		}
		auto cb = MakeCallback(std::move(a_done));
		vm->DispatchMethodCall(handle, a_class, a_fn, a_args, cb);
	}

	// ------------------------------------------------------------ NiOverride
	void SetBodyMorph(RE::Actor* a_actor, const std::string& a_morph, const char* a_key, float a_value)
	{
		CallStatic("NiOverride", "SetBodyMorph",
			RE::MakeFunctionArguments(Ref(a_actor), RE::BSFixedString(a_morph), RE::BSFixedString(a_key), std::move(a_value)));
	}

	void ClearBodyMorph(RE::Actor* a_actor, const std::string& a_morph, const char* a_key)
	{
		CallStatic("NiOverride", "ClearBodyMorph",
			RE::MakeFunctionArguments(Ref(a_actor), RE::BSFixedString(a_morph), RE::BSFixedString(a_key)));
	}

	void ClearBodyMorphKeys(RE::Actor* a_actor, const char* a_key)
	{
		CallStatic("NiOverride", "ClearBodyMorphKeys", RE::MakeFunctionArguments(Ref(a_actor), RE::BSFixedString(a_key)));
	}

	void UpdateModelWeight(RE::Actor* a_actor)
	{
		CallStatic("NiOverride", "UpdateModelWeight", RE::MakeFunctionArguments(Ref(a_actor)));
	}

	namespace
	{
		// NiOverride shader keys.
		constexpr std::int32_t kEmissiveColor = 0;
		constexpr std::int32_t kGlossiness = 2;
		constexpr std::int32_t kSpecular = 3;
		constexpr std::int32_t kTintColor = 7;
		constexpr std::int32_t kAlpha = 8;
		constexpr std::int32_t kTexture = 9;
		constexpr auto kDefaultOverlay = "actors\\character\\overlays\\default.dds";
	}

	void AddOverlays(RE::Actor* a_actor)
	{
		CallStatic("NiOverride", "AddOverlays", RE::MakeFunctionArguments(Ref(a_actor)));
	}

	// Overrides must be persisted: NPC overlay nodes are created by AddOverlays after
	// these calls, and RaceMenu only replays stored overrides onto new nodes.
	void SetOverlayTexture(RE::Actor* a_actor, bool a_female, const std::string& a_node, const std::string& a_texture)
	{
		CallStatic("NiOverride", "AddNodeOverrideString",
			RE::MakeFunctionArguments(Ref(a_actor), std::move(a_female), RE::BSFixedString(a_node), std::int32_t{ kTexture },
				std::int32_t{ 0 }, RE::BSFixedString(a_texture), true));
	}

	void SetOverlayTint(RE::Actor* a_actor, bool a_female, const std::string& a_node, std::int32_t a_rgb)
	{
		CallStatic("NiOverride", "AddNodeOverrideInt",
			RE::MakeFunctionArguments(Ref(a_actor), std::move(a_female), RE::BSFixedString(a_node), std::int32_t{ kTintColor },
				std::int32_t{ -1 }, std::move(a_rgb), true));
	}

	void SetOverlayAlpha(RE::Actor* a_actor, bool a_female, const std::string& a_node, float a_alpha)
	{
		CallStatic("NiOverride", "AddNodeOverrideFloat",
			RE::MakeFunctionArguments(Ref(a_actor), std::move(a_female), RE::BSFixedString(a_node), std::int32_t{ kAlpha },
				std::int32_t{ -1 }, std::move(a_alpha), true));
	}

	void SetOverlayGloss(RE::Actor* a_actor, bool a_female, const std::string& a_node, float a_glossiness, float a_specular)
	{
		CallStatic("NiOverride", "AddNodeOverrideFloat",
			RE::MakeFunctionArguments(Ref(a_actor), std::move(a_female), RE::BSFixedString(a_node), std::int32_t{ kGlossiness },
				std::int32_t{ -1 }, std::move(a_glossiness), true));
		CallStatic("NiOverride", "AddNodeOverrideFloat",
			RE::MakeFunctionArguments(Ref(a_actor), std::move(a_female), RE::BSFixedString(a_node), std::int32_t{ kSpecular },
				std::int32_t{ -1 }, std::move(a_specular), true));
	}

	void SetOverlayMatte(RE::Actor* a_actor, bool a_female, const std::string& a_node)
	{
		// No glow or shine, so the overlay reads as colour in the skin rather than a sheen.
		CallStatic("NiOverride", "AddNodeOverrideInt",
			RE::MakeFunctionArguments(Ref(a_actor), std::move(a_female), RE::BSFixedString(a_node), std::int32_t{ kEmissiveColor },
				std::int32_t{ -1 }, std::int32_t{ 0 }, true));
		SetOverlayGloss(a_actor, a_female, a_node, 0.0f, 0.0f);
	}

	void ClearOverlay(RE::Actor* a_actor, bool a_female, const std::string& a_node)
	{
		// Blank the slot (default texture, invisible) so other mods see it as free.
		// Deliberately no RemoveNodeOverride: VM calls are not strictly ordered, and a
		// late Remove would wipe an overlay that was just re-added to the same slot.
		SetOverlayAlpha(a_actor, a_female, a_node, 0.0f);
		SetOverlayTexture(a_actor, a_female, a_node, kDefaultOverlay);
	}

	// ------------------------------------------------------------ OStim
	void GetThreadActors(std::int32_t a_thread, std::function<void(std::vector<RE::Actor*>)> a_done)
	{
		CallStatic("OThread", "GetActors", RE::MakeFunctionArguments(std::move(a_thread)),
			[done = std::move(a_done)](const RE::BSScript::Variable& v) {
				std::vector<RE::Actor*> actors;
				if (v.IsArray()) actors = RE::BSScript::UnpackValue<std::vector<RE::Actor*>>(&v);
				done(std::move(actors));
			});
	}

	void GetThreadScene(std::int32_t a_thread, std::function<void(std::string)> a_done)
	{
		CallStatic("OThread", "GetScene", RE::MakeFunctionArguments(std::move(a_thread)),
			[done = std::move(a_done)](const RE::BSScript::Variable& v) { done(AsString(v)); });
	}

	void GetThreadSpeed(std::int32_t a_thread, std::function<void(std::int32_t)> a_done)
	{
		CallStatic("OThread", "GetSpeed", RE::MakeFunctionArguments(std::move(a_thread)),
			[done = std::move(a_done)](const RE::BSScript::Variable& v) { done(AsInt(v)); });
	}

	void IsThreadRunning(std::int32_t a_thread, std::function<void(bool)> a_done)
	{
		CallStatic("OThread", "IsRunning", RE::MakeFunctionArguments(std::move(a_thread)),
			[done = std::move(a_done)](const RE::BSScript::Variable& v) { done(AsBool(v)); });
	}

	void SetExpressionsEnabled(RE::Actor* a_actor, bool a_enabled, bool a_allowOverride)
	{
		CallStatic("OActor", "SetExpressionsEnabled", RE::MakeFunctionArguments(std::move(a_actor), std::move(a_enabled), std::move(a_allowOverride)));
	}

	void HasExpressionOverride(RE::Actor* a_actor, std::function<void(bool)> a_done)
	{
		CallStatic("OActor", "HasExpressionOverride", RE::MakeFunctionArguments(std::move(a_actor)),
			[done = std::move(a_done)](const RE::BSScript::Variable& v) { done(AsBool(v)); });
	}

	void PlayExpression(RE::Actor* a_actor, const std::string& a_event, std::function<void(float)> a_done)
	{
		Result cb;
		if (a_done) cb = [done = std::move(a_done)](const RE::BSScript::Variable& v) { done(AsFloat(v)); };
		CallStatic("OActor", "PlayExpression", RE::MakeFunctionArguments(std::move(a_actor), RE::BSFixedString(a_event)), std::move(cb));
	}

	void ClearExpression(RE::Actor* a_actor)
	{
		CallStatic("OActor", "ClearExpression", RE::MakeFunctionArguments(std::move(a_actor)));
	}

	void EquipObject(RE::Actor* a_actor, const char* a_type, std::function<void(bool)> a_done)
	{
		Result cb;
		if (a_done) cb = [done = std::move(a_done)](const RE::BSScript::Variable& v) { done(AsBool(v)); };
		CallStatic("OActor", "EquipObject", RE::MakeFunctionArguments(std::move(a_actor), RE::BSFixedString(a_type)), std::move(cb));
	}

	void UnequipObject(RE::Actor* a_actor, const char* a_type)
	{
		CallStatic("OActor", "UnequipObject", RE::MakeFunctionArguments(std::move(a_actor), RE::BSFixedString(a_type)));
	}

	void IsObjectEquipped(RE::Actor* a_actor, const char* a_type, std::function<void(bool)> a_done)
	{
		CallStatic("OActor", "IsObjectEquipped", RE::MakeFunctionArguments(std::move(a_actor), RE::BSFixedString(a_type)),
			[done = std::move(a_done)](const RE::BSScript::Variable& v) { done(AsBool(v)); });
	}

	void GetVoiceSetName(RE::FormID a_baseID, std::function<void(std::string)> a_done)
	{
		CallStatic("OData", "GetVoiceSetName", RE::MakeFunctionArguments(static_cast<std::int32_t>(a_baseID)),
			[done = std::move(a_done)](const RE::BSScript::Variable& v) { done(AsString(v)); });
	}

	void SetExcitementMultiplier(RE::Actor* a_actor, float a_multiplier)
	{
		CallStatic("OActor", "SetExcitementMultiplier", RE::MakeFunctionArguments(std::move(a_actor), std::move(a_multiplier)));
	}

	void ModifyExcitement(RE::Actor* a_actor, float a_amount)
	{
		CallStatic("OActor", "ModifyExcitement", RE::MakeFunctionArguments(std::move(a_actor), std::move(a_amount), false));
	}

	void StallClimax(RE::Actor* a_actor)
	{
		CallStatic("OActor", "StallClimax", RE::MakeFunctionArguments(std::move(a_actor)));
	}

	void PermitClimax(RE::Actor* a_actor)
	{
		CallStatic("OActor", "PermitClimax", RE::MakeFunctionArguments(std::move(a_actor)));
	}

	void ForceClimax(RE::Actor* a_actor)
	{
		CallStatic("OActor", "Climax", RE::MakeFunctionArguments(std::move(a_actor), true));
	}

	// ------------------------------------------------------------ head look
	void SetLookAt(RE::Actor* a_actor, RE::TESObjectREFR* a_target)
	{
		CallMethod(a_actor, "Actor", "SetLookAt", RE::MakeFunctionArguments(std::move(a_target), false));
	}

	void ClearLookAt(RE::Actor* a_actor)
	{
		CallMethod(a_actor, "Actor", "ClearLookAt", RE::MakeFunctionArguments());
	}

	// ------------------------------------------------------------ OStim scene choice
	void GetScenesInRange(const std::string& a_scene, std::vector<RE::Actor*> a_actors, std::int32_t a_distance, std::function<void(std::vector<std::string>)> a_done)
	{
		CallStatic("OLibrary", "GetScenesInRange", RE::MakeFunctionArguments(RE::BSFixedString(a_scene), std::move(a_actors), std::move(a_distance)),
			[done = std::move(a_done)](const RE::BSScript::Variable& v) { done(AsStrings(v)); });
	}

	void GetRandomFurnitureSceneWithAnyTag(std::vector<RE::Actor*> a_actors, const std::string& a_furniture, const std::string& a_tagsCSV, std::function<void(std::string)> a_done)
	{
		CallStatic("OLibrary", "GetRandomFurnitureSceneWithAnySceneTagCSV",
			RE::MakeFunctionArguments(std::move(a_actors), RE::BSFixedString(a_furniture), RE::BSFixedString(a_tagsCSV)),
			[done = std::move(a_done)](const RE::BSScript::Variable& v) { done(AsString(v)); });
	}

	void GetFurnitureType(std::int32_t a_thread, std::function<void(std::string)> a_done)
	{
		CallStatic("OThread", "GetFurnitureType", RE::MakeFunctionArguments(std::move(a_thread)),
			[done = std::move(a_done)](const RE::BSScript::Variable& v) { done(AsString(v)); });
	}

	void GetThreadMetadata(std::int32_t a_thread, std::function<void(std::vector<std::string>)> a_done)
	{
		CallStatic("OThread", "GetMetadata", RE::MakeFunctionArguments(std::move(a_thread)),
			[done = std::move(a_done)](const RE::BSScript::Variable& v) { done(AsStrings(v)); });
	}

	void NavigateTo(std::int32_t a_thread, const std::string& a_scene)
	{
		CallStatic("OThread", "NavigateTo", RE::MakeFunctionArguments(std::move(a_thread), RE::BSFixedString(a_scene)));
	}

	void WarpTo(std::int32_t a_thread, const std::string& a_scene, bool a_fades)
	{
		CallStatic("OThread", "WarpTo", RE::MakeFunctionArguments(std::move(a_thread), RE::BSFixedString(a_scene), std::move(a_fades)));
	}

	void IsInAutoMode(std::int32_t a_thread, std::function<void(bool)> a_done)
	{
		CallStatic("OThread", "IsInAutoMode", RE::MakeFunctionArguments(std::move(a_thread)),
			[done = std::move(a_done)](const RE::BSScript::Variable& v) { done(AsBool(v)); });
	}

	void StartAutoMode(std::int32_t a_thread)
	{
		CallStatic("OThread", "StartAutoMode", RE::MakeFunctionArguments(std::move(a_thread)));
	}

	void StopAutoMode(std::int32_t a_thread)
	{
		CallStatic("OThread", "StopAutoMode", RE::MakeFunctionArguments(std::move(a_thread)));
	}

	// ------------------------------------------------------------ victim voice
	void StartCombat(RE::Actor* a_who, RE::Actor* a_target)
	{
		CallMethod(a_who, "Actor", "StartCombat", RE::MakeFunctionArguments(std::move(a_target)));
	}

	void StopCombat(RE::Actor* a_actor)
	{
		CallMethod(a_actor, "Actor", "StopCombat", RE::MakeFunctionArguments());
	}

	void AddCrimeGold(RE::TESFaction* a_crimeFaction, std::int32_t a_gold, bool a_violent)
	{
		CallMethod(a_crimeFaction, "Faction", "ModCrimeGold", RE::MakeFunctionArguments(std::move(a_gold), std::move(a_violent)));
	}

	void MuteOStim(RE::Actor* a_actor)
	{
		CallStatic("OActor", "Mute", RE::MakeFunctionArguments(std::move(a_actor)));
	}

	void Notify(const std::string& a_text)
	{
		CallStatic("Debug", "Notification", RE::MakeFunctionArguments(RE::BSFixedString(a_text)));
	}

	// ------------------------------------------------------------ arousal
	void CallFloat(const char* a_class, const char* a_fn, RE::Actor* a_actor, std::function<void(float)> a_done)
	{
		// Only numeric results count. Softbody Arousal read a None result (a failed or
		// interrupted call) as arousal 0, which could snap an actor to rest mid-scene.
		CallStatic(a_class, a_fn, RE::MakeFunctionArguments(std::move(a_actor)),
			[done = std::move(a_done)](const RE::BSScript::Variable& v) {
				if (v.IsFloat() || v.IsInt()) done(AsFloat(v));
			});
	}
}
