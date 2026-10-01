// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// Shared helpers for the face engine translation units (Engine.cpp, Director.cpp, Layer.cpp).
// Function names follow OSExpressionFaces.psc so the port can be checked side by side.

#include "Face/Engine.h"
#include "Face/Output.h"
#include "OStimData.h"
#include "Scenes.h"
#include "Settings.h"

namespace Face::Engine::detail
{
	using Scenes::Slot;
	using Scenes::Thread;
	using OStimData::TagList;

	namespace S = Settings::Face;

	// ---- numbers
	[[nodiscard]] inline float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
	[[nodiscard]] inline int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
	[[nodiscard]] int RandInt(int lo, int hi);          // inclusive, like Utility.RandomInt
	[[nodiscard]] float RandFloat(float lo, float hi);

	// ---- tag lists (lowercase)
	struct Tags
	{
		TagList actionOral, actionKiss, actionVaginal, actionAnal, actionPenetration, actionAnySignal;
		TagList actionFootActor, actionFootTarget;  // whose feet: footjob's actor, everything else's target
		TagList tagOralAction, tagForced, tagRough, tagLoving, tagSub, tagDom;
		TagList deepthroat;
	};
	[[nodiscard]] const Tags& T();

	// ---- style / scaling
	[[nodiscard]] float StyleValue();
	[[nodiscard]] float StyleAmp();
	[[nodiscard]] float EyeScale();
	[[nodiscard]] float BrowScale();
	[[nodiscard]] int EyeValue(int v);
	[[nodiscard]] int BrowValue(int v);
	[[nodiscard]] float ProfileScale();
	[[nodiscard]] float MouthGate();

	// ---- face writes in the original Mfg units (0-100). SetMod takes the Mfg *preset*
	// index (18-29) that the original code used and maps it to the real modifier id.
	void SetMod(RE::Actor* a, int a_presetIndex, int a_value, float a_speed);
	void SetPh(RE::Actor* a, int a_id, int a_value, float a_speed);
	void ResetPh(RE::Actor* a, float a_speed);

	// ---- actor facts
	[[nodiscard]] int Raw(RE::Actor* a);
	[[nodiscard]] int Seed(RE::Actor* a);
	[[nodiscard]] float PersonalityMod(int seed);
	[[nodiscard]] int ActorSex(RE::Actor* a);      // 0 male, 1 female
	[[nodiscard]] bool IsNude(RE::Actor* a);
	[[nodiscard]] bool IsGagClosed(RE::Actor* a);
	[[nodiscard]] bool IsGagRing(RE::Actor* a);
	[[nodiscard]] bool IsBlind(RE::Actor* a);
	[[nodiscard]] int RelationshipRank(RE::Actor* a, RE::Actor* b);
	[[nodiscard]] bool NaturalGazeAngle(RE::Actor* a, RE::Actor* target);
	[[nodiscard]] bool OBlushLikely(RE::Actor* a, int raw);

	// ---- thread facts
	[[nodiscard]] float SceneTime(const Thread& t);
	[[nodiscard]] int EffectiveIntensity(Thread& t, RE::Actor* a);
	[[nodiscard]] int PickSeed(Thread& t, int seed, int n);
	[[nodiscard]] bool ActorHasAnyAction(Thread& t, const Slot& s, const TagList& types);
	[[nodiscard]] bool SceneHasAnyAction(Thread& t, const TagList& types);
	[[nodiscard]] bool ActorHasActionTagAsActor(Thread& t, const Slot& s, const TagList& tags);
	[[nodiscard]] bool ActorHasActionTagAsTarget(Thread& t, const Slot& s, const TagList& tags);
	[[nodiscard]] bool HasOralSceneTag(Thread& t);
	[[nodiscard]] RE::Actor* PartnerFromAction(Thread& t, const Slot& s, const TagList& types);
	[[nodiscard]] RE::Actor* PrimaryPartner(Thread& t, const Slot& s);
	[[nodiscard]] int ActRole(Thread& t, Slot& s, RE::Actor* a);
	[[nodiscard]] int PositionRole(Thread& t, const Slot& s);
	[[nodiscard]] bool IsSubmissive(Thread& t, const Slot& s);
	// Faces only: the victim of a non-consensual scene. When nobody can be identified, every
	// actor keeps the victim treatment rather than guessing an aggressor.
	[[nodiscard]] bool FaceVictim(Thread& t, const Slot& s);
	[[nodiscard]] bool ActorIsOralMouthActor(Thread& t, const Slot& s);
	[[nodiscard]] bool DialogueMouthYielded(Thread& t, RE::Actor* a);
	[[nodiscard]] bool HeadCommittedToAnimation(Thread& t, Slot& s, RE::Actor* a);
	[[nodiscard]] bool LipSyncMouthActive(Slot& s, RE::Actor* a);
	[[nodiscard]] std::string MouthOwnerLabel(Thread& t, Slot& s, RE::Actor* a, bool yielded);
	[[nodiscard]] int SelectDominant(Thread& t, int enj, int raw);
	[[nodiscard]] int PhrasePhase(Thread& t, int idx, int enjEff);
	[[nodiscard]] int ScenarioCode(Thread& t, int dom, int enjEff, int role, int tone, int posRole);
	[[nodiscard]] const char* ScenarioName(int scenario);
	[[nodiscard]] const char* DomName(int dom);
	[[nodiscard]] bool AhegaoYield();

