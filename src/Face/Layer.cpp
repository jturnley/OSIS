// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

// OSED 1.0 prototype layer (Assist / Enhanced over OStim's own face), anime accents and the
// tongue object, the 2.0 normal (pre-animation) state, the breath layer and the watcher
// trial. Ported from OSExpressionFaces.psc.

#include "Face/Internal.h"

#include "Papyrus.h"
#include "Scheduler.h"
#include "Pulse.h"

namespace Face::Engine::detail
{
	using namespace Scenes;

	namespace
	{
		RE::ActorHandle g_watcher;
		// Actors whose tongue we equipped. Keyed by form so it outlives the slot it was set from.
		std::mutex g_tongueLock;
		std::unordered_set<RE::FormID> g_ourTongues;
		float g_watcherNext = 0.0f;
		std::string g_watcherStatus = "OFF";

		std::string LayerEventName(Thread& t, int raw, bool shy, bool bold)
		{
			if (t.afterglow > 0) return "osis_afterglow";
			if (t.orgasm || raw >= 86) return "osis_climax_build";
			if (shy) return "osis_shy_avert";
			if (bold || raw >= 55) return "osis_focus";
			return "";
		}

		int TongueThreshold() { return S::bAnimeTongueFull ? 90 : 95; }
		float TongueHold() { return S::bAnimeTongueFull ? 1.15f : 0.55f; }
		float TongueCooldown() { return S::bAnimeTongueFull ? 1.6f : 2.8f; }

		bool TongueMoment(Thread& t, RE::Actor* a, int raw)
		{
			if (!a || !S::bAnimeTongue || StyleValue() < 1.5f || !t.consent || !OSEDAnimationActive(t)) return false;
			if (ActorSex(a) != 1) return false;
			if (S::bConsentGuardrails && S::bHardExclusionGate && t.toneForced) return false;
			return t.orgasm || raw >= TongueThreshold();
		}

		void ShapeAnimeTongueMouth(Slot& s, RE::Actor* a, int raw, bool tongueVisible)
		{
			const int big = tongueVisible ? ClampI(66 + raw / 6, 80, 88) : ClampI(54 + raw / 6, 68, 82);
			const int aah = ClampI(big / 7, 8, 13);
			SetPh(a, 1, big, 0.16f);
			SetPh(a, 0, aah, 0.16f);
			SetPh(a, 11, 0, 0.16f);
			SetOwners(s, "OSED Anime", tongueVisible ? "Anime tongue" : "Tongue prime", "Anime eyes", s.headOwner);
		}

		bool TongueLifeMoment(Thread& t, Slot& s, RE::Actor* a, int raw, bool yieldMouth)
		{
			if (!S::bTongueLife || !S::bAnimeTongue || !a || !t.consent || yieldMouth || t.toneForced || t.orgasm || !OSEDAnimationActive(t)) return false;
			if (StyleValue() < 1.5f || ActorSex(a) != 1) return false;
			if (raw < 45 || raw > 84) return false;
			if (s.exprOverride) return false;
			const float now = Scenes::Now();
			if (s.tongueLifeUntil > now) return true;
			if (now < s.tongueLifeNext) return false;
			s.tongueLifeNext = now + RandFloat(7.0f, 14.0f);
			if (RandInt(0, 100) > 18) return false;
			s.tongueLifeUntil = now + RandFloat(0.65f, 1.15f);
			SetPh(a, 1, ClampI(18 + raw / 8, 22, 34), 0.12f);
			SetPh(a, 11, 0, 0.12f);
			return true;
		}

		void ApplyAnimeDirectLayer(Slot& s, RE::Actor* a, int raw, bool yieldMouth, bool tongueJson)
		{
			int variant = s.animeVariant >= 0 ? s.animeVariant : 0;
			const float modeAmp = S::iMode == S::kEnhanced ? 1.12f : 0.95f;
			const int baseLook = 48 + raw / 2;
			const int baseSquint = 24 + raw / 3 + static_cast<int>(StyleValue() * 7.0f);
			int lookUp = ClampI(static_cast<int>(static_cast<float>(baseLook) * modeAmp), 50, 95);
			int squint = ClampI(static_cast<int>(static_cast<float>(baseSquint) * modeAmp), 30, 82);
			int browIn = 10;
			int browUp = 14;
			if (variant == 1) {
				lookUp = ClampI(lookUp + 8, 0, 95);
				browUp = 24;
			} else if (variant == 2) {
				squint = ClampI(squint + 14, 0, 88);
				browIn = 24;
				browUp = 6;
			} else if (variant == 3) {
				lookUp = ClampI(lookUp - 10, 0, 95);
				squint = ClampI(squint + 6, 0, 86);
			}
			SetMod(a, 27, EyeValue(lookUp), 0.22f);
			SetMod(a, 28, EyeValue(squint), 0.22f);
			SetMod(a, 29, EyeValue(squint + 4), 0.22f);
			SetMod(a, 20, BrowValue(browIn), 0.22f);
			SetMod(a, 21, BrowValue(browIn), 0.22f);
			SetMod(a, 22, BrowValue(browUp), 0.22f);
			SetMod(a, 23, BrowValue(browUp), 0.22f);
			if (!yieldMouth) {
				int open = ClampI(46 + raw / 2, 60, 92);
				if (tongueJson || s.tongueOn) open = ClampI(open + 6, 72, 94);
				SetPh(a, 1, open, 0.18f);
				SetPh(a, 0, open / 4, 0.18f);
				SetPh(a, 11, 0, 0.18f);
			}
		}

