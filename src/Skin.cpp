// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Skin.h"

#include "Arousal.h"
#include "Compat.h"

#include "Face/Engine.h"
#include "Face/Output.h"
#include "Papyrus.h"
#include "Scenes.h"
#include "Scheduler.h"
#include "Settings.h"

namespace Skin
{
	namespace
	{
		enum Effect : int { kBlush = 0, kSaliva, kTear, kCount };

		constexpr std::array<std::int32_t, kCount> kTint{ 0xE59A9A, 0xFFFFFF, 0xFFFFFF };
		constexpr auto kAutoBlush = "actors\\character\\Overlays\\FMS\\Blush\\Blush Cheeks 1.dds";

		struct State
		{
			bool female = false;
			bool distress = false;
			bool victim = false;     // the submissive actor of a non-consensual scene
			bool broken = false;     // a victim who climaxed: tears keep coming, the face doesn't react
			float nextTear = 0.0f;   // earliest time the next tear may start
			std::array<bool, kCount> on{};
			std::array<float, kCount> until{};
			float blushAlpha = 0.0f;
			int salivaCount = 0;
			float salivaCooldown = 0.0f;
			bool emoTears = false;   // carries Emotional Tears Effect's ability
		};

		std::mutex g_lock;
		std::unordered_map<RE::FormID, State> g_states;
		int g_faceSlots = 3;
		bool g_autoBlush = false;
		std::string g_status = "idle";
		RE::SpellItem* g_emoTears = nullptr;     // EmoTearsSpells.esp zzTearsTestAbility
		std::vector<RE::FormID> g_emoLeftovers;  // abilities a save made mid-scene left behind

		std::string Normalize(std::string p)
		{
			if (_strnicmp(p.c_str(), "data\\", 5) == 0 || _strnicmp(p.c_str(), "data/", 5) == 0) p.erase(0, 5);
			if (_strnicmp(p.c_str(), "textures\\", 9) == 0 || _strnicmp(p.c_str(), "textures/", 9) == 0) p.erase(0, 9);
			return p;
		}

		std::string Path(int effect)
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::Skin;
			switch (effect) {
			case kBlush: return !sBlushPath.empty() ? Normalize(sBlushPath) : (g_autoBlush ? kAutoBlush : "");
			case kSaliva: return Normalize(sSalivaPath);
			case kTear: return Normalize(sTearPath);
			default: return {};
			}
		}

		// Effects with a texture get consecutive slots from iFaceFirstSlot, in effect order.
		int SlotOf(int effect)
		{
			int slot;
			{
				std::scoped_lock l(Settings::lock);
				slot = Settings::Skin::iFaceFirstSlot;
			}
			for (int e = 0; e < effect; ++e) {
				if (!Path(e).empty()) ++slot;
			}
			return slot < g_faceSlots ? slot : -1;
		}

		std::string Node(int slot) { return std::format("Face [Ovl{}]", slot); }

		float StyleScalar()
		{
			if (!Settings::Skin::bStyleGated) return 1.0f;
			const float style = Settings::StyleValue();
			if (style < 0.5f) return 0.45f;
			if (style < 1.5f) return 0.75f;
			return 1.0f;
		}

		bool Paintable(RE::Actor* a, bool& female)
		{
			std::scoped_lock l(Settings::lock);
			if (Compat::Disabled(Compat::kSkin) || !Settings::Skin::bEnabled || !Settings::General::bEnabled || !a || a->IsDead() || a->IsChild() || !a->Is3DLoaded()) return false;
			if (!Face::Engine::IsHuman(a)) return false;
			auto* base = a->GetActorBase();
			female = base && base->GetSex() == RE::SEX::kFemale;
			return !(Settings::Skin::bFemaleOnly && !female);
		}

		void Apply(RE::Actor* a, State& st, int effect, float alpha, float holdSeconds)
		{
			const auto path = Path(effect);
			const int slot = SlotOf(effect);
			if (path.empty()) {
				g_status = std::format("no texture for effect {}", effect);
				return;
			}
			if (slot < 0) {
				g_status = std::format("no free face overlay slot (skee64.ini has {})", g_faceSlots);
				return;
			}
			const auto node = Node(slot);
			Papyrus::AddOverlays(a);
			Papyrus::SetOverlayTexture(a, st.female, node, path);
			Papyrus::SetOverlayTint(a, st.female, node, kTint[effect]);
			if (effect == kBlush && Settings::Skin::bMatteOverlays) Papyrus::SetOverlayMatte(a, st.female, node);
			Papyrus::SetOverlayAlpha(a, st.female, node, std::clamp(alpha, 0.0f, 1.0f));
			st.on[effect] = true;
			st.until[effect] = holdSeconds > 0.0f ? Scenes::Now() + holdSeconds : 0.0f;
			g_status = std::format("{}: {} {:.0f}%", a->GetDisplayFullName(), std::array{ "blush", "saliva", "tear" }[effect], alpha * 100.0f);
		}

