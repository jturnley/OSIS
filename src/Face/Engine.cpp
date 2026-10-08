// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Face/Internal.h"

#include "Compat.h"
#include "Face/PPA.h"

#include "Papyrus.h"
#include "Pulse.h"
#if !OSIS_LITE
#	include "SceneLock.h"
#	include "Voice.h"
#endif

namespace Face::Engine
{
	using namespace detail;

	namespace
	{
		// How long an afterglow may last however many beats or climax events arrive.
		constexpr float kAfterglowSeconds = 18.0f;
		// An actor's own afterglow ends once their excitement is back above this: they are building again.
		constexpr int kAfterglowEndsAt = 45;

		// forms resolved at data load; all optional
		RE::TESFaction* g_excitement = nullptr;
		RE::TESFaction* g_climaxed = nullptr;
		RE::BGSKeyword* g_kHuman = nullptr;
		RE::BGSKeyword* g_kStoic = nullptr;
		RE::BGSKeyword* g_kVocal = nullptr;
		RE::BGSKeyword* g_kShy = nullptr;
		RE::BGSKeyword* g_kDominant = nullptr;
		RE::BGSKeyword* g_kGag = nullptr;
		RE::BGSKeyword* g_kGagPlate = nullptr;
		RE::BGSKeyword* g_kGagLarge = nullptr;
		RE::BGSKeyword* g_kGagRing = nullptr;
		RE::BGSKeyword* g_kBlind = nullptr;
		RE::BGSKeyword* g_kHood = nullptr;
		RE::TESGlobal* g_oblushEnable = nullptr;
		RE::TESGlobal* g_oblushMin = nullptr;
		RE::TESGlobal* g_oblushMax = nullptr;
		RE::TESGlobal* g_oblushMale = nullptr;
		RE::TESGlobal* g_oblushFemale = nullptr;
		bool g_ostim = false;
		bool g_oblush = false;
		std::string g_ahegaoFound = "none detected";

		std::mutex g_dataLock;  // personality overrides, takeover list, voice cache
		// Personality per actor, in the cosave. The low byte is the personality; kPinnedFlag marks one OSIS settled on by
		// itself the first time it saw the actor, as against one the player chose. The player's own entry is under 0x14.
		std::unordered_map<RE::FormID, int> g_npcPersonality;
		constexpr int kPinnedFlag = 0x100;
		constexpr std::size_t kMaxPins = 4096;  // the cosave holds a few bytes per actor; a stray run of generated NPCs cannot bloat it
		std::unordered_set<RE::FormID> g_takenOver;
		std::unordered_map<RE::FormID, std::string> g_voiceNames;
		std::unordered_set<RE::FormID> g_voiceRequested;

		std::mt19937 g_rng{ std::random_device{}() };

		template <class T>
		T* Lookup(RE::FormID a_local, std::string_view a_plugin)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupForm<T>(a_local, a_plugin) : nullptr;
		}