		void ApplyOSEDMicroLayer(Thread& t, Slot& s, RE::Actor* a, int idx, int raw, bool shy, bool bold, bool yieldMouth)
		{
			if (!S::bExcitementGradient && !shy && !bold) {
				SetOwners(s, "OStim assist", MouthOwnerLabel(t, s, a, yieldMouth), "OStim eyes", s.headOwner);
				return;
			}
			int squint = ClampI(6 + raw / 6, 0, 44);
			int browIn = 0;
			int browDown = 0;
			int browUp = 0;
			int lookUp = 0;
			const float style = StyleValue();
			if (style >= 0.5f) {
				squint = ClampI(squint + 5, 0, 52);
				browUp = ClampI(browUp + 3, 0, 18);
			}
			if (style >= 1.5f) {
				squint = ClampI(squint + 8, 0, 66);
				lookUp = ClampI(6 + raw / 9, 0, 30);
			}
			if (S::iMode == S::kAssist) {
				squint = (squint * 8) / 10;
				lookUp = (lookUp * 8) / 10;
			} else {
				squint = ClampI(squint + 4, 0, 70);
				lookUp = ClampI(lookUp + 4, 0, 34);
			}
			if (shy) {
				browIn = 14;
				browDown = 6;
				squint = ClampI(squint + 5, 0, 70);
			}
			if (bold) {
				browUp = ClampI(browUp + 10, 0, 32);
				squint = ClampI(squint + 3, 0, 70);
			}
			if (S::bActTypeAware && S::bRoleMetadata) {
				if (ActorHasAnyAction(t, s, T().actionOral) || ActorHasActionTagAsActor(t, s, T().tagOralAction)) {
					browDown = ClampI(browDown + 7, 0, 28);
					squint = ClampI(squint + 5, 0, 74);
				} else if (ActorHasAnyAction(t, s, T().actionKiss)) {
					browUp = ClampI(browUp + 5, 0, 30);
				} else if (SceneHasAnyAction(t, T().actionAnal)) {
					browIn = ClampI(browIn + 5, 0, 30);
				}
			}
			if (S::bExposureAware && t.consent && IsNude(a) && raw < 65) {
				browIn = ClampI(browIn + 5, 0, 32);
				squint = ClampI(squint + 3, 0, 74);
			}
			if (raw < 25 && !shy && !bold) {
				squint = ClampI(squint / 3, 0, 12);
				lookUp = 0;
			}
			if (raw >= 70 && style >= 0.5f) lookUp = ClampI(lookUp + 8 + (raw - 70) / 4, 0, 42);
			const float tr = S::fTransition;
			if (lookUp > 0) SetMod(a, 27, EyeValue(lookUp), tr);
			SetMod(a, 28, EyeValue(squint), tr);
			SetMod(a, 29, EyeValue(squint + (idx % 2)), tr);
			SetMod(a, 20, BrowValue(browIn), tr);
			SetMod(a, 21, BrowValue(browIn), tr);
			SetMod(a, 18, BrowValue(browDown), tr);
			SetMod(a, 19, BrowValue(browDown), tr);
			SetMod(a, 22, BrowValue(browUp), tr);
			SetMod(a, 23, BrowValue(browUp), tr);
			if (!yieldMouth && S::bMouthVariety && raw >= 70) {
				int open = ClampI(8 + raw / 5 + static_cast<int>(style * 5.0f), 0, 38);
				if (style >= 1.5f) {
					open = ClampI(open + 8, 0, 52);
					SetPh(a, 1, open, tr);
					SetPh(a, 11, open / 4, tr);
					SetPh(a, 0, 0, tr);
				} else {
					SetPh(a, 0, open, tr);
					SetPh(a, 11, open / 5, tr);
				}
			}
			const char* face = shy ? "OStim assist/shy" : (bold ? "OStim assist/bold" : "OStim assist");
			const char* eye = shy ? "OSED shy eyes" : (bold ? "OSED bold eyes" : "OSED micro eyes");
			SetOwners(s, face, MouthOwnerLabel(t, s, a, yieldMouth), eye, s.headOwner);
		}

		bool IsNormalStateScene(Thread& t)
		{
			if (!S::bNormalState || !t.active) return false;
			if (t.normalAnimStarted || !t.normalPreWindow) return false;
			if (t.sceneID.empty()) {
				t.normalProbe = "startup-no-scene";
				return false;
			}
			std::string id = t.sceneID;
			std::ranges::transform(id, id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			for (const char* token : { "idle", "hub", "navigation", "menu" }) {
				if (id.find(token) != std::string::npos) {
					t.normalProbe = std::string("scene-name ") + token;
					return true;
				}
			}
			if ((!t.meta || !SceneHasAnimationSignal(*t.meta)) && t.speed < 0) {
				t.normalProbe = "startup-speed";
				return true;
			}
			t.normalProbe = "animation";
			return false;
		}

		bool IsValidWatcher(RE::Actor* a)
		{
			if (!a || a->IsPlayerRef() || InAnyScene(a)) return false;
			if (!IsHuman(a) || a->IsChild() || a->IsDead() || !a->Is3DLoaded()) return false;
			return true;
		}
	}