	void SetOwners(Slot& s, std::string face, std::string mouth, std::string eye, std::string head);
	void PulseActor(Thread& t, Slot& s, RE::Actor* a, int dom, int phrase, int enj);

	// ---- gaze / head
	void ClearLook(RE::Actor* a);
	void LookAt(RE::Actor* a, RE::Actor* target);
	void LookAtOffset(Slot& s, RE::Actor* a, float x, float y, float z);  // marker relative to the actor
	void SetGaze(Thread& t, Slot& s, RE::Actor* a, RE::Actor* partner);
	void ClearGazeAll(Thread& t);
	void ApplyHeadflow(Thread& t, Slot& s, RE::Actor* a, int idx, int dom, int phrase, RE::Actor* partner, int arch, int posRole, int scenario, float overwhelm, bool yieldMouth);
	void BodyDemo(Thread& t, Slot& s, RE::Actor* a, int dom);

	// ---- Director (legacy mode 2 grammar, now the default)
	void ApplyArc(Thread& t, Slot& s, RE::Actor* a, int idx, bool yieldMouth);
	void Breathe(Thread& t, Slot& s, RE::Actor* a, int idx);

	// ---- Assist / Enhanced layer, anime, tongue, normal state, watcher (Layer.cpp)
	void ApplyOSEDLayerArc(Thread& t, Slot& s, RE::Actor* a, int idx, bool yieldMouth);
	void ApplyOSEDLayerBreath(Thread& t, Slot& s, RE::Actor* a, int idx);
	bool OSEDShouldAnime(Thread& t, Slot& s, RE::Actor* a, int raw, bool yieldMouth);
	void ApplyOSEDAnimeAccent(Thread& t, Slot& s, RE::Actor* a, int raw, bool yieldMouth);
	void ClearOSEDAnimeAccent(Slot& s, RE::Actor* a);
	void UpdateOSEDTongue(Thread& t, Slot& s, RE::Actor* a, bool yieldMouth);
	void SetOSEDTongue(Slot& s, RE::Actor* a, bool on);
	void ClearOSEDPrototypeActor(Slot& s, RE::Actor* a);
	void PlayOSEDExpressionEvent(Thread& t, Slot& s, RE::Actor* a, const std::string& ev, float minGap, bool force = false);
	void ExpireOSEDExpressionEvent(Slot& s, RE::Actor* a);
	void ClearOSEDExpressionEvent(Slot& s, RE::Actor* a, bool force);
	void UpdateNormalStateFlag(Thread& t, bool sceneChanged);
	[[nodiscard]] bool SceneHasAnimationSignal(const OStimData::Scene& scene);
	[[nodiscard]] float NormalHintBoost(Slot& s);
	void ApplyNormalState(Thread& t, Slot& s, RE::Actor* a, int idx, bool yieldMouth);
	void MaybeApplyWatcherTrial(Thread& t);
	void ClearWatcherTrialActor();
	[[nodiscard]] bool OSEDAnimationActive(const Thread& t);
	[[nodiscard]] bool OSEDNeedsFastTick(const Thread& t);
}