		void Clear(RE::Actor* a, State& st, int effect)
		{
			if (!st.on[effect]) return;
			if (const int slot = SlotOf(effect); slot >= 0) Papyrus::ClearOverlay(a, st.female, Node(slot));
			st.on[effect] = false;
			st.until[effect] = 0.0f;
			if (effect == kBlush) st.blushAlpha = 0.0f;
		}

		void AddEmo(RE::Actor* a, State& st)
		{
			if (st.emoTears || !g_emoTears) return;
			{
				std::scoped_lock l(Settings::lock);
				if (!Settings::Skin::bEmoTears) return;
			}
			a->AddSpell(g_emoTears);
			st.emoTears = true;
		}

		void ClearEmo(RE::Actor* a, State& st)
		{
			if (!st.emoTears) return;
			if (g_emoTears) a->RemoveSpell(g_emoTears);
			st.emoTears = false;
		}

		void ClearState(RE::Actor* a, State& st)
		{
			for (int e = 0; e < kCount; ++e) Clear(a, st, e);
			ClearEmo(a, st);
		}

		// "Welling eyes" without a texture: brow in/up, a lowered gaze and a soft squint.
		void TearExpression(RE::Actor* a, State& st)
		{
			using namespace Face::Output;
			if (!st.broken) {  // a broken face doesn't knit its brows
				SetModifier(a, kBrowInL, 0.16f, 0.8f);
				SetModifier(a, kBrowInR, 0.16f, 0.8f);
				SetModifier(a, kBrowUpL, 0.10f, 0.8f);
				SetModifier(a, kBrowUpR, 0.10f, 0.8f);
			}
			SetModifier(a, kLookDown, 0.20f, 0.8f);
			SetModifier(a, kSquintL, 0.14f, 0.8f);
			SetModifier(a, kSquintR, 0.14f, 0.8f);
			st.until[kTear] = Scenes::Now() + 4.0f;
			g_status = std::format("{}: tear expression", a->GetDisplayFullName());
		}

		constexpr float kTearCooldown = 20.0f;

		void Tear(RE::Actor* a, State& st)
		{
			st.nextTear = Scenes::Now() + kTearCooldown;
			AddEmo(a, st);  // streaming tears for as long as the distress lasts
			if (!Path(kTear).empty()) {
				float alpha;
				{
					std::scoped_lock l(Settings::lock);
					alpha = std::clamp(0.55f * Settings::Skin::fStrength * StyleScalar(), 0.0f, 0.8f);
				}
				Apply(a, st, kTear, alpha, 5.0f);
			} else {
				TearExpression(a, st);
			}
		}

		float SalivaAlpha(float meter, bool peak, bool afterglow)
		{
			std::scoped_lock l(Settings::lock);
			const float style = Settings::Skin::bStyleGated ? Settings::StyleValue() : 1.0f;
			float base = style >= 1.5f ? 0.60f : (style >= 0.5f ? 0.45f : 0.30f);
			if (peak) base += 0.15f;
			else if (afterglow) base *= 0.70f;
			return std::clamp(base * Settings::Skin::fStrength * std::clamp(meter, 0.35f, 1.0f), 0.0f, 0.9f);
		}

		void TrySaliva(RE::Actor* a, State& st, float meter, bool peak, bool afterglow)
		{
			{
				std::scoped_lock l(Settings::lock);
				if (!Settings::Skin::bSaliva) return;
			}
			if (st.salivaCount >= 2) return;
			if (auto* t = Scenes::ThreadOf(a)) {
				if (auto* s = t->Find(a); s && Face::Engine::MouthYielded(*t, *s, a)) return;
			}
			if (!peak && !afterglow && meter < 0.82f) return;
			const float now = Scenes::Now();
			if (!afterglow && now < st.salivaCooldown) return;
			const float alpha = SalivaAlpha(meter, peak, afterglow);
			if (alpha <= 0.01f) return;
			Apply(a, st, kSaliva, alpha, afterglow ? 5.0f : (peak ? 4.0f : 3.0f));
			if (st.on[kSaliva]) {
				++st.salivaCount;
				st.salivaCooldown = now + 45.0f;
			}
		}