	bool OSEDAnimationActive(const Thread& t) { return t.active && t.normalAnimStarted && !t.normalPreWindow && !t.normalActive; }

	bool OSEDNeedsFastTick(const Thread& t)
	{
		if (!t.active || (!S::bTongueLife && (!S::bAnimeTongue || StyleValue() < 1.5f))) return false;
		const float now = Scenes::Now();
		for (const auto& s : t.slots) {
			if (s.tongueOn || s.tonguePrimeUntil > now || s.tongueHoldUntil > now) return true;
			if (S::bAnimeTongue && StyleValue() >= 1.5f && s.animeActive) return true;
		}
		return false;
	}

	// ------------------------------------------------------------------ expression events
	void PlayOSEDExpressionEvent(Thread& t, Slot& s, RE::Actor* a, const std::string& ev, float minGap, bool force)
	{
		if (!a || ev.empty()) return;
		// Director mode switches OStim's face writer off, so its expression events would not show.
		if (S::iMode == S::kDirector) return;
		const float now = Scenes::Now();
		if (s.jsonEvent == ev && now < s.jsonUntil && !force) return;
		if (!s.jsonEvent.empty() && s.jsonEvent != ev) ClearOSEDExpressionEvent(s, a, true);
		s.jsonEvent = ev;
		s.jsonUntil = now + minGap;
		const int tid = t.id;
		const RE::FormID id = a->GetFormID();
		Papyrus::PlayExpression(a, ev, [tid, id, ev, now, minGap](float dur) {
			// OStim reports the event's real duration; stretch our hold to match.
			SKSE::GetTaskInterface()->AddTask([=]() {
				std::scoped_lock l(Scenes::Lock());
				auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
				auto* th = a ? Scenes::ThreadOf(a) : nullptr;
				auto* sl = th && th->id == tid ? th->Find(a) : nullptr;
				if (sl && sl->jsonEvent == ev && dur > 0.0f) sl->jsonUntil = now + ClampF(dur, minGap, 6.0f);
			});
		});
	}

	void ExpireOSEDExpressionEvent(Slot& s, RE::Actor* a)
	{
		if (!a || s.jsonEvent.empty()) return;
		if (Scenes::Now() >= s.jsonUntil) ClearOSEDExpressionEvent(s, a, true);
	}

	void ClearOSEDExpressionEvent(Slot& s, RE::Actor* a, bool force)
	{
		if (!a || s.jsonEvent.empty()) return;
		if (force || Scenes::Now() >= s.jsonUntil) {
			Papyrus::ClearExpression(a);
			s.jsonEvent.clear();
			s.jsonUntil = 0.0f;
		}
	}

	// ------------------------------------------------------------------ anime / tongue
	bool OSEDShouldAnime(Thread& t, Slot& s, RE::Actor* a, int raw, bool)
	{
		if (!a || AhegaoYield() || !t.consent || StyleValue() < 1.5f || !OSEDAnimationActive(t)) return false;
		if (S::bConsentGuardrails && S::bHardExclusionGate && t.toneForced) return false;
		const int climaxes = TimesClimaxed(a);
		const bool newClimax = climaxes > s.lastClimax;
		if (newClimax) s.lastClimax = climaxes;
		const bool locked = climaxes >= 2;
		if (s.animeActive) return locked || t.orgasm || raw >= static_cast<int>(S::fAnimeEnd);
		if (locked || t.orgasm || newClimax || raw >= static_cast<int>(S::fAnimeStart)) {
			s.animeActive = true;
			s.animeVariant = PickSeed(t, Seed(a), 4);
			return true;
		}
		return false;
	}