		bool HasPlugin(std::string_view a_plugin)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh && dh->LookupModByName(a_plugin) != nullptr;
		}

		bool WornHasKeyword(RE::Actor* a, RE::BGSKeyword* kw)
		{
			if (!a || !kw) return false;
			auto inv = a->GetInventory([](RE::TESBoundObject& obj) { return obj.IsArmor(); });
			for (auto& [obj, data] : inv) {
				if (!data.second || !data.second->IsWorn()) continue;
				if (auto* kwf = obj->As<RE::BGSKeywordForm>(); kwf && kwf->HasKeyword(kw)) return true;
			}
			return false;
		}

		bool HasKeywordEditorID(RE::Actor* a, std::string_view id)
		{
			bool found = false;
			auto check = [&](RE::BGSKeyword* kw) {
				if (kw && _stricmp(kw->GetFormEditorID(), std::string(id).c_str()) == 0) {
					found = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			};
			if (auto* base = a->GetActorBase()) base->ForEachKeyword(check);
			return found;
		}

		std::string Lower(std::string s)
		{
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		void RequestVoiceName(RE::Actor* a)
		{
			if (!a) return;
			const RE::FormID key = a->GetFormID();
			{
				std::scoped_lock l(g_dataLock);
				if (!g_voiceRequested.insert(key).second) return;
			}
			const RE::FormID base = a->IsPlayerRef() ? 0x7 : (a->GetActorBase() ? a->GetActorBase()->GetFormID() : 0);
			if (!base) return;
			Papyrus::GetVoiceSetName(base, [key](std::string name) {
				std::scoped_lock l(g_dataLock);
				g_voiceNames[key] = std::move(name);
			});
		}

		// ---- consent: OStim excitement rate
		// OStim's own rate per actor, read before we first change it. Filled from the VM thread.
		std::mutex g_rateLock;
		std::unordered_map<RE::FormID, float> g_rateBase;
		std::unordered_set<RE::FormID> g_rateRequested;

		// OActor.SetExcitementMultiplier lives on OStim's per-scene actor record (set from its MCM
		// when the actor joins, discarded when the scene ends), so nothing here outlives a scene.
		// In a non-consensual scene with an identified victim, the victim builds at
		// fVictimExcitementMult and everyone else at fAggressorExcitementMult, relative to OStim's
		// own rate. The original rate comes back if the thread turns consensual.
		void UpdateExcitementRates(Thread& t)
		{
			const bool anyVictim = t.victimKnown;
			for (auto& s : t.slots) {
				auto* a = s.Get();
				if (!a) continue;
				float want = 1.0f;
				if (S::bConsentExcitement && !t.consent && anyVictim) want = IsSubmissive(t, s) ? S::fVictimExcitementMult : S::fAggressorExcitementMult;
				// Wild people really enjoy it: they build faster; a stoic is not into it and builds slowly. A submissive builds faster in a rough scene, or in one they have accepted.
				if (S::bPersonalityExcitement && t.consent) {
					const int pers = Archetype(a);
					if (pers == Pers::kWild) want *= S::fWildExcitementMult;
					else if (pers == Pers::kStoic) want *= S::fStoicExcitementMult;  // not very into it: builds slowly
#if !OSIS_LITE
					else if (pers == Pers::kSubmissive && (t.toneRough || t.toneForced)) want *= S::fSubmissiveRoughMult;
#endif
				}
				// Control: a dominant's own pleasure comes first, so they build faster; a crazed one drives everyone else up (not a dominant,
				// who is not driven) to make them climax as fast as it can.
				if (S::bPersonalityControl && t.consent) {
					const int pers = Archetype(a);
					bool others = false, crazedOther = false;
					for (auto& o : t.slots) {
						if (&o == &s || !o.Get()) continue;
						others = true;
						crazedOther = crazedOther || Archetype(o.Get()) == Pers::kCrazed;
					}
					if (pers == Pers::kDominant && others) want *= S::fDominantExcitementMult;
					else if (crazedOther && pers != Pers::kCrazed && pers != Pers::kDominant) want *= S::fCrazedDriveMult;
				}
				want = ClampF(want, 0.05f, 4.0f);  // OStim divides by the rate to time the climax: never zero
				if (std::abs(want - s.excitementFactor) < 0.001f) continue;
				const RE::FormID id = a->GetFormID();
				std::optional<float> base;
				{
					std::scoped_lock l(g_rateLock);
					if (auto it = g_rateBase.find(id); it != g_rateBase.end()) {
						base = it->second;
					} else if (g_rateRequested.insert(id).second) {
						Papyrus::CallFloat("OActor", "GetExcitementMultiplier", a, [id](float v) {
							std::scoped_lock l(g_rateLock);
							g_rateBase[id] = v;
						});
					}
				}
				if (!base) continue;  // applied on a later tick, once OStim's own rate is known
				Papyrus::SetExcitementMultiplier(a, *base * want);
				logger::info("Consent: {:08X} {} excitement rate x{:.2f} of OStim's {:.2f}", id, a->GetDisplayFullName(), want, *base);
				s.excitementFactor = want;
			}
		}

		// ---- personality control of climaxes
		// A dominant's partner is a toy: their climax is stalled (OActor.StallClimax: they wait at the edge, excitement full) until the dominant
		// lets them, which is the dominant's own climax - then they climax together - or, failing that, fControlMaxHold seconds at the edge.
		// A crazed one is the other way round: their own climax waits for their partner's, and comes with it, every time (OnOrgasm). Consensual
		// scenes only. OStim ignores a stall for an actor it has not put in a thread yet, so it is said again every few seconds.
		void UpdateClimaxControl(Thread& t)
		{
			const float now = Scenes::Now();
			const bool on = S::bPersonalityControl && t.consent && t.slots.size() >= 2;
			std::vector<int> pers(t.slots.size(), -1);
			for (std::size_t i = 0; i < t.slots.size(); ++i) {
				if (auto* a = t.slots[i].Get()) pers[i] = Archetype(a);
			}
			for (std::size_t i = 0; i < t.slots.size(); ++i) {
				auto& s = t.slots[i];
				auto* a = s.Get();
				if (!a) continue;
				bool want = false;
				if (on) {
					bool dominantOther = false, dominantClimaxing = false, partnerOther = false, partnerClimaxing = false;
					for (std::size_t j = 0; j < t.slots.size(); ++j) {
						if (j == i || !t.slots[j].Get()) continue;
						if (pers[j] == Pers::kDominant) {
							dominantOther = true;
							dominantClimaxing = dominantClimaxing || t.slots[j].climaxing;
						}
						if (pers[j] != Pers::kCrazed) {
							partnerOther = true;
							partnerClimaxing = partnerClimaxing || t.slots[j].climaxing;
						}
					}
					if (pers[i] != Pers::kDominant && dominantOther) {
						want = !dominantClimaxing && !s.climaxing;
						// Held at the edge: the clock for how long they are made to wait.
						if (want && s.holdSince <= 0.0f && Raw(a) >= 97) s.holdSince = now;
						if (want && s.holdSince > 0.0f && now - s.holdSince > S::fControlMaxHold) {
							want = false;  // the dominant lets them, in the end
							s.holdSince = 0.0f;
							logger::info("Control: {:08X} {} held {:.0f}s at the edge: the dominant lets them go", a->GetFormID(), a->GetDisplayFullName(), S::fControlMaxHold);
						}
					} else if (pers[i] == Pers::kCrazed && partnerOther) {
						want = !partnerClimaxing && !s.climaxing;
					}
				}
				if (!want) s.holdSince = 0.0f;
				if (want) {
					if (!s.stallActive) logger::info("Control: {:08X} {} ({}) climax held", a->GetFormID(), a->GetDisplayFullName(), PersonalityName(pers[i]));
					s.stallActive = true;
					if (now - s.stallIssuedAt >= 4.0f) {
						Papyrus::StallClimax(a);
						s.stallIssuedAt = now;
					}
				} else if (s.stallActive) {
					s.stallActive = false;
					Papyrus::PermitClimax(a);
					s.permitRepeatAt = now + 2.5f;  // said once more, in case OStim had not yet taken the first
					logger::info("Control: {:08X} {} climax permitted", a->GetFormID(), a->GetDisplayFullName());
				} else if (s.permitRepeatAt > 0.0f && now >= s.permitRepeatAt) {
					Papyrus::PermitClimax(a);
					s.permitRepeatAt = 0.0f;
				}
			}
		}

		std::string VoiceName(RE::Actor* a)
		{
			std::scoped_lock l(g_dataLock);
			auto it = g_voiceNames.find(a->GetFormID());
			return it != g_voiceNames.end() ? it->second : std::string{};
		}

		int VoiceArchetype(RE::Actor* a)
		{
			const std::string v = Lower(VoiceName(a));
			if (v.empty()) return -1;
			auto any = [&](std::initializer_list<const char*> toks) {
				return std::ranges::any_of(toks, [&](const char* t) { return v.find(t) != std::string::npos; });
			};
			if (any({ "timid", "meek" })) return Pers::kTimid;
			if (any({ "crazed", "yandere", "insane" })) return Pers::kCrazed;
			if (any({ "wild", "feral" })) return Pers::kWild;
			if (any({ "submissive" })) return EditionPersonality(Pers::kSubmissive);
			if (any({ "shy", "bashful" })) return 3;
			if (any({ "soft", "gentle", "sweet" })) return 3;
			if (any({ "excited", "vocal", "sensitive" })) return 2;
			if (any({ "needy", "passion", "high" })) return 2;
			if (any({ "dominant", "rough", "aggressive" })) return 4;
			if (any({ "orc", "deep", "command" })) return 4;
			if (any({ "calm", "quiet", "reserved" })) return 1;
			return -1;
		}

		int SPIDArchetype(RE::Actor* a)
		{
			if (!S::bSPIDPersonality || !a) return -1;
			// OSED_* are the pre-rename keywords, still read so an old DISTR file keeps working.
			if (HasKeywordEditorID(a, "OSIS_Personality_Bashful") || HasKeywordEditorID(a, "OSIS_Personality_Soft")) return 3;
			if (HasKeywordEditorID(a, "OSIS_Personality_Bold")) return 2;
			if (HasKeywordEditorID(a, "OSIS_Personality_Fierce")) return 4;
			if (HasKeywordEditorID(a, "OSED_Personality_Bashful") || HasKeywordEditorID(a, "OSED_Personality_Soft")) return 3;
			if (HasKeywordEditorID(a, "OSED_Personality_Bold")) return 2;
			if (HasKeywordEditorID(a, "OSED_Personality_Fierce")) return 4;
			if (HasKeywordEditorID(a, "OSIS_Personality_Timid")) return Pers::kTimid;
			if (HasKeywordEditorID(a, "OSIS_Personality_Wild")) return Pers::kWild;
			if (HasKeywordEditorID(a, "OSIS_Personality_Crazed") || HasKeywordEditorID(a, "OSIS_Personality_Yandere")) return Pers::kCrazed;
			if (HasKeywordEditorID(a, "OSIS_Personality_Submissive")) return EditionPersonality(Pers::kSubmissive);
			return -1;
		}

		int VanillaAIPersonality(RE::Actor* a)
		{
			auto* avo = a->AsActorValueOwner();
			const float aggression = avo->GetActorValue(RE::ActorValue::kAggression);
			const float confidence = avo->GetActorValue(RE::ActorValue::kConfidence);
			const float morality = avo->GetActorValue(RE::ActorValue::kMorality);
			if (aggression >= 3.0f) return Pers::kCrazed;  // frenzied
			if (aggression >= 2.0f && confidence >= 2.0f) return 4;
			if (confidence <= 0.0f) return Pers::kTimid;   // cowardly
			if (confidence <= 1.0f) return 3;
			if (aggression <= 0.0f && confidence >= 2.0f) return 1;
			if (morality <= 1.0f && aggression >= 1.0f) return 4;
			return -1;
		}

		void UpdatePlateau(Thread& t)
		{
			if (t.orgasm || t.afterglow > 0) {
				t.plateau = 0;
				return;
			}
			// The original measured the player; NPC-only threads use their most excited actor.
			int raw = 0;
			for (auto& s : t.slots) {
				auto* a = s.Get();
				if (!a) continue;
				if (s.player) {
					raw = Raw(a);
					break;
				}
				raw = std::max(raw, Raw(a));
			}
			t.plateau = raw >= 85 ? t.plateau + 1 : 0;
		}

		int ArcEvery(const Thread& t)
		{
			if (t.orgasm && S::bClimaxPool) return 1;
			if (!S::bBreathing && !S::bTongueLife && !OSEDNeedsFastTick(t)) return 1;
			int n = static_cast<int>(S::fBaseInterval / 0.8f + 0.5f);
			if (t.sceneOral && S::bYieldOralMouth && !OverridesAreOurs()) n *= 2;
			return std::max(1, n);
		}

		// Broken victims restarted into a new thread (a follower joining) stay broken.
		// Guarded by Settings::lock, like the engine entry points that use it.
		std::unordered_map<RE::FormID, float> g_brokenCarry;
		constexpr float kBrokenCarry = 10.0f;

#if !OSIS_LITE
		void Break(Thread& t, Slot& s, RE::Actor* a, std::string_view why)
		{
			s.broken = true;
			ClearOSEDPrototypeActor(s, a);
			ClearLook(a);
			logger::info("thread {}: {:08X} {} {}: broken for the rest of the scene", t.id, a->GetFormID(), a->GetDisplayFullName(), why);
		}
#endif

		// The victim has checked out: a slack, vacant face that no longer reacts. Tears (Skin) and
		// the body (arousal, climaxes, toe curl) keep going through the pulse.
		void ApplyBroken(Thread& t, Slot& s, RE::Actor* a, int idx, bool ym, bool arc)
		{
			if (AhegaoYield()) {
				if (arc) {
					const int enjEff = EffectiveIntensity(t, a);
					PulseActor(t, s, a, SelectDominant(t, s, enjEff, Raw(a)), PhrasePhase(t, idx, enjEff), enjEff);
					SetOwners(s, "Ahegao mod yield", "Ahegao mod yield", "Ahegao mod yield", "Ahegao mod yield");
				}
				return;
			}
			ReleaseOStimFace(s, a);
			SetOSEDTongue(s, a, false);
			if (!arc) return;
			std::array<float, 32> e{};
			e[0] = 0.08f;           // Aah: the jaw hangs a little
			e[24] = 0.14f;          // LookDown
			e[28] = e[29] = 0.22f;  // Squint: heavy lids
			e[30] = 7.0f;           // neutral mood
			e[31] = 0.0f;
			Output::ApplyPreset(a, e, ym, S::fGlobalStrength, 1.0f, 1.0f, std::max(S::fTransition, 1.5f));
			SetOwners(s, "Broken", ym ? MouthOwnerLabel(t, s, a, true) : "Broken (slack)", "Broken (vacant)", "Broken (unfocused)");
			const int enjEff = EffectiveIntensity(t, a);
			PulseActor(t, s, a, SelectDominant(t, s, enjEff, Raw(a)), PhrasePhase(t, idx, enjEff), enjEff);
		}

		// The climax that breaks the victim: eyes wide, brows up, jaw dropped, for a few seconds
		// before the vacant face. The scream itself comes from Voice.
		void ApplyShock(Thread& t, Slot& s, RE::Actor* a, int idx, bool ym, bool arc)
		{
			ReleaseOStimFace(s, a);
			SetOSEDTongue(s, a, false);
			std::array<float, 32> e{};
			if (!ym) e[0] = 0.55f;  // Aah: the jaw drops
			e[11] = 0.25f;          // Oh
			e[20] = e[21] = 0.35f;  // BrowIn
			e[22] = e[23] = 0.9f;   // BrowUp
			e[30] = 12.0f;          // surprise
			e[31] = 1.0f;
			Output::ApplyPreset(a, e, ym, S::fGlobalStrength, 1.0f, 1.0f, 0.25f);
			SetOwners(s, "Shock", ym ? MouthOwnerLabel(t, s, a, true) : "Shock", "Shock", "Shock");
			if (arc) {
				const int enjEff = EffectiveIntensity(t, a);
				PulseActor(t, s, a, SelectDominant(t, s, enjEff, Raw(a)), PhrasePhase(t, idx, enjEff), enjEff);
			}
		}

		// What the probe's beat line says about this actor's orgasms and personality.
		std::string OrgasmNote(const Slot& s)
		{
			const float now = Scenes::Now();
			std::string out = std::format(", personality {} ({})", PersonalityName(s.arch), s.archSource);
			if (s.orgasms > 0) {
				out += std::format(", orgasm {} ({:.0f} s ago{}{})", s.orgasms, now - s.lastOrgasmAt,
					s.orgasms > 1 ? std::format(", {:.0f} s after the one before", s.orgasmGap) : std::string(),
					s.climaxing && now < s.climaxUntil ? ", climax face" : "");
			}
			return out;
		}

		void ApplyAll(Thread& t, bool arc)
		{
			if (!t.active) return;
			t.dialogueMenuOpen = S::bDialogueMouthYield && RE::UI::GetSingleton()->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
			// Faces switched off, or the old OSED core owns them: keep tracking and pulsing so
			// Body, Living Skin, Lip-Sync and the arousal scene factors still work, but paint nothing.
			const bool faceOff = Compat::Disabled(Compat::kFace) || !S::bEnabled;
			if (arc) {
				UpdatePlateau(t);
				if (t.afterglow > 0) --t.afterglow;
				if (t.hasPlayer && !faceOff) MaybeApplyWatcherTrial(t);
			}
			const bool director = S::iMode == S::kDirector;
			for (int idx = 0; idx < static_cast<int>(t.slots.size()); ++idx) {
				auto& s = t.slots[idx];
				auto* a = s.Get();
				if (!a || !s.painted || !a->Is3DLoaded()) continue;
				// The mode was changed to Assist or Enhanced part way through a scene: give the
				// face back now. It used to wait for the scene to end, which left an actor taken
				// over - and any tongue OStim had out stranded with it - until then.
				if (s.takenOver && !director && !S::bTakeOverFace) RestoreOStimFace(s, a);
				// Our own tongue, left out by a slot rebuild: take it back before anything else
				// looks at it. Tongue mods (Halo's HDT Tongues and the like) make this visible,
				// because the equipped object is theirs and stays put until someone unequips it.
				if (!s.tongueOn && s.tongueOut && OurTongue(a)) {
					SetOSEDTongue(s, a, false);
					logger::debug("thread {}: took back a stray tongue on {:08X}", t.id, a->GetFormID());
				} else if (s.tongueOut && !s.tongueOn && s.takenOver && !s.exprOverride &&
						   !AhegaoModInstalled() && Scenes::Now() > s.tongueClearUntil) {
					// Not ours, no override driving it any more, and no ahegao mod installed to
					// own it: this is OStim's own tongue, stranded by our takeover.
					ClearOStimTongue(s, a);
				}
				// An ahegao mod has this actor: hand the whole face over until it is done. It sets
				// its own mouth, so our jaw floor would only fight it.
				if (ExternalAhegao(s, a)) {
					if (!Output::IsSuspended(a)) {
						// Drop our own prototype claim without calling OStim's clear, which would
						// take their expression override down with ours.
						s.jsonEvent.clear();
						s.jsonUntil = 0.0f;
						s.animeActive = false;
						s.animeVariant = -1;
						s.tongueLifeUntil = s.tongueLifeNext = s.tonguePrimeUntil = s.tongueHoldUntil = s.tongueCooldownUntil = 0.0f;
						Output::SetSuspended(a, true);
						logger::debug("thread {}: {:08X} {} handed to an ahegao mod", t.id, a->GetFormID(), a->GetDisplayFullName());
					}
					// Their tongue, their mouth. We used to hold the jaw clear of it, but an ahegao
					// mod puts the tongue out on its own schedule and opens the mouth in its own
					// time, so the clearance only ever arrived at the wrong moment.
					Output::SetMouthFloor(a, 0.0f);
					// Only call it an ahegao mod when one is actually installed. Saying "Ahegao mod"
					// to someone who has none sent at least one person hunting for a mod conflict
					// that did not exist.
					const char* owner = AhegaoModInstalled() ? "Ahegao mod" : "External tongue";
					SetOwners(s, owner, owner, owner, owner);
					if (arc) {
						const int enjEff = EffectiveIntensity(t, a);
						PulseActor(t, s, a, SelectDominant(t, s, enjEff, Raw(a)), PhrasePhase(t, idx, enjEff), enjEff);
					}
					continue;
				}
				Output::SetSuspended(a, false);
				// Our own tongue out: hold the jaw clear of it for as long as it is out, whether or
				// not lip-sync or the grammar is writing the mouth this frame.
				{
					const bool want = s.tongueOn && Settings::LipSync::iTongueMode != Settings::LipSync::kTongueIgnore;
					Output::SetMouthFloor(a, want ? Settings::LipSync::fTongueMinOpen : 0.0f);
				}
				if (faceOff) {
					if (s.faced) {  // switched off mid-scene: hand the face back
						ClearOSEDPrototypeActor(s, a);
						ClearLook(a);
						Output::Release(a, 0.6f);
						RestoreOStimFace(s, a);
						s.faced = false;
					}
					if (arc) {
						const int enjEff = EffectiveIntensity(t, a);
						PulseActor(t, s, a, SelectDominant(t, s, enjEff, Raw(a)), PhrasePhase(t, idx, enjEff), enjEff);
						const char* owner = S::bEnabled ? "Old OSED core" : "Faces off";
						SetOwners(s, owner, owner, owner, owner);
					}
					continue;
				}
				// Whether OStim's writer is also on for this actor. If so the face is shared and OSIS
				// adds to it; if the takeover holds, OSIS has it to itself.
				Output::SetLayered(a, !s.takenOver);
				s.faced = true;
				RequestVoiceName(a);
				UpdatePPAMouth(t, s, a);
				const bool ym = MouthYielded(t, s, a);
				if (Scenes::Now() < s.shockUntil) {
					Output::SetMouthOwned(a, !ym);
					ApplyShock(t, s, a, idx, ym, arc);
					continue;
				}
				if (s.broken) {
					Output::SetMouthOwned(a, !ym);
					ApplyBroken(t, s, a, idx, ym, arc);
					continue;
				}
				if (director) Output::SetMouthOwned(a, !ym);
				// Outside Director mode nothing here owns the mood. One left behind by the Director
				// (a mid-scene mode change) or by a shock would otherwise be rewritten every frame
				// over OStim's own expression.
				else Output::ReleaseMood(a, 0.6f);
				if (arc) {
					if (t.normalActive) ApplyNormalState(t, s, a, idx, ym);
					else if (director) ApplyArc(t, s, a, idx, ym);
					else ApplyOSEDLayerArc(t, s, a, idx, ym);
				}
				if (S::bBreathing) {
					if (ym) s.mouthOwner = MouthOwnerLabel(t, s, a, true);
					else if (director) Breathe(t, s, a, idx);
					else ApplyOSEDLayerBreath(t, s, a, idx);
				}
				// A moan riding on the pose rather than owning the mouth. Only overwrite the
				// label when one is actually playing, or this would clobber "OSED arc" with
				// "OStim mouth" on every actor the grammar is in fact driving.
				if (!ym && LipSyncMouthActive(s, a)) s.mouthOwner = "Face + Lip-Sync";
				UpdateOSEDTongue(t, s, a, ym);
			}
			if (arc && Output::Probing()) {
				for (auto& s : t.slots) {
					auto* a = s.Get();
					if (!a) continue;
					logger::info("Probe {:08X} {}: beat {} - mode {}, face '{}', mouth '{}', eyes '{}', head '{}', excitement {}{}{}{}{}{}; OStim library: {}", a->GetFormID(),
						a->GetDisplayFullName(), t.tick, S::iMode == S::kDirector ? "Director" : (S::iMode == S::kEnhanced ? "Enhanced" : "Assist"),
						s.faceOwner, s.mouthOwner, s.eyeOwner, s.headOwner, Raw(a), t.normalActive ? ", normal state" : "",
						Scenes::Now() < s.afterglowUntil ? ", afterglow" : "", s.takenOver ? ", OStim face off" : ", OStim face ON", s.painted ? "" : ", not painted", OrgasmNote(s),
						t.meta && s.pos >= 0 ? Library::Describe(*t.meta, s.pos) : std::string("no scene data"));
				}
			}
		}
	}

	// ------------------------------------------------------------------ detail helpers
	namespace detail
	{
		int RandInt(int lo, int hi)
		{
			if (hi < lo) std::swap(lo, hi);
			return std::uniform_int_distribution<int>(lo, hi)(g_rng);
		}

		float RandFloat(float lo, float hi)
		{
			if (hi < lo) std::swap(lo, hi);
			return std::uniform_real_distribution<float>(lo, hi)(g_rng);
		}

		const Tags& T()
		{
			using OStimData::SplitCSV;
			static const Tags tags = [] {
				Tags x;
				x.actionOral = SplitCSV("blowjob,deepthroat,cunnilingus,anilingus,rimjob,oralfingering,lickingpenis,lickingvagina,lickingtesticles,lickingnipple,suckingnipple,rubbingpenisagainstface,3pp_cunnilingus,3pp_kissfellatio1,3pp_kissfellatio2,3pp_lickingpenis");
				x.actionKiss = SplitCSV("kissing,frenchkissing,kissingcheek,kissinghand,kissingfoot,kissingneck,3pp_kissing");
				x.actionVaginal = SplitCSV("vaginalsex,vaginalfingering,vaginalfisting,vaginaltoying,tribbing,rubbingclitoris,grindingpenis,grindingthigh,3pp_vaginalfingering");
				x.actionAnal = SplitCSV("analsex,analfingering,analfisting,analtoying,buttjob");
				x.actionPenetration = x.actionVaginal;
				x.actionPenetration.insert(x.actionPenetration.end(), x.actionAnal.begin(), x.actionAnal.end());
				x.actionAnySignal = x.actionOral;
				for (auto* l : { &x.actionKiss, &x.actionVaginal, &x.actionAnal }) x.actionAnySignal.insert(x.actionAnySignal.end(), l->begin(), l->end());
				// OStim puts the feet on different roles: footjob's actor "has the feet", while
				// grinding/holding/kissing/tickling put them on the target.
				x.actionMouthPenetrated = SplitCSV("blowjob,deepthroat");
				x.actionFootActor = SplitCSV("footjob");
				x.actionFootTarget = SplitCSV("grindingfoot,holdingfoot,kissingfoot,ticklingfoot");
				x.tagOralAction = SplitCSV("oral,blowjob,deepthroat,cunnilingus,anilingus,rimjob,facefuck,fellatio,mouth");
#if !OSIS_LITE
				// Non-consent. "aggressive"/"aggressivedefault" are OStim's own marker for aggressive
				// (non-consensual) threads: 1158 of the 1253 installed aggressive scenes also say
				// forced/rape. A forced tag always wins over the rough list below.
				x.tagForced = SplitCSV("forced,forceful,rape,fbrape,nonconsensual,noncon,non-consensual,aggressive,aggressivedefault,aggressor");
#endif
				// Consensual rough play / BDSM: intense, but still consensual. (Installed data: "dom"
				// and "spank" never carry a forced tag; "femdom" does on 59 of 182 scenes.)
				x.tagRough = SplitCSV("rough,dom,femdom,maledom,domination,dominant,bdsm,bondage,spank,spanking,choking,slave");
				x.tagLoving = SplitCSV("loving,romance,romantic,tender,passionate");
				// OStimData names a scene's actions the way OStim resolves them: "cunnilingus" is "vulvaleating",
				// "lickingvagina" "vulvallicking", "deepthroat" "deepthroating", "anilingus" "rimjob". These lists were
				// written with the older names, so for those acts no lookup ever matched, and the oral fallbacks
				// below were quietly covering for it. Each list now carries both names.
				for (auto* list : { &x.actionOral, &x.actionKiss, &x.actionVaginal, &x.actionAnal, &x.actionPenetration, &x.actionAnySignal,
						&x.actionFootActor, &x.actionFootTarget, &x.actionMouthPenetrated }) {
					const auto original = *list;
					for (const auto& name : original) {
						const auto canonical = OStimData::CanonicalAction(name);
						if (canonical != name && std::ranges::find(*list, canonical) == list->end()) list->push_back(canonical);
					}
				}
#if OSIS_LITE
				x.tagSub = SplitCSV("submissive,sub,bottom,receiving,passive");
#else
				x.tagSub = SplitCSV("victim,submissive,sub,bottom,receiving,passive");
#endif
				x.tagDom = SplitCSV("aggressor,dominant,dom,top,giving,active");
				x.deepthroat = SplitCSV("deepthroat");
				return x;
			}();
			return tags;
		}

		// PPA plays its own facial preset on whoever has a penis in their mouth: by default the phonemes that open it, and
		// every other phoneme zeroed. That is the mouth the Director would be writing for a blowjob, and the head it would
		// be aiming at the partner, so while PPA has the mouth the Director stops writing the phonemes and stops turning the
		// head - nothing of ours pulls the mouth off the penis PPA is aligning to it.
		//
		// Whether PPA is actually playing a preset on this actor is not told to us, so it is seen: when the mouth is handed
		// over the phonemes stop moving, and a change in them from what they were is PPA at work. If nothing changes in
		// kProbe seconds PPA is not driving this mouth (the scene is not one it recognises, or the penis is not in range),
		// and the mouth is taken back, to be offered again after kRetry seconds.
		void UpdatePPAMouth(Thread& t, Slot& s, RE::Actor* a)
		{
			constexpr float kProbe = 6.0f;
			constexpr float kRetry = 15.0f;
			constexpr float kChange = 0.6f;  // summed change in the 16 phonemes that counts as someone else moving the mouth
			const bool candidate = a && S::bYieldMouthToPPA && OverridesAreOurs() && PPA::DrivesMouth() && t.meta && s.pos >= 0 &&
				OStimData::FindAnyActionForActor(*t.meta, s.pos, T().actionMouthPenetrated) >= 0;
			if (!candidate) {
				if (s.ppaYield && a) {
					logger::info("PPA: {} is no longer giving a blowjob in this scene node ({}); the Director has the mouth back", a->GetDisplayFullName(), t.sceneID);
				}
				s.ppaYield = false;
				s.ppaGaveUp = false;
				s.ppaSeen = false;
				return;
			}
			const float now = Scenes::Now();
			if (s.ppaGaveUp) {
				if (now < s.ppaRetryAt) return;
				s.ppaGaveUp = false;
			}
			if (!s.ppaYield) {
				s.ppaYield = true;
				s.ppaSeen = false;
				s.ppaSince = now;
				s.ppaBaseSet = Output::ReadPhonemes(a, s.ppaBase);
				logger::info("PPA: {} gives a blowjob; the mouth is PPA's while it plays its preset", a->GetDisplayFullName());
				return;
			}
			if (s.ppaSeen) return;
			// One of our own moan clips finishing moves the phonemes too: not PPA, so measure from after it.
			if (Output::HasMouthOverride(a)) {
				s.ppaBaseSet = Output::ReadPhonemes(a, s.ppaBase);
				return;
			}
			std::array<float, Output::kPhonemes> cur{};
			if (s.ppaBaseSet && Output::ReadPhonemes(a, cur)) {
				float change = 0.0f;
				for (std::size_t i = 0; i < cur.size(); ++i) change += std::abs(cur[i] - s.ppaBase[i]);
				if (change >= kChange) {
					s.ppaSeen = true;
					logger::info("PPA: is driving {}'s mouth ({:.2f} phoneme change, {:.1f} s after the hand-over)", a->GetDisplayFullName(), change, now - s.ppaSince);
					return;
				}
			}
			if (now - s.ppaSince >= kProbe) {
				s.ppaYield = false;
				s.ppaGaveUp = true;
				s.ppaRetryAt = now + kRetry;
				logger::info("PPA: nothing moved {}'s mouth in {:.0f} s; it is not driving it here, so the Director has it back", a->GetDisplayFullName(), kProbe);
			}
		}

		float StyleValue() { return ClampF(S::fStyle, 0.0f, 2.0f); }
		float StyleAmp() { return 1.0f + StyleValue() * 0.10f; }
		float EyeScale() { return ClampF(S::fEyeStrength * (1.0f + StyleValue() * 0.25f), 0.10f, 1.25f); }
		float BrowScale() { return ClampF(0.70f + EyeScale() * 0.30f, 0.55f, 1.10f); }
		int EyeValue(int v) { return ClampI(static_cast<int>(static_cast<float>(v) * EyeScale()), 0, 95); }
		int BrowValue(int v) { return ClampI(static_cast<int>(static_cast<float>(v) * BrowScale()), 0, 95); }

		float ProfileScale()
		{
			if (S::iProfile == 0) return 0.7f;
			if (S::iProfile == 2) return 1.3f;
			return 1.0f;
		}

		float MouthGate() { return S::bBreathing ? 0.0f : 1.0f; }

		void SetMod(RE::Actor* a, int presetIndex, int value, float speed)
		{
			// OSED 2.0 passed Mfg *preset* indices (16-29) here, which SetModifier ignores.
			Output::SetModifier(a, presetIndex - 16, static_cast<float>(value) / 100.0f, speed);
		}

		void SetPh(RE::Actor* a, int id, int value, float speed)
		{
			Output::SetPhoneme(a, id, static_cast<float>(value) / 100.0f, speed);
		}

		void ResetPh(RE::Actor* a, float speed) { Output::ResetPhonemes(a, speed); }

		int Raw(RE::Actor* a) { return ClampI(Excitement(a), 0, 130); }

		int Seed(RE::Actor* a)
		{
			auto* b = a ? a->GetActorBase() : nullptr;
			return b ? static_cast<int>(b->GetFormID() % 100) : 0;
		}

		float PersonalityMod(int seed) { return 0.8f + static_cast<float>(seed % 5) * 0.1f; }

		int ActorSex(RE::Actor* a)
		{
			auto* b = a ? a->GetActorBase() : nullptr;
			return b && b->GetSex() == RE::SEX::kFemale ? 1 : 0;
		}

		bool IsNude(RE::Actor* a)
		{
			using Slot_ = RE::BGSBipedObjectForm::BipedObjectSlot;
			return !a->GetWornArmor(Slot_::kBody) && !a->GetWornArmor(Slot_::kFeet);
		}

		bool IsGagClosed(RE::Actor* a)
		{
			if (!g_kGag) return false;
			return WornHasKeyword(a, g_kGag) || WornHasKeyword(a, g_kGagPlate) || WornHasKeyword(a, g_kGagLarge);
		}

		bool IsGagRing(RE::Actor* a) { return g_kGagRing && WornHasKeyword(a, g_kGagRing); }

		bool IsBlind(RE::Actor* a)
		{
			if (!g_kBlind) return false;
			return WornHasKeyword(a, g_kBlind) || WornHasKeyword(a, g_kHood);
		}

		int RelationshipRank(RE::Actor* a, RE::Actor* b)
		{
			auto* na = a ? a->GetActorBase() : nullptr;
			auto* nb = b ? b->GetActorBase() : nullptr;
			if (!na || !nb) return 0;
			auto* rel = RE::BGSRelationship::GetRelationship(na, nb);
			if (!rel) return 0;
			// kLover(0) .. kArchnemesis(8) map to Papyrus ranks 4 .. -4.
			return 4 - static_cast<int>(rel->level.get());
		}

		bool NaturalGazeAngle(RE::Actor* a, RE::Actor* target)
		{
			if (!a || !target) return false;
			return std::abs(a->GetHeadingAngle(target->GetPosition(), false)) <= 75.0f;
		}

		bool OBlushLikely(RE::Actor* a, int raw)
		{
			if (!S::bOBlushSync || !a || !g_oblush) return false;
			if (g_oblushEnable && g_oblushEnable->value <= 0.0f) return false;
			const int sex = ActorSex(a);
			if (sex == 1 && g_oblushFemale && g_oblushFemale->value <= 0.0f) return false;
			if (sex == 0 && g_oblushMale && g_oblushMale->value <= 0.0f) return false;
			const float minv = g_oblushMin ? g_oblushMin->value : 45.0f;
			const float maxv = g_oblushMax ? g_oblushMax->value : 100.0f;
			return raw >= static_cast<int>(minv) && raw <= static_cast<int>(maxv);
		}

		float SceneTime(const Thread& t)
		{
			if (t.start <= 0.0f) return 0.0f;
			return std::max(0.0f, Scenes::Now() - t.start);
		}

		int EffectiveIntensity(Thread& t, RE::Actor* a)
		{
			int enj = Raw(a);
			if (t.leadin) enj = (enj * 4) / 10;
			if (S::bPaceBoost) {
				const int boost = std::min(t.stageSeq * 4, 25);
				int velocity = 0;
				if (S::bSpeedSync && t.maxSpeed > 0) velocity = ClampI((t.speed * 18) / t.maxSpeed, 0, 18);
				enj += boost + velocity;
			}
			return ClampI(enj, 0, 130);
		}

		int PickSeed(Thread& t, int seed, int n)
		{
			int v = (seed + RandInt(0, n - 1)) % n;
			if (v == t.lastVariant && n > 1) v = (v + 1) % n;
			t.lastVariant = v;
			return v;
		}

		bool ActorHasAnyAction(Thread& t, const Slot& s, const TagList& types)
		{
			if (!t.meta || s.pos < 0) return false;
			const auto& m = *t.meta;
			return OStimData::FindAnyActionForActor(m, s.pos, types) >= 0 || OStimData::FindAnyActionForTarget(m, s.pos, types) >= 0 ||
			       OStimData::FindAnyActionForPerformer(m, s.pos, types) >= 0;
		}

		bool SceneHasAnyAction(Thread& t, const TagList& types)
		{
			return t.meta && OStimData::FindAnyAction(*t.meta, types) >= 0;
		}

		bool ActorHasActionTagAsActor(Thread& t, const Slot& s, const TagList& tags)
		{
			return t.meta && s.pos >= 0 && OStimData::FindActionTaggedForActor(*t.meta, s.pos, tags) >= 0;
		}

		bool ActorHasActionTagAsTarget(Thread& t, const Slot& s, const TagList& tags)
		{
			return t.meta && s.pos >= 0 && OStimData::FindActionTaggedForTarget(*t.meta, s.pos, tags) >= 0;
		}

		bool HasOralSceneTag(Thread& t)
		{
			if (!t.meta) return false;
			const auto& m = *t.meta;
			return OStimData::HasAnySceneTag(m, T().tagOralAction) || OStimData::HasAnyActionTagOnAny(m, T().tagOralAction) ||
			       OStimData::FindAnyAction(m, T().actionOral) >= 0;
		}

		RE::Actor* PartnerFromAction(Thread& t, const Slot& s, const TagList& types)
		{
			if (!t.meta || s.pos < 0) return nullptr;
			const auto& m = *t.meta;
			auto byPos = [&](int pos) -> RE::Actor* {
				auto* p = t.FindPos(pos);
				return p ? p->Get() : nullptr;
			};
			if (int i = OStimData::FindAnyActionForActor(m, s.pos, types); i >= 0) return byPos(m.actions[i].target);
			if (int i = OStimData::FindAnyActionForTarget(m, s.pos, types); i >= 0) return byPos(m.actions[i].actor);
			if (int i = OStimData::FindAnyActionForPerformer(m, s.pos, types); i >= 0) {
				auto* p = byPos(m.actions[i].actor);
				if (p && p != s.Get()) return p;
				return byPos(m.actions[i].target);
			}
			return nullptr;
		}

		RE::Actor* PrimaryPartner(Thread& t, const Slot& s)
		{
			RE::Actor* self = s.Get();
			for (const auto* list : { &T().actionKiss, &T().actionVaginal, &T().actionAnal, &T().actionOral }) {
				if (auto* p = PartnerFromAction(t, s, *list); p && p != self) return p;
			}
			for (auto& o : t.slots) {
				auto* p = o.Get();
				if (p && p != self) return p;
			}
			return nullptr;
		}

		int ActRole(Thread& t, Slot& s, RE::Actor* a)
		{
			if (!S::bActTypeAware || !S::bRoleMetadata) return 0;
			if (MouthYielded(t, s, a)) return 1;
			if (ActorHasAnyAction(t, s, T().actionKiss)) return 2;
			if (ActorHasAnyAction(t, s, T().actionVaginal) || ActorHasAnyAction(t, s, T().actionAnal)) return 3;
			return 0;
		}

		int PositionRole(Thread& t, const Slot& s)
		{
			if (!S::bRoleMetadata || s.pos < 0 || !t.meta) return 0;
			const auto& m = *t.meta;
			if (OStimData::HasAnyActorTag(m, s.pos, T().tagDom)) return 1;
			if (OStimData::HasAnyActorTag(m, s.pos, T().tagSub)) return -1;
			int dom = 0;
			int sub = 0;
			if (OStimData::FindAnyActionForActor(m, s.pos, T().actionPenetration) >= 0) ++dom;
			if (OStimData::FindAnyActionForTarget(m, s.pos, T().actionPenetration) >= 0) ++sub;
			if (OStimData::FindAnyActionForActor(m, s.pos, T().actionOral) >= 0) ++sub;
			if (ActorHasActionTagAsActor(t, s, T().tagOralAction)) ++sub;
			if (OStimData::FindAnyActionForTarget(m, s.pos, T().actionOral) >= 0) ++dom;
			if (ActorHasActionTagAsTarget(t, s, T().tagOralAction)) ++dom;
			if (dom > sub) return 1;
			if (sub > dom) return -1;
			return 0;
		}

		bool FaceVictim(Thread& t, const Slot& s)
		{
			return !t.consent && (!t.victimKnown || IsSubmissive(t, s));
		}

		bool IsSubmissive(Thread& t, const Slot& s)
		{
			// The player's magic compelled the NPCs it hit, whatever the animation's roles say.
			// Everyone else (the player, a follower who asked to join) is an aggressor.
			if (t.spellNonConsent) return t.SpellVictim(s);
			if (!S::bAggressorGrammar) return false;
			if (t.meta && s.pos >= 0) {
				if (OStimData::HasAnyActorTag(*t.meta, s.pos, T().tagSub)) return true;
				if (OStimData::HasAnyActorTag(*t.meta, s.pos, T().tagDom)) return false;
			}
			return (t.toneForced || t.toneRough) && PositionRole(t, s) < 0;
		}

		// Whether the scene's own data puts this actor in an action, as the one doing it or the one it is
		// done to. When it does, the roles are known and a guess about everyone in an oral scene must not
		// overrule them: the receiver of cunnilingus is not the one whose mouth is busy.
		bool SceneNamesActor(const Thread& t, const Slot& s)
		{
			if (!t.meta || s.pos < 0) return false;
			return std::ranges::any_of(t.meta->actions, [&](const OStimData::Action& a) {
				return a.actor == s.pos || a.target == s.pos || a.performer == s.pos;
			});
		}

		bool ActorIsOralMouthActor(Thread& t, const Slot& s)
		{
			if (!t.meta || s.pos < 0) return false;
			const auto& m = *t.meta;
			return OStimData::FindAnyActionForActor(m, s.pos, T().actionOral) >= 0 || ActorHasActionTagAsActor(t, s, T().tagOralAction) ||
			       (t.sceneOral && OStimData::HasAnyActorTag(m, s.pos, T().tagOralAction)) ||
			       (t.sceneOral && t.PaintedCount() <= 2 && HasOralSceneTag(t) && !SceneNamesActor(t, s));
		}

		bool DialogueMouthYielded(Thread& t, RE::Actor* a)
		{
			if (!S::bDialogueMouthYield || !a) return false;
			// Mfg's IsInDialogue: the face is currently playing dialogue lip data.
			if (auto* fg = a->GetFaceGenAnimationData(); fg && fg->dialogueData) return true;
			if (auto* mtm = RE::MenuTopicManager::GetSingleton()) {
				auto speaker = mtm->speaker.get();
				if (speaker && speaker.get() == a) return true;
			}
			return t.dialogueMenuOpen;
		}

		bool HeadCommittedToAnimation(Thread& t, Slot& s, RE::Actor* a)
		{
			if (!a || !t.active) return false;
			if (DialogueMouthYielded(t, a)) return true;
			if (s.exprOverride) return true;
			return ActorIsOralMouthActor(t, s);
		}

		bool LipSyncMouthActive(Slot& s, RE::Actor* a)
		{
			if (s.externalMouthUntil > Scenes::Now()) return true;
			return Output::HasMouthOverride(a);
		}

		std::string MouthOwnerLabel(Thread& t, Slot& s, RE::Actor* a, bool yielded)
		{
			if (!yielded) return LipSyncMouthActive(s, a) ? "Face + Lip-Sync" : "OStim mouth";
			if (LipSyncMouthActive(s, a)) return "Lip-Sync";
			if (AhegaoYield()) return "Ahegao mod";
			if (DialogueMouthYielded(t, a)) return "Dialogue/lip-sync";
			if (s.ppaYield) return "PPA";
			if (s.exprOverride) return "OStim override";
			return "Oral action";
		}

		float RapidFactor(const Slot& s)
		{
			if (s.rapidRun < 1 || S::fRapidOrgasmSeconds <= 0.0f) return 0.0f;
			return ClampF(1.0f - s.orgasmGap / S::fRapidOrgasmSeconds, 0.0f, 1.0f);
		}

		float Sensitivity(const Slot& s, float now)
		{
			if (s.rapidRun < 1 || s.climaxing || S::fRapidOrgasmSeconds <= 0.0f) return 0.0f;
			const float since = now - s.lastOrgasmAt;
			if (since >= S::fRapidOrgasmSeconds) return 0.0f;
			return std::min(0.35f, 0.12f * static_cast<float>(s.rapidRun)) * (1.0f - since / S::fRapidOrgasmSeconds);
		}

		// How long the climax face lasts for this actor's latest orgasm: the setting for one on its own, shorter for one that came
		// quickly after the last (by up to 60%), and never most of the gap since it, so the face has time to come back down.
		float ClimaxLength(const Slot& s)
		{
			const float full = ClampF(S::fClimaxSeconds, 4.0f, 30.0f);
			// The clip's own length: a long standard orgasm is the setting, a short one under half of it, and a rapid one is shorter again
			// and never most of the gap, so the face has time to come back down before the next.
			switch (s.climaxKind) {
			case 0: return full;
			case 1: return std::max(4.0f, full * 0.45f);
			// A rapid clip is at most half the gap since the last orgasm (the 2.0.3 test at a 7 s cadence spent about 60% of the
			// time on the climax with a 60% cap): the other half is left for the build-up face. The short form is kept shorter than
			// the long even at a fast cadence. Floors keep a clip long enough to play in the 0.8 s steps.
			case 2: return std::max(3.0f, std::min(full * 0.65f, s.orgasmGap * 0.50f));
			case 3: return std::max(2.5f, std::min(full * 0.35f, s.orgasmGap * 0.35f));
			default: break;
			}
			// The built-in template (the pool off, or a non-consensual scene): shorter for a rapid one, by up to 60%.
			const float r = RapidFactor(s);
			if (r <= 0.0f) return full;
			return std::max(3.0f, std::min(full * (1.0f - 0.6f * r), s.orgasmGap * 0.50f));
		}

		int SelectDominant(Thread& t, const Slot& s, int enj, int raw)
		{
			using namespace Scenes;
			if (S::bHardExclusionGate && !t.consent) return kDistress;
			const float now = Scenes::Now();
			// Their own orgasm holds the climax for its timed length. It was the thread's: five beats from the latest orgasm of anyone,
			// re-armed by each, so repeated orgasms (every 7-14 s in the 1.9.7 test) held one actor's climax face for two minutes and gave a
			// partner at 90 excitement the face before their own orgasm. Afterglow is theirs too, and ends when they are building again.
			if (s.climaxing && now < s.climaxUntil) return kClimax;
			if (now < s.afterglowUntil && raw < kAfterglowEndsAt) return kAfterglow;
			if (!t.consent) return kDistress;
			if (t.leadin || enj < 25) return kAnticipation;
			if (S::bNaturalDetail && t.plateau >= 3) return kPlateau;
			return kPleasure;
		}

		int PhrasePhase(Thread& t, int idx, int enjEff)
		{
			if (!S::bPhraseGrammar) return 1;
			int offset = idx;
			if (S::bGroupConductor && t.PaintedCount() >= 3) offset += (idx * 2) + ((idx + t.lastVariant + 5) % 3);
			return std::max(0, (t.tick + offset + (enjEff / 35)) % 5);
		}

		int ScenarioCode(Thread& t, int dom, int enjEff, int /*role*/, int tone, int posRole)
		{
			using namespace Scenes;
			if (!S::bScenarioCycler) return 0;
			if (dom == kAfterglow) return 8;
			if (dom == kClimax) return 4;
			if (dom == kDistress) return 7;
			if (t.leadin || dom == kAnticipation) return 0;
			if (tone == 5) return 9;
			if (enjEff >= 88) return 3;
			if (enjEff >= 72) return 2;
			if (posRole == -1 && enjEff >= 60) return 6;
			if (enjEff >= 45) return 1;
			return 5;
		}

		const char* ScenarioName(int scenario)
		{
			static constexpr std::array names{ "Entry", "Active", "Intense", "Near peak", "Peak", "Breathing", "Wanting", "Brace", "Ending", "Detached" };
			return scenario >= 0 && scenario < static_cast<int>(names.size()) ? names[scenario] : "Entry";
		}

		const char* DomName(int dom)
		{
			static constexpr std::array names{ "Anticipation", "Pleasure", "Plateau", "Distress", "Climax", "Afterglow" };
			return dom >= 0 && dom < static_cast<int>(names.size()) ? names[dom] : "Pleasure";
		}

		// Ahegao Expressions drives the whole face on its own schedule: its tongue can come out at
		// half arousal with its own expression behind it, and it fades in and out as it likes.
		// Sharing a face with that only produces a fight neither side wins, so when one is
		// installed we leave faces to it and keep to the body. Looked up once; plugins do not come
		// and go mid-session.
		bool AhegaoModInstalled()
		{
			static const bool has = [] {
				const bool found = HasPlugin("AhegaoExpressions.esp");
				logger::info("Ahegao Expressions {}", found ? "is installed: the face engine stands down" : "not found");
				return found;
			}();
			return has;
		}

		bool AhegaoYield() { return S::bAhegaoModYield || (S::bAhegaoAutoYield && AhegaoModInstalled()); }

		// Ahegao Expressions and friends put the tongue out with OActor.EquipObject(act, "tongue")
		// and then write their own phonemes. A tongue that is out but not ours means one of them
		// has this actor, so we stop writing its face until the tongue goes back in.
		bool ExternalAhegao(const Slot& s, RE::Actor* a)
		{
			// Never our own. A tongue we equipped can outlive the slot that recorded it - an
			// animation change rebuilds the slot - and without this check that stray read as an
			// ahegao mod, stood the face down, and left the tongue out with nothing to retract it.
			return s.tongueOut && !s.tongueOn && !OurTongue(a);
		}

		void SetOwners(Slot& s, std::string face, std::string mouth, std::string eye, std::string head)
		{
			s.faceOwner = std::move(face);
			s.mouthOwner = std::move(mouth);
			s.eyeOwner = std::move(eye);
			s.headOwner = std::move(head);
		}

		void PulseActor(Thread& t, Slot& s, RE::Actor* a, int dom, int phrase, int enj)
		{
			Pulse::Beat b;
			b.actor = a;
			b.thread = t.id;
			b.enj = enj;
			b.raw = Raw(a);
			b.dom = dom;
			b.phrase = phrase;
			b.consent = t.consent;
			b.victim = !t.consent && IsSubmissive(t, s);
			b.broken = s.broken;
			b.yieldMouth = MouthYielded(t, s, a);
			b.footAction = t.meta && s.pos >= 0 &&
			               (OStimData::FindAnyActionForActor(*t.meta, s.pos, T().actionFootActor) >= 0 ||
			                   OStimData::FindAnyActionForTarget(*t.meta, s.pos, T().actionFootTarget) >= 0);
			b.orgasm = t.orgasm;
			b.sceneTime = SceneTime(t);
			s.enj = enj;
			s.raw = b.raw;
			s.phrase = phrase;
			Pulse::Paint(b);
			if (s.lastPhrase != phrase) {
				Pulse::PhraseChanged(b);
				s.lastPhrase = phrase;
			}
			if (s.lastPulseDom != dom) {
				Pulse::DomChanged(b, s.lastPulseDom);
				s.lastPulseDom = dom;
			}
			s.dom = dom;
		}
	}

	// ------------------------------------------------------------------ public
	void OnDataLoaded()
	{
		g_ostim = HasPlugin("OStim.esp");
		PPA::Init();
		g_excitement = Lookup<RE::TESFaction>(0xD93, "OStim.esp");
		g_climaxed = Lookup<RE::TESFaction>(0xE49, "OStim.esp");
		g_kHuman = Lookup<RE::BGSKeyword>(0x13794, "Skyrim.esm");
		if (HasPlugin("OStimExpressionDirector_Keywords.esp")) {
			g_kStoic = Lookup<RE::BGSKeyword>(0x800, "OStimExpressionDirector_Keywords.esp");
			g_kVocal = Lookup<RE::BGSKeyword>(0x801, "OStimExpressionDirector_Keywords.esp");
			g_kShy = Lookup<RE::BGSKeyword>(0x802, "OStimExpressionDirector_Keywords.esp");
			g_kDominant = Lookup<RE::BGSKeyword>(0x803, "OStimExpressionDirector_Keywords.esp");
		}
		if (HasPlugin("Devious Devices - Assets.esm")) {
			g_kGag = Lookup<RE::BGSKeyword>(0x007EB8, "Devious Devices - Assets.esm");
			g_kGagPlate = Lookup<RE::BGSKeyword>(0x01F306, "Devious Devices - Assets.esm");
			g_kBlind = Lookup<RE::BGSKeyword>(0x011B1A, "Devious Devices - Assets.esm");
			g_kHood = Lookup<RE::BGSKeyword>(0x02AFA2, "Devious Devices - Assets.esm");
		}
		if (HasPlugin("Devious Devices - Integration.esm")) {
			g_kGagLarge = Lookup<RE::BGSKeyword>(0x0840F7, "Devious Devices - Integration.esm");
			g_kGagRing = Lookup<RE::BGSKeyword>(0x08C854, "Devious Devices - Integration.esm");
		}
		g_oblush = HasPlugin("OBlush.esp");
		if (g_oblush) {
			g_oblushEnable = Lookup<RE::TESGlobal>(0x800, "OBlush.esp");
			g_oblushMin = Lookup<RE::TESGlobal>(0x802, "OBlush.esp");
			g_oblushMax = Lookup<RE::TESGlobal>(0x803, "OBlush.esp");
			g_oblushMale = Lookup<RE::TESGlobal>(0x804, "OBlush.esp");
			g_oblushFemale = Lookup<RE::TESGlobal>(0x807, "OBlush.esp");
		}
		std::string found;
		if (HasPlugin("AhegaoExpressions.esp")) found = "Ahegao Expressions";
		if (HasPlugin("OAhegao.esp") || HasPlugin("OAhegaoNG.esp")) found += found.empty() ? "OAhegao" : ", OAhegao";
		g_ahegaoFound = found.empty() ? "none detected" : found;
		logger::info("Face engine: OStim {}, excitement faction {}, OBlush {}, Devious Devices {}, ahegao mods: {}",
			g_ostim, g_excitement != nullptr, g_oblush, g_kGag != nullptr, g_ahegaoFound);
	}

	bool OStimPresent() { return g_ostim; }
	bool OBlushPresent() { return g_oblush; }
	bool DevicesPresent() { return g_kGag != nullptr; }

	void ClearStrayTongues() { detail::ClearStrayTongues(); }

	bool AhegaoPresent() { return detail::AhegaoModInstalled(); }

	bool FaceYielded() { return detail::AhegaoYield(); }

	std::string AhegaoStatus()
	{
		std::string why;
		if (S::bAhegaoModYield) why = "forced on";
		else if (detail::AhegaoModInstalled()) why = S::bAhegaoAutoYield ? "Ahegao Expressions is installed" : "";
		if (!why.empty()) return "Yield ON (" + why + "): no faces written, body only. " + g_ahegaoFound;
		return "Yield OFF: " + g_ahegaoFound;
	}

	float StyleValue() { return detail::StyleValue(); }

	bool IsHuman(RE::Actor* a)
	{
		if (!a) return false;
		if (!g_kHuman) return true;  // never wrongly exclude
		return a->HasKeyword(g_kHuman);
	}

	int Excitement(RE::Actor* a)
	{
		if (!a || !g_excitement) return 0;
		return std::max(0, static_cast<int>(a->GetFactionRank(g_excitement, a->IsPlayerRef())));
	}

	int TimesClimaxed(RE::Actor* a)
	{
		if (!a || !g_climaxed) return 0;
		return std::max(0, static_cast<int>(a->GetFactionRank(g_climaxed, a->IsPlayerRef())));
	}

	// Whether something else owns the mouth outright, so the grammar must not write it. A moan
	// is deliberately not on this list: it rides on top of whatever face is being worn (see
	// Output::Update), because taking the mouth away for the length of every moan left the
	// expression grammar with no mouth to write at all.
	bool MouthYielded(Thread& t, Slot& s, RE::Actor* a)
	{
		if (!t.active || !a) return false;
		if (S::bDialogueMouthYield && DialogueMouthYielded(t, a)) return true;
		if (s.ppaYield) return true;  // PPA is playing its mouth preset on this actor
		if (!S::bYieldOralMouth) return false;
		// The Director plays the override pool (open mouth, tongue) itself, so the mouth is not handed away.
		if (detail::OverridesAreOurs()) return false;
		if (s.exprOverride) return true;
		if (!t.meta || !S::bRoleMetadata || s.pos < 0) return false;
		const auto& m = *t.meta;
		if (OStimData::FindAnyActionForActor(m, s.pos, T().actionOral) >= 0) return true;
		if (ActorHasActionTagAsActor(t, s, T().tagOralAction)) return true;
		if (t.sceneOral && OStimData::HasAnyActorTag(m, s.pos, T().tagOralAction)) return true;
		// Only for a scene that does not say what this actor is doing; with role data it is wrong for the receiver.
		if (t.sceneOral && t.PaintedCount() <= 2 && HasOralSceneTag(t) && !SceneNamesActor(t, s)) return true;
		return false;
	}

	// ---- personality
	namespace
	{
		// One of the newer personalities for someone nothing else has placed, rolled from a hash of the actor's form so the same person
		// always rolls the same: fNewPersonalityShare of them get one (timid 30, submissive 30, wild 25, crazed 15 in a hundred of those;
		// the lite edition's submissives are timid, so the same people roll the same everywhere bar that), the rest -1 and fall to the
		// original five.
		int NewPersonalityRoll(RE::Actor* a)
		{
			auto* b = a ? a->GetActorBase() : nullptr;
			if (!b || S::fNewPersonalityShare <= 0.0f) return -1;
			const std::uint32_t h = (b->GetFormID() * 2654435761u) >> 8;
			if (static_cast<float>(h % 1000u) / 1000.0f >= S::fNewPersonalityShare) return -1;
			const int pick = static_cast<int>((h / 1000u) % 100u);
			return EditionPersonality(pick < 30 ? Pers::kTimid : (pick < 60 ? Pers::kSubmissive : (pick < 85 ? Pers::kWild : Pers::kCrazed)));
		}

		// What the rules alone give for this actor, with nothing pinned. The caller holds Settings::lock.
		int ResolvePersonality(RE::Actor* a, std::string& source)
		{
			if (int sp = SPIDArchetype(a); sp >= 0) {
				source = "SPID";
				return sp;
			}
			if (g_kStoic && a->HasKeyword(g_kStoic)) return source = "Keyword", 1;
			if (g_kVocal && a->HasKeyword(g_kVocal)) return source = "Keyword", 2;
			if (g_kShy && a->HasKeyword(g_kShy)) return source = "Keyword", 3;
			if (g_kDominant && a->HasKeyword(g_kDominant)) return source = "Keyword", 4;
			if (S::bVoiceArchetype) {
				if (int va = VoiceArchetype(a); va >= 0) {
					source = "Voice";
					return va;
				}
			}
			if (int v = VanillaAIPersonality(a); v >= 0) {
				source = "Vanilla AI";
				return v;
			}
			if (const int extra = NewPersonalityRoll(a); extra >= 0) {
				source = "Seed (new type)";
				return extra;
			}
			source = "Seed";
			return Seed(a) % 5;
		}

		// Settle an actor's personality the first time it is needed, so it is the same in every scene and every load.
		// The SPID distribution is a roll made when the game loads, not a property of the actor: in the 1.9.3 test
		// the same two actresses came out bold and fierce in one run and shy in the next, and the faces they were
		// given followed the roll. The player gets one too. Not for children or creatures, which have no personality here.
		void PinPersonality(RE::Actor* a, int arch, const std::string& why)
		{
			if (!a || (!a->IsPlayerRef() && (!IsHuman(a) || a->IsChild()))) return;
			{
				std::scoped_lock l(g_dataLock);
				if (g_npcPersonality.size() >= kMaxPins && !g_npcPersonality.contains(a->GetFormID())) return;
				g_npcPersonality[a->GetFormID()] = arch | kPinnedFlag;
			}
			logger::info("Personality: {} ({:08X}) pinned as {} ({})", a->GetDisplayFullName(), a->GetFormID(), PersonalityName(arch), why);
		}
	}

	int Archetype(RE::Actor* a, std::string* source)
	{
		auto src = [&](const std::string& s) {
			if (source) *source = s;
		};
		if (!a) {
			src("Unavailable");
			return 0;
		}
		std::scoped_lock l(Settings::lock);
		if (const int chosen = EditionPersonality(S::iPlayerPersonality); a->IsPlayerRef() && PersonalityAvailable(chosen)) {
			src("Player set");
			return chosen;
		}
		if (const int stored = StoredPersonality(a); stored >= 0) {
			src((stored & kPinnedFlag) ? "Pinned" : (a->IsPlayerRef() ? "Player set" : "Player set (NPC)"));
			const int arch = EditionPersonality(stored & 0xFF);  // a save from the full edition can hold a submissive, which the lite edition plays as a timid
			return PersonalityAvailable(arch) ? arch : 0;
		}
		std::string why;
		const int arch = ResolvePersonality(a, why);
		PinPersonality(a, arch, why);
		src(why);
		return arch;
	}

	Reaction VictimReaction(int arch)
	{
		switch (arch) {
		case 1: return Reaction::kNumb;      // stoic
		case 2: return Reaction::kPanic;     // vocal
		case 3: return Reaction::kFear;      // shy
		case 4: return Reaction::kDefiance;  // dominant
		case Pers::kTimid: return Reaction::kFear;
		case Pers::kWild: return Reaction::kDefiance;
		case Pers::kCrazed: return Reaction::kDefiance;  // never cowers: it glares
		default: return Reaction::kBalanced;             // balanced, and submissive when it does not accept the scene
		}
	}

	const char* ReactionName(Reaction r)
	{
		switch (r) {
		case Reaction::kFear: return "fear";
		case Reaction::kPanic: return "panic";
		case Reaction::kNumb: return "numb";
		case Reaction::kDefiance: return "defiance";
		default: return "sad-to-fear";
		}
	}

	bool VictimCries(RE::Actor* a)
	{
		return a && VictimReaction(Archetype(a)) != Reaction::kDefiance;
	}

	const char* PersonalityName(int arch)
	{
		switch (arch) {
		case 1: return "Stoic";
		case 2: return "Vocal";
		case 3: return "Shy";
		case 4: return "Dominant";
		case Pers::kTimid: return "Timid";
#if !OSIS_LITE
		case Pers::kSubmissive: return "Submissive";
#endif
		case Pers::kWild: return "Wild";
		case Pers::kCrazed: return "Crazed";
		default: return "Balanced";
		}
	}

	int StoredPersonality(RE::Actor* a)
	{
		if (!a) return -1;
		std::scoped_lock l(g_dataLock);
		auto it = g_npcPersonality.find(a->GetFormID());
		return it != g_npcPersonality.end() ? it->second : -1;
	}

	int GetNpcPersonality(RE::Actor* a)
	{
		const int stored = StoredPersonality(a);
		return stored >= 0 && !(stored & kPinnedFlag) ? EditionPersonality(stored) : -1;  // only what the player chose
	}

	void SetNpcPersonality(RE::Actor* a, int arch)
	{
		if (!a || a->IsPlayerRef() || !IsHuman(a) || a->IsChild()) return;
		std::scoped_lock l(g_dataLock);
		if (arch < 0) g_npcPersonality.erase(a->GetFormID());
		else g_npcPersonality[a->GetFormID()] = PersonalityAvailable(EditionPersonality(arch)) ? EditionPersonality(arch) : 0;
	}

	// Forget every personality OSIS settled on by itself - the player's included - so they are worked out again
	// from the current rules the next time each is needed. What the player chose stays.
	std::size_t ForgetPinnedPersonalities()
	{
		std::scoped_lock l(g_dataLock);
		return std::erase_if(g_npcPersonality, [](const auto& kv) { return (kv.second & kPinnedFlag) != 0; });
	}

	std::unordered_map<RE::FormID, int> NpcPersonalities()
	{
		std::scoped_lock l(g_dataLock);
		return g_npcPersonality;
	}

	void SetNpcPersonalities(std::unordered_map<RE::FormID, int> m)
	{
		std::scoped_lock l(g_dataLock);
		g_npcPersonality = std::move(m);
	}

	// ---- OStim face writer ownership
	// Switch OStim's own face writer off for this actor, and keep it off.
	//
	// OActor.SetExpressionsEnabled does nothing at all when OStim has not yet registered the
	// actor in a thread (ActorScript.cpp: `if (!threadActor) return;`), and it says nothing
	// when that happens. The first call can easily land in that window at scene start, and
	// this used to mark the actor taken over regardless and never ask again - which left
	// OStim's writer running for the whole scene: its expressions over ours, so no faces, and
	// its moan expression opening the mouth on top of our lip-sync, so two mouth movements per
	// moan. The call is idempotent on OStim's side, so it is simply repeated every couple of
	// seconds while the actor is ours.
	void ReleaseOStimFace(Slot& s, RE::Actor* a)
	{
		if (AhegaoYield() || !g_ostim || !a) return;
		constexpr float kRecheck = 2.0f;
		const float now = Scenes::Now();
		// With the Director playing the pools itself, OStim's override expressions go off too: it has nothing left to
		// write. Otherwise they stay on, as they always did, and the mouth is handed to them for oral acts.
		const bool noOverride = detail::OverridesAreOurs();
		if (s.takenOver) {
			// Not while ClearOStimTongue has deliberately handed the face back for a moment.
			if (now < s.takeoverRecheck || now < s.tongueClearUntil - 2.5f) return;
			// SetExpressionsEnabled(false, ...) never lifts the override flag, so a change of setting needs both undone first.
			if (s.noOverride && !noOverride) Papyrus::SetExpressionsEnabled(a, true, true);
			Papyrus::SetExpressionsEnabled(a, false, !noOverride);
			s.noOverride = noOverride;
			s.takeoverRecheck = now + kRecheck;
			return;
		}
		Papyrus::SetExpressionsEnabled(a, false, !noOverride);
		s.noOverride = noOverride;
		s.takenOver = true;
		s.takeoverRecheck = now + kRecheck;
		logger::info("Face: took over OStim's face writer for {:08X} {}", a->GetFormID(), a->GetDisplayFullName());
		std::scoped_lock l(g_dataLock);
		g_takenOver.insert(a->GetFormID());
	}

	void RestoreOStimFace(Slot& s, RE::Actor* a)
	{
		if (!s.takenOver || !a) return;
		if (g_ostim) Papyrus::SetExpressionsEnabled(a, true, true);
		s.takenOver = false;
		std::scoped_lock l(g_dataLock);
		g_takenOver.erase(a->GetFormID());
	}

	void RestorePersistedTakeovers()
	{
		std::vector<RE::FormID> ids;
		{
			std::scoped_lock l(g_dataLock);
			ids.assign(g_takenOver.begin(), g_takenOver.end());
			g_takenOver.clear();
		}
		if (!g_ostim) return;
		for (auto id : ids) {
			if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id)) Papyrus::SetExpressionsEnabled(a, true, true);
		}
		if (!ids.empty()) logger::info("Restored OStim's face writer for {} actor(s)", ids.size());
	}

	std::vector<RE::FormID> TakenOverIDs()
	{
		std::scoped_lock l(g_dataLock);
		return { g_takenOver.begin(), g_takenOver.end() };
	}

	void SetTakenOverIDs(std::vector<RE::FormID> ids)
	{
		std::scoped_lock l(g_dataLock);
		g_takenOver = { ids.begin(), ids.end() };
	}