		State& Track(RE::Actor* a, bool female)
		{
			auto& st = g_states[a->GetFormID()];
			st.female = female;
			return st;
		}

		// Tears are reserved for non-consent: only an actor OnDistress marked as the victim.
		void VictimTear(RE::Actor* a, State& st)
		{
			{
				std::scoped_lock l(Settings::lock);
				if (!Settings::Skin::bTears) return;
			}
			if (!st.victim || Scenes::Now() < st.nextTear) return;
			if (!st.broken && !Face::Engine::VictimCries(a)) return;  // a defiant (dominant) victim doesn't cry until broken
			Tear(a, st);
		}
	}

	void OnDataLoaded()
	{
		CSimpleIniA skee;
		if (skee.LoadFile("Data/SKSE/Plugins/skee64.ini") >= 0) {
			// skee64.ini has inline "; Default[3]" comments, which GetLongValue rejects.
			g_faceSlots = std::atoi(skee.GetValue("Overlays/Face", "iNumOverlays", "3"));
		}
		std::error_code ec;
		g_autoBlush = std::filesystem::exists(std::string("Data/textures/") + kAutoBlush, ec);
#if !OSIS_LITE
		if (auto* dh = RE::TESDataHandler::GetSingleton()) g_emoTears = dh->LookupForm<RE::SpellItem>(0xD65, "EmoTearsSpells.esp");
		logger::info("Living Skin: Emotional Tears Effect {}", g_emoTears ? "found" : "not installed");
#endif
		logger::info("Living Skin: {} face overlay slots; Female Makeup Suite cheek blush {}", g_faceSlots, g_autoBlush ? "found" : "not found");
	}

	int FaceOverlaySlots() { return g_faceSlots; }

	std::string ResolvedPath(int effect) { return Path(effect); }

	void OnPaint(const Pulse::Beat& b)
	{
		bool female = false;
		if (!Paintable(b.actor, female)) return;
		std::scoped_lock l(g_lock);
		auto& st = Track(b.actor, female);
		if (b.broken) {  // the victim checked out: tears keep coming, nothing else
			st.broken = st.distress = st.victim = true;
			Clear(b.actor, st, kBlush);
			Clear(b.actor, st, kSaliva);
			VictimTear(b.actor, st);
			return;
		}
		if (!b.consent) {
			st.victim = b.victim;  // roles can change with the scene
			if (!st.victim) ClearEmo(b.actor, st);
			return;                // non-consensual: no blush or saliva
		}
		if (st.distress) {  // the thread moved on to a consensual scene: no more tears
			st.distress = false;
			st.victim = false;
			ClearEmo(b.actor, st);
		}
		const float enjoy = std::clamp(static_cast<float>(b.enj) / 100.0f, 0.0f, 1.0f);
		bool blush;
		float strength;
		{
			std::scoped_lock sl(Settings::lock);
			blush = Settings::Skin::bBlush;
			strength = Settings::Skin::fStrength;
		}
		TrySaliva(b.actor, st, enjoy, false, false);
		if (blush) {
			if (Face::Engine::OBlushPresent()) {
				// OBlush already blushes the face; two blush layers look like a rash.
				Clear(b.actor, st, kBlush);
				g_status = "blush yielded to OBlush";
			} else {
				// Face and body flush together: the softbody arousal level can carry the blush too.
				const float ramp = std::max(std::clamp((enjoy - 0.15f) / 0.75f, 0.0f, 1.0f), Arousal::Flush(b.actor));
				const float alpha = std::clamp(ramp * strength * StyleScalar(), 0.0f, 0.9f);
				if (alpha > 0.03f) {
					if (!st.on[kBlush] || std::abs(alpha - st.blushAlpha) > 0.02f) {
						Apply(b.actor, st, kBlush, alpha, 0.0f);
						st.blushAlpha = alpha;
					}
				} else {
					Clear(b.actor, st, kBlush);
				}
			}
		}
	}

	void OnClimaxPeak(RE::Actor* a)
	{
		bool female = false;
		if (!Paintable(a, female)) return;
		std::scoped_lock l(g_lock);
		auto& st = Track(a, female);
		if (st.distress) {
			VictimTear(a, st);  // a forced climax
			return;
		}
		TrySaliva(a, st, 1.0f, true, false);
	}

	void OnAfterglow(RE::Actor* a)
	{
		bool female = false;
		std::scoped_lock l(g_lock);
		auto it = a ? g_states.find(a->GetFormID()) : g_states.end();
		if (it == g_states.end()) return;
		auto& st = it->second;
		const bool paintable = Paintable(a, female);
		if (paintable && st.broken) return;  // a broken victim keeps crying
		if (!paintable || st.distress) {
			ClearState(a, st);
			return;
		}
		Clear(a, st, kBlush);
		TrySaliva(a, st, 0.65f, false, true);
	}

	void OnDistress(RE::Actor* a, bool victim)
	{
		if (!a) return;
		bool female = false;
		const bool paintable = Paintable(a, female);
		std::scoped_lock l(g_lock);
		auto& st = paintable ? Track(a, female) : g_states[a->GetFormID()];
		st.distress = true;
		st.victim = victim || st.broken;
		// No blush or saliva for anyone in a non-consensual scene; tears only for the victim.
		Clear(a, st, kBlush);
		Clear(a, st, kSaliva);
		if (!st.victim) {
			Clear(a, st, kTear);
			ClearEmo(a, st);
		}
		if (paintable) VictimTear(a, st);
	}

	void ClearActor(RE::Actor* a)
	{
		if (!a) return;
		std::scoped_lock l(g_lock);
		if (auto it = g_states.find(a->GetFormID()); it != g_states.end()) {
			ClearState(a, it->second);
			g_states.erase(it);
		}
	}

	void ClearAll()
	{
		std::scoped_lock l(g_lock);
		for (auto& [id, st] : g_states) {
			if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id)) ClearState(a, st);
		}
		g_states.clear();
		if (g_emoTears) {
			for (auto id : g_emoLeftovers) {
				if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id)) a->RemoveSpell(g_emoTears);
			}
		}
		g_emoLeftovers.clear();
		g_status = "cleared";
	}

	bool EmoTearsFound() { return g_emoTears != nullptr; }

	std::vector<RE::FormID> EmoTearIDs()
	{
		std::scoped_lock l(g_lock);
		std::vector<RE::FormID> out;
		for (const auto& [id, st] : g_states) {
			if (st.emoTears) out.push_back(id);
		}
		return out;
	}

	void SetEmoTearIDs(std::vector<RE::FormID> a_ids)
	{
		std::scoped_lock l(g_lock);
		g_emoLeftovers = std::move(a_ids);  // stripped by ClearAll once the game has loaded
	}

	void Tick()
	{
		bool on;
		{
			std::scoped_lock sl(Settings::lock);
			on = Settings::Skin::bEnabled && Settings::General::bEnabled && !Compat::Disabled(Compat::kSkin);
		}
		if (!on) {  // switched off: take the overlays down now, not at the end of the scene
			bool any;
			{
				std::scoped_lock l(g_lock);
				any = !g_states.empty();
			}
			if (any) ClearAll();
			return;
		}
		std::scoped_lock l(g_lock);
		const float now = Scenes::Now();
		for (auto& [id, st] : g_states) {
			auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
			if (!a) continue;
			for (int e = kBlush; e < kCount; ++e) {
				if (st.until[e] > 0.0f && now >= st.until[e]) {
					Clear(a, st, e);
					st.until[e] = 0.0f;
				}
			}
		}
	}

	void TestTear(RE::Actor* a)
	{
		bool female = false;
		if (!a) return;
		Paintable(a, female);
		std::scoped_lock l(g_lock);
		auto& st = Track(a, female);
		const bool hadEmo = st.emoTears;
		Tear(a, st);
		st.nextTear = 0.0f;  // a test must not delay a real tear
		if (st.emoTears && !hadEmo) {
			Scheduler::After(10.0f, [h = a->GetHandle()]() {
				auto actor = h.get();
				if (!actor) return;
				std::scoped_lock l(g_lock);
				if (auto it = g_states.find(actor->GetFormID()); it != g_states.end() && !it->second.distress) ClearEmo(actor.get(), it->second);
			});
		}
	}

	void TestSaliva(RE::Actor* a)
	{
		bool female = false;
		if (!a) return;
		Paintable(a, female);
		std::scoped_lock l(g_lock);
		auto& st = Track(a, female);
		Apply(a, st, kSaliva, SalivaAlpha(1.0f, true, false), 8.0f);
	}

	void TestBlush(RE::Actor* a)
	{
		bool female = false;
		if (!a) return;
		Paintable(a, female);
		std::scoped_lock l(g_lock);
		auto& st = Track(a, female);
		Apply(a, st, kBlush, 0.8f, 6.0f);
	}

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		return std::format("{} ({} actor(s) tracked)", g_status, g_states.size());
	}
}