	void ApplyOSEDAnimeAccent(Thread& t, Slot& s, RE::Actor* a, int raw, bool yieldMouth)
	{
		if (!OSEDAnimationActive(t) && t.id != -100) {
			ClearOSEDAnimeAccent(s, a);
			return;
		}
		s.animeActive = true;
		if (s.animeVariant < 0) s.animeVariant = PickSeed(t, Seed(a), 4);
		const float now = Scenes::Now();
		const bool tongueJson = !yieldMouth && TongueMoment(t, a, raw) && now >= s.tongueCooldownUntil;
		if (yieldMouth) PlayOSEDExpressionEvent(t, s, a, "osis_anime_peak_eyes", 2.0f);
		else if (tongueJson || s.tongueOn || s.tonguePrimeUntil > now) PlayOSEDExpressionEvent(t, s, a, "osis_anime_peak_tongue", 1.6f);
		else PlayOSEDExpressionEvent(t, s, a, "osis_anime_peak", 2.0f);

		if (S::iMode != S::kDirector) {
			ApplyAnimeDirectLayer(s, a, raw, yieldMouth, tongueJson);
			SetOwners(s, "OSED Anime", yieldMouth ? MouthOwnerLabel(t, s, a, true) : (tongueJson ? "Anime JSON prime" : "Anime JSON"), "Anime JSON", s.headOwner);
			return;
		}
		int lookUp = ClampI(42 + raw / 2, 55, 92);
		int squint = ClampI(18 + raw / 4, 28, 58);
		int browIn = 8;
		int browUp = 10;
		if (s.animeVariant == 1) {
			lookUp = ClampI(lookUp + 8, 0, 95);
			browUp = 20;
		} else if (s.animeVariant == 2) {
			squint = ClampI(squint + 16, 0, 72);
			browIn = 22;
			browUp = 4;
		} else if (s.animeVariant == 3) {
			lookUp = ClampI(lookUp - 12, 0, 95);
			squint = ClampI(squint + 5, 0, 62);
		}
		SetMod(a, 27, EyeValue(lookUp), 0.25f);
		SetMod(a, 28, EyeValue(squint), 0.25f);
		SetMod(a, 29, EyeValue(squint + 4), 0.25f);
		SetMod(a, 20, BrowValue(browIn), 0.25f);
		SetMod(a, 21, BrowValue(browIn), 0.25f);
		SetMod(a, 22, BrowValue(browUp), 0.25f);
		SetMod(a, 23, BrowValue(browUp), 0.25f);
		if (!yieldMouth) {
			const int open = ClampI(42 + raw / 2, 58, 90);
			SetPh(a, 1, open, 0.22f);
			SetPh(a, 0, open / 3, 0.22f);
			SetPh(a, 11, 0, 0.22f);
			SetOwners(s, "OSED Anime", "Anime mouth", "Anime eyes", s.headOwner);
		} else {
			SetOwners(s, "OSED Anime", MouthOwnerLabel(t, s, a, true), "Anime eyes", s.headOwner);
		}
	}

	void ClearOSEDAnimeAccent(Slot& s, RE::Actor* a)
	{
		// A tongue that belongs to the override pool is that pool's to take back, not this accent's.
		if (!s.animeActive && (!s.tongueOn || s.libTongue)) return;
		SetOSEDTongue(s, a, false);
		ClearOSEDExpressionEvent(s, a, true);
		for (int i : { 27, 28, 29, 20, 21, 22, 23 }) SetMod(a, i, 0, 0.25f);
		ResetPh(a, 0.25f);
		s.animeActive = false;
		s.animeVariant = -1;
	}

	bool OurTongue(RE::Actor* a)
	{
		if (!a) return false;
		std::scoped_lock l(g_tongueLock);
		return g_ourTongues.contains(a->GetFormID());
	}

	void SetOSEDTongue(Slot& s, RE::Actor* a, bool on)
	{
		if (!a) return;
		// Both have to agree before this is a no-op. After a slot rebuild the flag reads false
		// while the tongue is still out, and that is exactly the case that has to reach the
		// unequip.
		const bool held = OurTongue(a);
		if (s.tongueOn == on && held == on) return;
		{
			std::scoped_lock l(g_tongueLock);
			if (on) g_ourTongues.insert(a->GetFormID());
			else g_ourTongues.erase(a->GetFormID());
		}
		if (on) Papyrus::EquipObject(a, "tongue");
		else Papyrus::UnequipObject(a, "tongue");
		s.tongueOn = on;
		// What we just did, without waiting for the next poll: until it catches up, a tongue that is out but no
		// longer registered as ours reads as an ahegao mod's and the whole face is stood down.
		s.tongueOut = on;
	}

	// OStim equips a tongue for a licking action's expression override, and takes it back when
	// the override ends - through applyExpression on the *underlying* expression. Director mode
	// switches that path off (SetExpressionsEnabled false flags NO_UNDERLYING_EXPRESSION), so the
	// tongue is never taken back and hangs there for the rest of the scene.
	//
	// Unequipping it ourselves is not enough: OStim's own phonemeObjects list would still hold
	// "tongue", and the next licking action skips the equip for anything already in that list, so
	// the tongue would never come out again in that scene. Instead hand the face back for a
	// moment and play an event expression that owns no phoneme objects. OStim's phoneme branch
	// then unequips what is left and resets its list, which is exactly what it does for itself on
	// every node change. Then take the face back.
	void ClearOStimTongue(Slot& s, RE::Actor* a)
	{
		if (!a) return;
		s.tongueClearUntil = Scenes::Now() + 3.0f;
		Papyrus::SetExpressionsEnabled(a, true, true);
		Papyrus::PlayExpression(a, "osis_tongue_clear");
		Scheduler::After(0.3f, [h = a->GetHandle(), allow = !s.noOverride]() {
			if (auto actor = h.get()) Papyrus::SetExpressionsEnabled(actor.get(), false, allow);
		});
		logger::info("Asked OStim to take back its own tongue on {:08X} {}", a->GetFormID(), a->GetDisplayFullName());
	}

