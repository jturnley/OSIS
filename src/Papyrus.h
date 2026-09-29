#pragma once

// Calls into other plugins' Papyrus natives (RaceMenu's NiOverride, OStim, OSL Aroused,
// SLO Aroused NG) through the script VM. This avoids depending on their C++ headers:
// the calls are queued on the VM and complete asynchronously, so anything that needs a
// result takes a callback. Call these from the game's main thread.
namespace Papyrus
{
	using Result = std::function<void(const RE::BSScript::Variable&)>;

	void CallStatic(const char* a_class, const char* a_fn, RE::BSScript::IFunctionArguments* a_args, Result a_done = {});
	void CallMethod(RE::TESForm* a_self, const char* a_class, const char* a_fn, RE::BSScript::IFunctionArguments* a_args, Result a_done = {});

	// ---- NiOverride body morphs
	void SetBodyMorph(RE::Actor* a_actor, const std::string& a_morph, const char* a_key, float a_value);
	void ClearBodyMorph(RE::Actor* a_actor, const std::string& a_morph, const char* a_key);
	void ClearBodyMorphKeys(RE::Actor* a_actor, const char* a_key);
	void UpdateModelWeight(RE::Actor* a_actor);

	// ---- NiOverride overlays. Node is e.g. "Body [Ovl6]" or "Face [Ovl1]". Overrides are
	// persisted so RaceMenu re-applies them when overlay nodes are (re)created.
	void AddOverlays(RE::Actor* a_actor);
	void SetOverlayMatte(RE::Actor* a_actor, bool a_female, const std::string& a_node);
	void SetOverlayTexture(RE::Actor* a_actor, bool a_female, const std::string& a_node, const std::string& a_texture);
	void SetOverlayTint(RE::Actor* a_actor, bool a_female, const std::string& a_node, std::int32_t a_rgb);
	void SetOverlayAlpha(RE::Actor* a_actor, bool a_female, const std::string& a_node, float a_alpha);
	void SetOverlayGloss(RE::Actor* a_actor, bool a_female, const std::string& a_node, float a_glossiness, float a_specular);
	void ClearOverlay(RE::Actor* a_actor, bool a_female, const std::string& a_node);

	// ---- OStim
	void GetThreadActors(std::int32_t a_thread, std::function<void(std::vector<RE::Actor*>)> a_done);
	void GetThreadScene(std::int32_t a_thread, std::function<void(std::string)> a_done);
	void GetThreadSpeed(std::int32_t a_thread, std::function<void(std::int32_t)> a_done);
	void IsThreadRunning(std::int32_t a_thread, std::function<void(bool)> a_done);
	void SetExpressionsEnabled(RE::Actor* a_actor, bool a_enabled, bool a_allowOverride = true);
	void HasExpressionOverride(RE::Actor* a_actor, std::function<void(bool)> a_done);
	void PlayExpression(RE::Actor* a_actor, const std::string& a_event, std::function<void(float)> a_done = {});
	void ClearExpression(RE::Actor* a_actor);
	void EquipObject(RE::Actor* a_actor, const char* a_type, std::function<void(bool)> a_done = {});
	void UnequipObject(RE::Actor* a_actor, const char* a_type);
	void GetVoiceSetName(RE::FormID a_baseID, std::function<void(std::string)> a_done);

	// ---- vanilla actor head look
	void SetLookAt(RE::Actor* a_actor, RE::TESObjectREFR* a_target);
	void ClearLookAt(RE::Actor* a_actor);

	// ---- Debug.Notification
	void Notify(const std::string& a_text);

	// ---- arousal mods: `float Fn(Actor)` globals
	void CallFloat(const char* a_class, const char* a_fn, RE::Actor* a_actor, std::function<void(float)> a_done);
}