#if !OSIS_LITE
	namespace
	{
		// A submissive person accepts any means of sexual encounter - a rough or forced scene, the player's spell - from someone they are
		// close to: a relationship rank of 3 (ally) or 4 (lover) to everyone else in it. A scene that would be non-consensual is not if every
		// victim is such a person. The victims are whoever the scene's roles name, or everyone when they name nobody.
		bool SubmissivesAccept(Thread& t)
		{
			bool anyVictim = false;
			for (auto& s : t.slots) anyVictim = anyVictim || (s.Get() && IsSubmissive(t, s));
			bool any = false;
			for (auto& s : t.slots) {
				auto* a = s.Get();
				if (!a || (anyVictim && !IsSubmissive(t, s))) continue;
				any = true;
				if (Archetype(a) != Pers::kSubmissive) return false;
				for (auto& o : t.slots) {
					auto* b = o.Get();
					if (b && b != a && RelationshipRank(a, b) < 3) return false;
				}
			}
			return any;
		}
	}
#endif

	// ---- lifecycle
	void RefreshDerived(Thread& t, bool sceneChanged)
	{
		std::scoped_lock l(Settings::lock);
		if (t.meta) {
			t.maxSpeed = t.meta->maxSpeed > 0 ? t.meta->maxSpeed : (t.meta->defaultSpeed > 0 ? t.meta->defaultSpeed : 4);
		}
		if (t.normalPreWindow && t.meta && SceneHasAnimationSignal(*t.meta)) {
			t.normalAnimStarted = true;
			t.normalPreWindow = false;
			t.normalProbe = "animation-start";
		}
		if (sceneChanged) {
			++t.stageSeq;
			t.sceneOral = HasOralSceneTag(t);
		}
#if OSIS_LITE
		// This edition has no non-consent: every scene is consensual, whatever its tags.
		t.toneForced = false;
		t.toneRough = t.meta && OStimData::HasAnySceneTag(*t.meta, T().tagRough);
		t.toneLoving = t.meta && OStimData::HasAnySceneTag(*t.meta, T().tagLoving);
		t.spellNonConsent = false;
		t.consent = true;
#else
		t.toneForced = t.meta && OStimData::HasAnySceneTag(*t.meta, T().tagForced);
		t.toneRough = !t.toneForced && t.meta && OStimData::HasAnySceneTag(*t.meta, T().tagRough);
		t.toneLoving = t.meta && OStimData::HasAnySceneTag(*t.meta, T().tagLoving);
		// Only while one of the spell's victims is still in the thread.
		t.spellNonConsent = S::bSpellNonConsent && std::ranges::any_of(t.slots, [&](const Slot& s) { return t.SpellVictim(s); });
		t.consent = !(t.toneForced && S::bAggressorGrammar) && !t.spellNonConsent;
		const bool wasAccepted = t.acceptedBySubmissive;
		t.acceptedBySubmissive = !t.consent && SubmissivesAccept(t);
		if (t.acceptedBySubmissive) t.consent = true;
		if (t.acceptedBySubmissive != wasAccepted) {
			logger::info("Consent: thread {} {}", t.id,
				t.acceptedBySubmissive ? "would be non-consensual, but its victim(s) are submissive and close to everyone in it: playing it as consensual"
									   : "is no longer accepted by a submissive");
		}
#endif
		t.victimKnown = false;
		if (!t.consent) {
			for (auto& s : t.slots) {
				if (s.Get() && IsSubmissive(t, s)) {
					t.victimKnown = true;
					break;
				}
			}
		}
		for (auto& s : t.slots) s.victim = !t.consent && s.Get() && IsSubmissive(t, s);
		UpdateNormalStateFlag(t, sceneChanged);
	}

	float TickInterval(const Thread& t)
	{
		std::scoped_lock l(Settings::lock);
		if (t.orgasm && S::bClimaxPool) return 0.8f;  // a climax clip needs a face update every second or so, not every three
		if (S::bBreathing || S::bTongueLife || OSEDNeedsFastTick(t)) return 0.8f;
		float base = S::fBaseInterval;
		if (t.sceneOral && S::bYieldOralMouth && !detail::OverridesAreOurs()) base *= 1.75f;
		return ClampF(base + RandFloat(-S::fIntervalJitter, S::fIntervalJitter), 1.0f, 12.0f);
	}

	void OnThreadReady(Thread& t)
	{
		std::scoped_lock l(Settings::lock);
		PPA::Refresh();  // it reloads its config on a hotkey; follow it
		for (auto& s : t.slots) {
			if (auto* a = s.Get()) RequestVoiceName(a);
		}
		t.sceneOral = HasOralSceneTag(t);
		UpdateNormalStateFlag(t, true);
#if !OSIS_LITE
		if (!g_brokenCarry.empty()) {
			const float now = Scenes::Now();
			std::erase_if(g_brokenCarry, [&](const auto& kv) { return now - kv.second >= kBrokenCarry; });
			for (auto& s : t.slots) {
				auto it = g_brokenCarry.find(s.id);
				if (it == g_brokenCarry.end()) continue;
				g_brokenCarry.erase(it);
				auto* a = s.Get();
				if (S::bBrokenAfterClimax && a && s.painted && !t.consent && IsSubmissive(t, s)) Break(t, s, a, "is still the victim");
			}
		}
#endif
		logger::debug("thread {} ready: scene={} actors={} player={}", t.id, t.sceneID, t.slots.size(), t.hasPlayer);
#if !OSIS_LITE
		Voice::Sync(t);
		SceneLock::OnThreadReady(t);
#endif
		ApplyAll(t, true);
		t.nextTick = Scenes::Now() + TickInterval(t);
	}

	void OnSceneChanged(Thread& t)
	{
		std::scoped_lock l(Settings::lock);
		RefreshDerived(t, true);
		for (auto& sl : t.slots) sl.ppaGaveUp = false;  // a new node, a new look at whether PPA is driving the mouth
#if !OSIS_LITE
		Voice::Sync(t);
		SceneLock::OnSceneChanged(t);
#endif
		t.gasp = true;
		ApplyAll(t, true);
		t.gasp = false;
	}

	bool IsClimaxHeld(RE::Actor* a)
	{
		auto* t = Scenes::ThreadOf(a);
		auto* s = t ? t->Find(a) : nullptr;
		return s && s->stallActive;
	}

	void OnOrgasm(Thread& t, RE::Actor* a)
	{
		std::scoped_lock l(Settings::lock);
		auto* s = t.Find(a);
		if (!s) return;
		Pulse::Climax(a, t.id);
		s->holdSince = 0.0f;
		// A dominant's climax is the partner's permission, and they climax with it; a crazed one climaxes with their partner, whenever they do.
		if (S::bPersonalityControl && t.consent && t.slots.size() >= 2) {
			const int mine = Archetype(a);
			const float at = Scenes::Now();
			for (auto& o : t.slots) {
				auto* b = o.Get();
				if (&o == s || !b || at - o.forcedAt < 6.0f || o.climaxing) continue;
				const int theirs = Archetype(b);
				const bool partnerOfDominant = mine == Pers::kDominant && theirs != Pers::kDominant && o.stallActive && Raw(b) >= 80;
				const bool crazedWithPartner = theirs == Pers::kCrazed && mine != Pers::kCrazed;
				if (!partnerOfDominant && !crazedWithPartner) continue;
				o.forcedAt = at;
				o.stallActive = false;
				o.holdSince = 0.0f;
				Papyrus::PermitClimax(b);
				Papyrus::ForceClimax(b);
				logger::info("Control: {:08X} {} climaxes with {} ({})", b->GetFormID(), b->GetDisplayFullName(), a->GetDisplayFullName(),
					partnerOfDominant ? "the dominant lets them" : "crazed, with their partner");
			}
		}
		// This actor's orgasm, timed on its own (see SelectDominant): how long since their last decides how long the climax
		// face lasts and whether an afterglow follows.
		const float now = Scenes::Now();
		const bool again = s->orgasms > 0;
		s->orgasmGap = again ? now - s->lastOrgasmAt : 0.0f;
		s->rapidRun = again && s->orgasmGap < S::fRapidOrgasmSeconds ? s->rapidRun + 1 : 0;
		s->lastOrgasmAt = now;
		++s->orgasms;
		s->climaxStart = now;
		// This orgasm's climax clip: one of the fifteen faces, long or short, as a standard or a rapid orgasm.
		if (S::bClimaxPool && t.consent) PickClimaxClip(t, *s, Archetype(a));
		else s->climaxKind = -1;
		s->climaxUntil = now + ClimaxLength(*s);
		s->afterglowUntil = 0.0f;  // a new orgasm ends any afterglow
		s->climaxing = true;
		t.orgasm = true;  // somebody's climax is in progress: gaze and the thread-level consumers follow this
		const int c = TimesClimaxed(a);
		t.orgCount = c > t.orgCount ? c : t.orgCount + 1;
#if !OSIS_LITE
		if (S::bBrokenAfterClimax && !s->broken && s->painted && !t.consent && IsSubmissive(t, *s)) {
			Break(t, *s, a, "climaxed as the victim");
			s->shockUntil = Scenes::Now() + Settings::Voice::fShockSeconds;
			Voice::OnBreak(t, a);
		}
#endif
		ApplyAll(t, true);
		// The tick after an orgasm is on the quick clock (TickInterval), but the one already scheduled may be three seconds off.
		if (S::bClimaxPool) t.nextTick = std::min(t.nextTick, Scenes::Now() + 0.8f);
	}

	void OnTick(Thread& t)
	{
		std::scoped_lock l(Settings::lock);
		if (!t.active) return;
		RefreshDerived(t, false);
		UpdateExcitementRates(t);
		UpdateClimaxControl(t);
		++t.tick;
		if (t.leadin && SceneTime(t) > 3.0f) t.leadin = false;
		// Afterglow is counted in beats, and every orgasm event re-arms it. In a long scene with
		// repeated climaxes that never drained, and because afterglow outranks everything but
		// distress and climax it pinned the whole thread's faces - including actors who were
		// barely excited. It ends on the clock as well now, whatever the beats are doing.
		if (t.afterglow > 0 && Scenes::Now() > t.afterglowUntil) {
			t.afterglow = 0;
			logger::debug("thread {}: afterglow timed out after {:.0f}s", t.id, kAfterglowSeconds);
		}
		if (t.orgasm) {
			// Each actor's climax ends on the clock, and their own afterglow starts: the full length for an orgasm on its own, none for
			// one that came right after the last, since they are building again. The thread's flag follows whoever is still climaxing.
			const float now = Scenes::Now();
			bool ended = false;
			bool stillClimaxing = false;
			for (auto& sl : t.slots) {
				if (!sl.climaxing) continue;
				if (now < sl.climaxUntil) {
					stillClimaxing = true;
					continue;
				}
				sl.climaxing = false;
				ended = true;
				if (S::bCinematic) {
					const float len = kAfterglowSeconds * (1.0f - RapidFactor(sl));
					sl.afterglowUntil = len >= 3.0f ? sl.climaxUntil + len : 0.0f;
				}
			}
			t.orgasm = stillClimaxing;
			if (ended && !stillClimaxing) {
				if (S::bCinematic) {
					t.afterglow = 5;
					t.afterglowUntil = now + kAfterglowSeconds;
				}
				ClearGazeAll(t);
			}
		}
#if !OSIS_LITE
		Voice::Sync(t);
#endif
		ApplyAll(t, (t.tick % ArcEvery(t)) == 0);
		t.nextTick = Scenes::Now() + TickInterval(t);
	}

	void EndScene(Thread& t)
	{
		std::scoped_lock l(Settings::lock);
		{
			// OStim drops its per-scene rates with the scene; re-read them next time.
			std::scoped_lock rl(g_rateLock);
			for (auto& s : t.slots) {
				g_rateBase.erase(s.id);
				g_rateRequested.erase(s.id);
			}
		}
		for (auto& s : t.slots) {
			if (s.broken) g_brokenCarry[s.id] = Scenes::Now();
			auto* a = s.Get();
			if (!a) continue;
			Pulse::ClearActor(a, t.id);
			ClearOSEDPrototypeActor(s, a);
			ClearLook(a);
			Output::Release(a, 0.6f);
			RestoreOStimFace(s, a);
			if (auto m = s.marker.get()) {
				m->Disable();
				m->SetDelete(true);
			}
			s.marker.reset();
		}
		if (t.hasPlayer) ClearWatcherTrialActor();
#if !OSIS_LITE
		Voice::OnSceneEnd(t);
		SceneLock::OnSceneEnd(t);
#endif
		t.active = false;
		Pulse::SceneEnd(t.id);
		logger::debug("thread {} ended", t.id);
	}

	void StoreHint(RE::Actor* a, std::string_view kind, int strength)
	{
		auto* t = Scenes::ThreadOf(a);
		auto* s = t ? t->Find(a) : nullptr;
		if (!s) return;
		s->hintKind = std::string(kind);
		s->hintStrength = ClampF(static_cast<float>(strength) / 100.0f, 0.0f, 1.0f);
		s->hintUntil = Scenes::Now() + 20.0f;
	}

	void SetExternalMouth(RE::Actor* a, float until)
	{
		auto* t = Scenes::ThreadOf(a);
		auto* s = t ? t->Find(a) : nullptr;
		if (s) s->externalMouthUntil = until;
	}

	void TestOnActor(RE::Actor* a)
	{
		if (!a) return;
		std::scoped_lock l(Settings::lock);
		// A throwaway thread so the grammar has somewhere to keep its state.
		Thread t;
		t.id = -100;
		t.active = true;
		t.start = Scenes::Now() - 30.0f;
		t.normalAnimStarted = true;
		Slot s;
		s.handle = a->GetHandle();
		s.id = a->GetFormID();
		s.painted = true;
		t.slots.push_back(s);
		auto& slot = t.slots.front();
		if (detail::StyleValue() >= 1.5f) {
			ApplyOSEDAnimeAccent(t, slot, a, 95, false);
		} else if (S::iMode != S::kDirector) {
			ApplyOSEDLayerArc(t, slot, a, 0, false);
		} else {
			std::array<float, 32> e{};
			e[0] = 0.0f;
			e[11] = 0.4f;
			e[22] = e[23] = 0.4f;
			e[28] = e[29] = 0.5f;
			e[30] = 12.0f;
			e[31] = 0.6f;
			Output::ApplyPreset(a, e, false, S::fGlobalStrength * ProfileScale(), S::fGlobalStrength, S::fGlobalStrength, S::fTransition);
		}
	}
}