	void ClearStrayTongues()
	{
		std::vector<RE::FormID> ids;
		{
			std::scoped_lock l(g_tongueLock);
			ids.assign(g_ourTongues.begin(), g_ourTongues.end());
			g_ourTongues.clear();
		}
		for (const auto id : ids) {
			if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id)) Papyrus::UnequipObject(a, "tongue");
		}
		if (!ids.empty()) logger::info("Took back {} stray tongue(s)", ids.size());
	}

	void UpdateOSEDTongue(Thread& t, Slot& s, RE::Actor* a, bool yieldMouth)
	{
		if (!a) return;
		if (s.libTongue) return;  // the override pool has it out; nothing here may take it back
		if (AhegaoYield()) {
			SetOwners(s, "Ahegao mod yield", "Ahegao mod yield", "Ahegao mod yield", s.headOwner);
			return;
		}
		const int raw = Raw(a);
		const float now = Scenes::Now();
		const bool director = S::iMode == S::kDirector;
		if (!OSEDAnimationActive(t)) {
			s.tonguePrimeUntil = s.tongueHoldUntil = s.tongueLifeUntil = 0.0f;
			SetOSEDTongue(s, a, false);
			return;
		}
		bool animeBlocked = !S::bAnimeTongue || yieldMouth || !t.consent || StyleValue() < 1.5f;
		if (S::bConsentGuardrails && S::bHardExclusionGate && t.toneForced) animeBlocked = true;
		bool eligible = !animeBlocked && s.animeActive && TongueMoment(t, a, raw);
		if (!animeBlocked && s.tongueHoldUntil > now && raw >= 85) eligible = true;
		if (animeBlocked || raw < static_cast<int>(S::fAnimeEnd) - 5) s.tonguePrimeUntil = s.tongueHoldUntil = 0.0f;
		if (eligible) {
			if (!s.tongueOn && now < s.tongueCooldownUntil) {
				if (director) ShapeAnimeTongueMouth(s, a, raw, false);
				return;
			}
			const float hold = now + TongueHold();
			if (s.tongueHoldUntil < hold && TongueMoment(t, a, raw)) s.tongueHoldUntil = hold;
			const bool forceReplay = s.jsonEvent != "osis_anime_peak_tongue" || now >= s.jsonUntil - 0.8f;
			PlayOSEDExpressionEvent(t, s, a, "osis_anime_peak_tongue", 1.6f, forceReplay);
			if (!s.tongueOn && s.tonguePrimeUntil <= now) s.tonguePrimeUntil = now + 0.35f;
			if (s.tonguePrimeUntil > now) {
				if (director) ShapeAnimeTongueMouth(s, a, raw, false);
				else SetOwners(s, "OSED Anime", "Anime JSON prime", "Anime JSON", s.headOwner);
				return;
			}
			// Director has no JSON event to time the tongue window, so it keeps the tongue
			// out for as long as the hold lasts.
			const bool window = director ? s.tongueHoldUntil > now : (s.jsonEvent == "osis_anime_peak_tongue" && now < s.jsonUntil - 0.6f);
			if (window) {
				if (director) ShapeAnimeTongueMouth(s, a, raw, true);
				else SetOwners(s, "OSED Anime", "Anime tongue JSON", "Anime JSON", s.headOwner);
				SetOSEDTongue(s, a, true);
			} else {
				SetOSEDTongue(s, a, false);
			}
			return;
		}
		if (TongueLifeMoment(t, s, a, raw, yieldMouth)) {
			SetOSEDTongue(s, a, true);
			return;
		}
		s.tonguePrimeUntil = s.tongueHoldUntil = 0.0f;
		const bool wasOn = s.tongueOn;
		SetOSEDTongue(s, a, false);
		if (wasOn) s.tongueCooldownUntil = now + TongueCooldown();
	}

	void ClearOSEDPrototypeActor(Slot& s, RE::Actor* a)
	{
		SetOSEDTongue(s, a, false);
		ClearOSEDExpressionEvent(s, a, true);
		s.animeActive = false;
		s.animeVariant = -1;
		s.lastClimax = 0;
		s.tongueLifeUntil = s.tongueLifeNext = s.tonguePrimeUntil = s.tongueHoldUntil = s.tongueCooldownUntil = 0.0f;
	}

	// ------------------------------------------------------------------ Assist / Enhanced
	void ApplyOSEDLayerArc(Thread& t, Slot& s, RE::Actor* a, int idx, bool yieldMouth)
	{
		const int raw = Raw(a);
		const int enjEff = EffectiveIntensity(t, a);
		std::string src;
		const int arch = Archetype(a, &src);
		s.arch = arch;
		s.archSource = src;
		const int dom = SelectDominant(t, enjEff, raw);
		const bool shy = arch == 3 || (S::bExposureAware && t.consent && IsNude(a) && raw < 72) || OBlushLikely(a, raw);
		const bool bold = arch == 2 || arch == 4;
		if (AhegaoYield()) {
			PulseActor(t, s, a, dom, PhrasePhase(t, idx, enjEff), enjEff);
			Pulse::Emit("OSED_Mode", t.id, a, static_cast<float>(S::iMode));
			SetOwners(s, "Ahegao mod yield", "Ahegao mod yield", "Ahegao mod yield", "Ahegao mod yield");
			return;
		}
		// The takeover toggle used to be honored only by the dormant replace mode, so OStim
		// kept overwriting these eye/brow writes. AllowOverride stays on for oral mouths.
		if (S::bTakeOverFace) ReleaseOStimFace(s, a);
		ExpireOSEDExpressionEvent(s, a);
		if (OSEDShouldAnime(t, s, a, raw, yieldMouth)) {
			ApplyOSEDAnimeAccent(t, s, a, raw, yieldMouth);
		} else {
			ClearOSEDAnimeAccent(s, a);
			PlayOSEDExpressionEvent(t, s, a, LayerEventName(t, raw, shy, bold), 2.0f);
			ApplyOSEDMicroLayer(t, s, a, idx, raw, shy, bold, yieldMouth);
		}
		const int phrase = PhrasePhase(t, idx, enjEff);
		PulseActor(t, s, a, dom, phrase, enjEff);
		Pulse::Emit("OSED_Phase", t.id, a, static_cast<float>(raw));
		Pulse::Emit("OSED_Mode", t.id, a, static_cast<float>(S::iMode));

		RE::Actor* partner = PrimaryPartner(t, s);
		if (yieldMouth) {
			ClearLook(a);
			s.headOwner = "Yielded with mouth";
		} else if (S::bGaze && partner && (!S::bGazeConsentOnly || t.consent)) {
			SetGaze(t, s, a, partner);
			s.headOwner = "OStim layer gaze";
		} else if (!S::bGaze) {
			s.headOwner = "OStim";
		}
		if (S::bHeadflow && S::iMode == S::kEnhanced && !yieldMouth) {
			const int role = ActRole(t, s, a);
			const int posRole = S::bPositionalDomSub ? PositionRole(t, s) : 0;
			ApplyHeadflow(t, s, a, idx, dom, phrase, partner, arch, posRole, ScenarioCode(t, dom, enjEff, role, 0, posRole), 0.0f, yieldMouth);
		}
	}

	void ApplyOSEDLayerBreath(Thread& t, Slot& s, RE::Actor* a, int idx)
	{
		if (!S::bBreathing) return;
		const int raw = Raw(a);
		const int seed = Seed(a);
		const float style = StyleValue();
		if (t.normalActive && !t.consent) {
			SetPh(a, 0, 0, 0.30f);
			SetPh(a, 2, 8, 0.30f);
			s.mouthOwner = "Normal brace";
			return;
		}
		float heat = 0.0f;
		if (t.normalActive && S::bNormalPreWarm && t.consent) {
			heat = ClampF((static_cast<float>(raw) - 30.0f) / 65.0f, 0.0f, 1.0f);
			heat = ClampF(heat + NormalHintBoost(s), 0.0f, 1.0f);
		}
		float amp = S::fGlobalStrength * ProfileScale() * (0.45f + style * 0.22f);
		if (S::iMode == S::kEnhanced) amp *= 1.15f;
		if (t.normalActive) amp *= 0.65f + heat * (0.35f + style * 0.12f);
		int cyc = raw >= 85 ? 3 : (raw >= 60 ? 4 : 5);
		if (t.normalActive && heat >= 0.65f) cyc = 3;
		const int p = (t.tick + idx * 2 + seed) % cyc;
		const int peakCap = t.normalActive ? 34 + static_cast<int>(style * 5.0f) : 30;
		int peak = ClampI(static_cast<int>((6.0f + static_cast<float>(raw) / 6.0f + style * 3.0f) * amp + heat * (8.0f + style * 7.0f)), 0, peakCap);
		if (raw < 30) peak /= 2;
		const int open = p == 1 ? peak : (p == 2 ? (peak * 2) / 3 : 0);
		const bool normalBite = t.normalActive && heat > 0.55f && ((t.tick + idx + seed) % 5 == 0);
		if (normalBite) {
			SetPh(a, 2, ClampI(8 + static_cast<int>(heat * 14.0f), 8, 22), 0.35f);
			SetPh(a, 0, ClampI(peak / 2, 0, 14), 0.35f);
			SetPh(a, 11, 0, 0.35f);
			s.mouthOwner = "Normal lip-bite";
			return;
		}
		if (open <= 0) {
			SetPh(a, 0, 0, 0.30f);
			if (t.normalActive) SetPh(a, 2, 0, 0.30f);
			if (style >= 1.5f) SetPh(a, 11, 0, 0.30f);
		} else {
			SetPh(a, 0, open, 0.35f);
			if (t.normalActive) SetPh(a, 2, 0, 0.35f);
			if (style >= 1.0f) SetPh(a, 11, open / 5, 0.35f);
		}
		s.mouthOwner = "Assist breath";
	}

	// ------------------------------------------------------------------ normal state
	void UpdateNormalStateFlag(Thread& t, bool sceneChanged)
	{
		if (!S::bNormalState || !t.active) {
			t.normalActive = false;
			return;
		}
		const bool active = IsNormalStateScene(t);
		if (t.normalActive && !active) {
			for (auto& s : t.slots) s.normalMood = "Handoff";
		}
		if (sceneChanged && active != t.normalActive) logger::debug("thread {} normal-state {} ({})", t.id, active, t.normalProbe);
		t.normalActive = active;
	}

	bool SceneHasAnimationSignal(const OStimData::Scene& scene)
	{
		if (OStimData::FindAnyAction(scene, T().actionAnySignal) >= 0) return true;
		return scene.maxSpeed > 0 || scene.defaultSpeed > 0;
	}

	float NormalHintBoost(Slot& s)
	{
		const float now = Scenes::Now();
		if (s.hintUntil <= now) {
			s.hintKind.clear();
			s.hintStrength = 0.0f;
			return 0.0f;
		}
		const auto& k = s.hintKind;
		auto has = [&](const char* x) { return k.find(x) != std::string::npos; };
		if (has("needy") || has("warm") || has("tender") || has("playful")) return ClampF(s.hintStrength * 0.35f, 0.0f, 0.35f);
		if (has("shy")) return ClampF(s.hintStrength * 0.18f, 0.0f, 0.18f);
		return ClampF(s.hintStrength * 0.12f, 0.0f, 0.12f);
	}

	void ApplyNormalState(Thread& t, Slot& s, RE::Actor* a, int idx, bool yieldMouth)
	{
		const int raw = Raw(a);
		const int seed = Seed(a);
		std::string src;
		const int arch = Archetype(a, &src);
		s.arch = arch;
		s.archSource = src;
		RE::Actor* partner = PrimaryPartner(t, s);
		if (AhegaoYield()) {
			s.normalMood = "yielded";
			PulseActor(t, s, a, kAnticipation, PhrasePhase(t, idx, raw), raw);
			Pulse::Emit("OSED_Normal", t.id, a, static_cast<float>(raw));
			SetOwners(s, "Ahegao mod yield", "Ahegao mod yield", "Ahegao mod yield", "Ahegao mod yield");
			return;
		}
		if (S::iMode == S::kDirector || S::bTakeOverFace) ReleaseOStimFace(s, a);
		const float style = StyleValue();
		const float arousal = S::bNormalPreWarm ? ClampF((static_cast<float>(raw) - 30.0f) / 65.0f, 0.0f, 1.0f) : 0.0f;
		const float hintBoost = t.consent ? NormalHintBoost(s) : 0.0f;
		const float heat = ClampF(arousal + hintBoost, 0.0f, 1.0f);
		const float amp = ClampF(S::fNormalIntensity * S::fGlobalStrength * ProfileScale() * (0.85f + style * 0.18f), 0.05f, 1.25f);
		const float lookAmp = ClampF(S::fGlobalStrength * ProfileScale() * (0.65f + style * 0.22f), 0.35f, 1.35f);
		const float heatAmp = ClampF(heat * lookAmp, 0.0f, 1.25f);
		int squint = ClampI(static_cast<int>(8.0f * amp), 0, 24);
		int browIn = 0;
		int browUp = ClampI(static_cast<int>(6.0f * amp), 0, 18);
		int browDown = 0;
		std::string mood = "anticipating";
		if (S::bNormalPreWarm && t.consent) squint = ClampI(squint + static_cast<int>(static_cast<float>(raw) * amp / 22.0f), 0, 34);
		if (t.consent && heat > 0.0f) {
			squint = ClampI(squint + static_cast<int>((10.0f + style * 6.0f) * heatAmp), 0, 38 + static_cast<int>(style * 8.0f));
			browUp = ClampI(browUp + static_cast<int>((4.0f + style * 3.0f) * heatAmp), 0, 28);
			if (heat > 0.52f) mood = "wanting";
		}
		if (!t.consent) {
			squint = ClampI(18 + static_cast<int>(style * 4.0f), 0, 35);
			browIn = 18;
			browDown = 15;
			browUp = 0;
			mood = "braced";
		} else if (arch == 3) {
			browIn = ClampI(10 + static_cast<int>(amp * 8.0f) + static_cast<int>(heatAmp * 8.0f), 0, 32);
			squint = ClampI(squint + 4 + static_cast<int>(heatAmp * 4.0f), 0, 42);
			mood = heat > 0.48f ? "flustered" : "bashful";
		} else if (arch == 4) {
			browDown = ClampI(4 + static_cast<int>(amp * 5.0f) + static_cast<int>(heatAmp * 6.0f), 0, 22);
			browUp = ClampI(static_cast<int>(heatAmp * 5.0f), 0, 12);
			mood = heat > 0.55f ? "hungry" : "composed";
		} else if (arch == 2) {
			browUp = ClampI(browUp + 6 + static_cast<int>(heatAmp * 6.0f), 0, 30);
			browDown = ClampI(static_cast<int>(heatAmp * 5.0f), 0, 12);
			mood = heat > 0.55f ? "hungry" : "eager";
		} else if (arch == 1) {
			squint = ClampI((squint / 2) + static_cast<int>(heatAmp * 5.0f), 0, 26);
			browDown = ClampI(5 + static_cast<int>(amp * 4.0f) + static_cast<int>(heatAmp * 4.0f), 0, 18);
			mood = heat > 0.65f ? "focused" : "controlled";
		}
		if (t.consent && hintBoost > 0.10f && heat > 0.35f) mood = "needy";
		if (S::bExposureAware && t.consent && IsNude(a) && raw < 50) {
			browIn = ClampI(browIn + 6, 0, 30);
			squint = ClampI(squint + 3, 0, 36);
			mood = "exposed";
		}
		const float tr = S::fTransition;
		SetMod(a, 28, EyeValue(squint), tr);
		SetMod(a, 29, EyeValue(squint + (idx % 2)), tr);
		SetMod(a, 20, BrowValue(browIn), tr);
		SetMod(a, 21, BrowValue(browIn), tr);
		SetMod(a, 18, BrowValue(browDown), tr);
		SetMod(a, 19, BrowValue(browDown), tr);
		SetMod(a, 22, BrowValue(browUp), tr);
		SetMod(a, 23, BrowValue(browUp), tr);
		if (t.consent && style >= 1.5f && heat >= 0.82f) {
			SetMod(a, 27, EyeValue(ClampI(static_cast<int>(heatAmp * 18.0f), 8, 24)), tr);
			mood = "can't wait";
		} else if (t.consent) {
			SetMod(a, 27, 0, tr);
		}
		if (!yieldMouth) {
			if (!t.consent) {
				SetPh(a, 0, 0, 0.35f);
				SetPh(a, 2, 8, 0.35f);
			} else if (S::bNormalPreWarm) {
				const int openBase = ClampI(static_cast<int>((4.0f + static_cast<float>(raw) / 24.0f) * amp), 0, 12);
				const int heatOpen = ClampI(static_cast<int>(heatAmp * (8.0f + style * 8.0f)), 0, 24);
				int openCap = 14 + static_cast<int>(style * 7.0f);
				if (style >= 1.5f) openCap += 5;
				const int open = ClampI(openBase + heatOpen, 0, openCap);
				if (heat > 0.55f && ((seed + t.tick + idx) % 5 == 0)) {
					SetPh(a, 2, ClampI(8 + static_cast<int>(heatAmp * 12.0f), 8, 22), 0.35f);
					SetPh(a, 0, ClampI(open / 2, 0, 12), 0.35f);
				} else {
					SetPh(a, 0, open, 0.45f);
					SetPh(a, 2, 0, 0.45f);
					if (style < 1.5f || heat < 0.82f) SetPh(a, 1, 0, 0.45f);
					SetPh(a, 11, style >= 1.0f && open > 8 ? ClampI(open / 6, 0, 6) : 0, 0.45f);
					if (style >= 1.5f && heat >= 0.82f) {
						SetPh(a, 1, ClampI(open + 6, 0, 34), 0.45f);
						SetPh(a, 11, 0, 0.45f);
					}
				}
			}
		}
		float gazeChance = S::fNormalGazeFrequency;
		if (t.consent) {
			gazeChance = ClampF(gazeChance + heat * 0.35f, 0.0f, 0.95f);
			if (arch == 3) gazeChance = ClampF(gazeChance - 0.15f + heat * 0.10f, 0.0f, 0.90f);
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!t.consent) {
			ClearLook(a);
			s.headOwner = "Normal brace";
		} else if (S::bGaze && RandFloat(0.0f, 1.0f) <= gazeChance) {
			if (partner) {
				SetGaze(t, s, a, partner);
				s.headOwner = "Normal gaze";
			} else if (S::bNormalGlancePlayer && !a->IsPlayerRef() && player) {
				LookAt(a, player);
				s.headOwner = "Player glance";
			}
		} else {
			ClearLook(a);
			s.headOwner = "Normal glance-away";
		}
		s.normalMood = mood;
		SetOwners(s, "Normal/" + mood, yieldMouth ? MouthOwnerLabel(t, s, a, true) : "Normal breath", "Normal eyes", s.headOwner);
		PulseActor(t, s, a, kAnticipation, PhrasePhase(t, idx, raw), raw);
		Pulse::Emit("OSED_Normal", t.id, a, static_cast<float>(raw));
	}

	// ------------------------------------------------------------------ watcher trial
	void ClearWatcherTrialActor()
	{
		if (auto w = g_watcher.get()) {
			ClearLook(w.get());
			Output::Release(w.get(), 0.4f);
		}
		g_watcher.reset();
	}

	void MaybeApplyWatcherTrial(Thread& t)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!S::bWatcher || !t.active || !player) {
			g_watcherStatus = "OFF";
			return;
		}
		const float now = Scenes::Now();
		if (now < g_watcherNext) return;
		g_watcherNext = now + 6.0f;
		// Game.FindRandomActorFromRef(player, 1200)
		std::vector<RE::Actor*> nearby;
		for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
			auto p = h.get();
			auto* a = p.get();
			if (a && IsValidWatcher(a) && a->GetPosition().GetDistance(player->GetPosition()) <= 1200.0f) nearby.push_back(a);
		}
		if (nearby.empty()) {
			g_watcherStatus = "no safe nearby witness";
			return;
		}
		RE::Actor* w = nearby[RandInt(0, static_cast<int>(nearby.size()) - 1)];
		if (auto cur = g_watcher.get(); cur && cur.get() != w) ClearWatcherTrialActor();
		g_watcher = w->GetHandle();
		int squint = 8;
		int browIn = 4;
		int browDown = 0;
		if (!t.consent || t.toneForced || t.toneRough) {
			squint = 16;
			browIn = 14;
			browDown = 9;
		}
		SetMod(w, 28, EyeValue(squint), 0.55f);
		SetMod(w, 29, EyeValue(squint + 2), 0.55f);
		SetMod(w, 20, BrowValue(browIn), 0.55f);
		SetMod(w, 21, BrowValue(browIn), 0.55f);
		SetMod(w, 18, BrowValue(browDown), 0.55f);
		SetMod(w, 19, BrowValue(browDown), 0.55f);
		if (NaturalGazeAngle(w, player)) LookAt(w, player);
		g_watcherStatus = std::string(w->GetDisplayFullName()) + " neutral";
	}
}

namespace Face::Engine
{
	std::string WatcherStatus()
	{
		std::scoped_lock l(Settings::lock);
		return Settings::Face::bWatcher ? detail::g_watcherStatus : "OFF";
	}
}
