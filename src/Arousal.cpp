// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Arousal.h"

#include "Body.h"
#include "Compat.h"
#include "Face/Engine.h"
#include "FsUtil.h"
#include "Overlays.h"
#include "Papyrus.h"
#include "Scenes.h"
#include "Settings.h"

namespace Arousal
{
	namespace
	{
		namespace S = Settings::Arousal;

		constexpr auto kMorphKey = "OSIS_Arousal";
		constexpr auto kLegacyMorphKey = "OSEDReborn_Arousal";  // pre-rename builds; cleared, never written
		// An actor missing from one update (3D reload at orgasm, a load door, briefly out of
		// the high process) keeps its state this long before returning to rest.
		constexpr float kGraceSeconds = 15.0f;
		constexpr auto kLegacyKey = "SoftbodyArousal";  // Softbody Arousal 2.0 morphs, still in old saves

		struct ActorState
		{
			RE::ActorHandle handle;
			std::string name;
			float arousal = 0.0f;   // 0-100, arousal mod
			bool haveArousal = false;
			bool seeded = false;    // level starts at the real state, not at zero
			float lastSeen = 0.0f;
			float target = 0.0f;    // 0-1
			float level = 0.0f;     // 0-1
			float flushMult = 1.0f;
			float climaxUntil = 0.0f;
			float exc = 0.0f;       // OStim excitement 0-1 as last read, and when
			float excAt = 0.0f;
			float excRate = 0.0f;   // how fast it is rising per second, smoothed
			float boost = 0.0f;     // 1 during an orgasm, then fading: the full range shows only then
			float shown = 1.0f;     // 0-1 share of each response's range that shows
			float faceFlush = 0.0f; // a stoic's face flush for Skin: the same gate, at the peak share; others use level x flushMult
			float blushGate = 1.0f; // 0-1 how much of the body blush shows: all of it, except for a stoic (the last seconds before an orgasm and the orgasm)
			int pers = 0;           // the actor's personality, as of the last Target
			std::string why = "arousal";
			std::vector<float> applied;
			std::uint32_t lastTick = 0;
			bool legacyCleared = false;
			bool female = true;

			std::vector<std::string> blushNodes;
			std::vector<std::string> blushTextures;
			std::vector<float> blushAlpha;
			const void* last3D = nullptr;
		};

		bool g_hasOSL = false;
		bool g_hasSLO = false;
		std::atomic_bool g_clearRequested = false;
		std::uint32_t g_tick = 0;
		std::unordered_set<std::string> g_missingBlush;  // texture paths already warned about
		bool g_warnedSlots = false;
		float g_lastTick = 0.0f;
		float g_nextTick = 0.0f;
		constexpr float kLoadGrace = 6.0f;  // seconds after a load before the first overlay work
		int g_bodyOverlays = 6;

		std::mutex g_stateLock;  // main thread + VM callbacks + UI
		std::unordered_map<RE::FormID, ActorState> g_states;

		struct Snap
		{
			bool enabled, player, npcs, ostim, factors, personality;
			int source, maxNPCs;
			float intensity, radius, rise, fall, climaxHold;
			std::vector<S::Morph> morphs;
			bool blush;
			int firstSlot, slots;
			std::vector<S::Blush> blushes;
			std::vector<S::RaceBlush> raceBlush;
			bool matte;
			bool genitals;
			bool shaped;
			float floorShown, ceilingShown, peakWindow, stoicRest, stoicPeak;
		};

		Snap CopySettings()
		{
			std::scoped_lock l(Settings::lock);
			return { S::bEnabled && Settings::General::bEnabled, S::bAffectPlayer, S::bAffectNPCs, S::bOStimExcitement, S::bSceneFactors,
				S::bPersonality, S::iSource, S::iMaxNPCs, S::fIntensity, S::fRadius, S::fRiseHalfLife, S::fFallHalfLife, S::fClimaxHold,
				S::morphs, S::bBlush, S::iOverlayFirstSlot, S::iOverlaySlots, S::blushes, S::raceBlush, Settings::Skin::bMatteOverlays,
				Settings::Body::bGenitals, S::bShapedResponse, S::fResponseFloor, S::fResponseCeiling, S::fPeakWindow, S::fStoicRest, S::fStoicPeak };
		}

		float Ease(float level, float start, float full)
		{
			const float t = std::clamp((level - start) / (full - start), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		bool SexMatch(int want, bool female)
		{
			return want == S::kAnySex || (want == S::kFemaleBody) == female;
		}

		// A row's own texture wins; otherwise the Body Blushing region of that name. Paths are
		// given relative to Data\textures, and a leading data\ or textures\ is tolerated.
		std::string BlushTexture(const S::Blush& b)
		{
			if (b.texture.empty()) return "Actors\\Character\\Overlays\\CheeseBlushOverlays\\" + b.name + ".dds";
			std::string p = b.texture;
			if (_strnicmp(p.c_str(), "data\\", 5) == 0 || _strnicmp(p.c_str(), "data/", 5) == 0) p.erase(0, 5);
			if (_strnicmp(p.c_str(), "textures\\", 9) == 0 || _strnicmp(p.c_str(), "textures/", 9) == 0) p.erase(0, 9);
			return p;
		}

		std::string RaceID(RE::Actor* a)
		{
			auto* race = a->GetRace();
			std::string id = race ? race->GetFormEditorID() : "";
			std::ranges::transform(id, id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return id;
		}

		// The same alpha reads very differently on pale and on dark skin, so each race scales it.
		// First substring match wins; a race with no row is left at 1.
		float BlushRaceMult(RE::Actor* a, const Snap& s)
		{
			const std::string id = RaceID(a);
			for (const auto& r : s.raceBlush) {
				if (!r.race.empty() && id.contains(r.race)) return r.mult;
			}
			return 1.0f;
		}

		// Overlay tint per race; the Body Blushing textures are neutral grey, so the tint
		// supplies the hue. nullopt for races that should not blush.
		std::optional<std::int32_t> BlushTint(RE::Actor* a)
		{
			const std::string id = RaceID(a);
			if (id.contains("khajiit") || id.contains("argonian") || id.contains("vampire")) return std::nullopt;
			if (id.contains("redguard")) return 0xC01810;
			if (id.contains("darkelf")) return 0xA81858;
			if (id.contains("orc")) return 0xB02818;
			if (id.contains("woodelf")) return 0xF02A20;
			return 0xFF2030;
		}

		void ClearBlush(RE::Actor* a, ActorState& st)
		{
			for (const auto& node : st.blushNodes) Papyrus::ClearOverlay(a, st.female, node);
			st.blushNodes.clear();
			st.blushTextures.clear();
			st.blushAlpha.clear();
		}

		void ApplyBlush(RE::Actor* a, ActorState& st, const Snap& s)
		{
			std::vector<const S::Blush*> active;
			const auto tint = s.blush ? BlushTint(a) : std::nullopt;
			if (s.blush) {
				for (const auto& b : s.blushes) {
					// A row with its own colour paints even on a race that has no default tint.
					if (!b.enabled || (!tint && b.tint < 0)) continue;
					if (!SexMatch(b.sex, st.female)) continue;
					if (active.size() < static_cast<size_t>(s.slots)) active.push_back(&b);
				}
			}
			// A row whose texture is not installed is dropped rather than painted: the slot would
			// render black over the body, which reads as a shader bug.
			std::erase_if(active, [](const S::Blush* b) {
				const auto path = BlushTexture(*b);
				if (FsUtil::TextureExists(path)) return false;
				if (g_missingBlush.insert(path).second) {
					logger::warn("Arousal: body blush texture not found, that row is skipped: {}", path);
				}
				return true;
			});
			std::vector<std::string> textures;
			for (const auto* b : active) textures.push_back(BlushTexture(*b));

			const void* root = a->Get3D();
			if (textures.empty()) {
				if (!st.blushNodes.empty()) ClearBlush(a, st);
				st.last3D = root;
				return;
			}
			// Rebuild on config or 3D change, and periodically so anything dropped heals.
			constexpr std::uint32_t kRefreshTicks = 10;
			if (textures != st.blushTextures || root != st.last3D || g_tick % kRefreshTicks == 0) {
				st.last3D = root;
				Papyrus::AddOverlays(a);
				// Which slots are actually free on this actor, rather than the configured run:
				// ODF, an ahegao mod or a hand-painted overlay may be sitting in them.
				const int have = BodyOverlaySlots();
				const auto claimed = Overlays::Claim(a, false, static_cast<int>(textures.size()), s.firstSlot,
					std::min(have, s.firstSlot + s.slots), textures);
				if (claimed.size() < textures.size()) {
					textures.resize(claimed.size());
					active.resize(claimed.size());
					if (!g_warnedSlots) {
						g_warnedSlots = true;
						logger::warn("Arousal: only {} body overlay slot(s) free of {}; some blush rows are not painted",
							claimed.size(), have);
					}
				}
				std::vector<std::string> nodes;
				for (size_t i = 0; i < textures.size(); ++i) {
					std::string node = std::format("Body [Ovl{}]", claimed[i]);
					Papyrus::SetOverlayTexture(a, st.female, node, textures[i]);
					Papyrus::SetOverlayTint(a, st.female, node, active[i]->tint >= 0 ? active[i]->tint : *tint);
					if (s.matte) Papyrus::SetOverlayMatte(a, st.female, node);
					nodes.push_back(std::move(node));
				}
				for (size_t i = nodes.size(); i < st.blushNodes.size(); ++i) Papyrus::ClearOverlay(a, st.female, st.blushNodes[i]);
				st.blushNodes = std::move(nodes);
				st.blushTextures = std::move(textures);
				st.blushAlpha.assign(active.size(), std::numeric_limits<float>::quiet_NaN());
			}
			const float raceMult = BlushRaceMult(a, s);
			for (size_t i = 0; i < active.size(); ++i) {
				const auto* b = active[i];
				const float alpha = std::clamp(b->max * Ease(st.level, b->start, b->full) * s.intensity * st.shown * st.blushGate * st.flushMult * raceMult, 0.0f, 1.0f);
				if (!std::isnan(st.blushAlpha[i]) && std::abs(alpha - st.blushAlpha[i]) <= 0.01f) continue;
				Papyrus::SetOverlayAlpha(a, st.female, st.blushNodes[i], alpha);
				st.blushAlpha[i] = alpha;
			}
		}

		// Both sexes are tracked; which rows actually paint is decided per table row, so a male
		// body does nothing at all until someone adds male rows with their own textures.
		bool Eligible(RE::Actor* a)
		{
			if (!a || !a->Is3DLoaded() || a->IsDead() || a->IsChild()) return false;
			return a->GetActorBase() != nullptr;
		}

		std::vector<RE::Actor*> Gather(const Snap& s)
		{
			std::vector<RE::Actor*> out;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (s.player && Eligible(player)) out.push_back(player);
			if (!s.npcs) return out;

			// Scene partners always count, even beyond the NPC cap.
			if (auto* t = Scenes::PlayerThread()) {
				for (auto& sl : t->slots) {
					auto* a = sl.Get();
					if (a && a != player && Eligible(a)) out.push_back(a);
				}
			}
			if (s.maxNPCs <= 0) return out;
			struct Cand
			{
				RE::Actor* a;
				float d;
			};
			std::vector<Cand> cands;
			const auto origin = player->GetPosition();
			for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
				auto ptr = h.get();
				auto* a = ptr.get();
				if (!a || a == player || !Eligible(a) || std::ranges::find(out, a) != out.end()) continue;
				const float d = a->GetPosition().GetDistance(origin);
				if (d <= s.radius) cands.push_back({ a, d });
			}
			std::ranges::sort(cands, {}, &Cand::d);
			for (size_t i = 0; i < cands.size() && i < static_cast<size_t>(s.maxNPCs); ++i) out.push_back(cands[i].a);
			return out;
		}

		void ClearActor(ActorState& st)
		{
			if (auto ptr = st.handle.get(); ptr) {
				Papyrus::ClearBodyMorphKeys(ptr.get(), kMorphKey);
				Papyrus::ClearBodyMorphKeys(ptr.get(), kLegacyMorphKey);
				Papyrus::UpdateModelWeight(ptr.get());
				ClearBlush(ptr.get(), st);
			}
		}

		void ClearAllNow()
		{
			std::scoped_lock l(g_stateLock);
			for (auto& [id, st] : g_states) ClearActor(st);
			g_states.clear();
		}

		void RequestArousal(RE::Actor* a, int source)
		{
			if (source != Settings::Arousal::kOSL && source != Settings::Arousal::kSLO) return;
			const RE::FormID id = a->GetFormID();
			auto done = [id](float v) {
				std::scoped_lock l(g_stateLock);
				if (auto it = g_states.find(id); it != g_states.end()) {
					it->second.arousal = std::clamp(v, 0.0f, 100.0f);
					it->second.haveArousal = true;
				}
			};
			if (source == Settings::Arousal::kOSL) Papyrus::CallFloat("OSLArousedNative", "GetArousalNoSideEffects", a, done);
			else Papyrus::CallFloat("SloangNative", "GetArousal", a, done);
		}

		// Target tissue level from every factor; also sets the response multipliers.
		float Target(RE::Actor* a, ActorState& st, const Snap& s, float now, float& riseMult, float& fallMult)
		{
			riseMult = fallMult = 1.0f;
			st.flushMult = 1.0f;
			float target = st.arousal / 100.0f;
			st.why = "arousal";

			if (auto* t = Scenes::ThreadOf(a); t && t->active) {
				if (s.ostim) {
					const float exc = std::clamp(static_cast<float>(Face::Engine::Excitement(a)) / 100.0f, 0.0f, 1.0f);
					if (exc > target) target = exc, st.why = "OStim excitement";
				}
				if (s.factors) {
					if (t->plateau >= 3 && !t->orgasm && target < 0.9f) target = 0.9f, st.why = "edging";
					if (t->afterglow > 0 && target < 0.7f) target = 0.7f, st.why = "afterglow";
				}
			}
			if (s.factors && now < st.climaxUntil) {
				target = 1.0f;
				st.why = "climax";
				riseMult = 0.15f;  // engorgement at orgasm is near-instant
			}
			st.pers = s.personality ? Face::Engine::Archetype(a) : 0;  // (with the personality setting off, everyone responds alike)
			if (s.personality) {
				switch (st.pers) {
				case 1:  // stoic: slow to show, quick to settle, muted flush
					riseMult *= 1.5f, fallMult *= 0.8f;  // not into it: slow to show, and quick to settle (Shape: minimal response, no blush)
					break;
				case 2:  // vocal: quick to engorge
					riseMult *= 0.7f;
					break;
				case 3:  // shy: flushes hard
					st.flushMult = 1.25f;
					break;
				case 4:  // dominant: firm, fast response
					riseMult *= 0.85f;
					break;
				case Face::Engine::Pers::kTimid:  // like shy: flushes hard
					st.flushMult = 1.25f;
					break;
				case Face::Engine::Pers::kSubmissive:  // quick to respond to a rough scene, flushes a little more
					st.flushMult = 1.1f;
					if (auto* th = Scenes::ThreadOf(a); th && th->active && (th->toneRough || th->toneForced)) riseMult *= 0.7f;
					break;
				case Face::Engine::Pers::kWild:  // really enjoys it: engorges quickly and stays aroused
					riseMult *= 0.6f, fallMult *= 1.3f;
					break;
				case Face::Engine::Pers::kCrazed:  // firm, fast response, like dominant
					riseMult *= 0.85f;
					break;
				default: break;
				}
			}
			return std::clamp(target, 0.0f, 1.0f);
		}

		// How much of each response's range (every morph's change, the body blush, the genital bend) shows, 0-1.
		//
		// The old curve gave each morph its maximum once the level passed that morph's own `full` - 0.35 to 0.7 for most of them - and the
		// level was held at 0.9 while edging, so most of the body sat at its extreme for most of a scene, and the orgasm added nothing.
		// Now: the level sets a share between a floor and a ceiling on a logarithmic curve (quick to become apparent, then flattening, so
		// the rest of the build-up does not exaggerate it); a lift from the ceiling towards 0.95 only when the orgasm is within the peak
		// window, estimated from how fast OStim excitement is rising; and the full range only during the orgasm, fading over a few
		// seconds after it. Each morph still waits for its own start level, so the order they come in is as before.
		float Shape(RE::Actor* a, ActorState& st, const Snap& s, float now, float dt)
		{
			st.blushGate = 1.0f;
			st.faceFlush = 0.0f;
			// A stoic is not excited by sex: only purely physiological reactions, minimal, and the blush - the body's and the face's - only in the
			// last seconds before an orgasm and in the orgasm itself, for a few seconds, then back to minimal and no blush. Whatever the shaping setting.
			const bool stoic = st.pers == Face::Engine::Pers::kStoic;
			if (!s.shaped && !stoic) return 1.0f;
			float imminent = 0.0f;
			if (auto* t = Scenes::ThreadOf(a); t && t->active) {
				const float exc = std::clamp(static_cast<float>(Face::Engine::Excitement(a)) / 100.0f, 0.0f, 1.0f);
				if (st.excAt > 0.0f && now > st.excAt) {
					const float span = now - st.excAt;
					// Excitement resets at an orgasm: that is not a falling rate, so the rate never goes below zero.
					const float rate = std::max(0.0f, (exc - st.exc) / span);
					st.excRate += (rate - st.excRate) * (1.0f - std::exp(-span / 3.0f));
				}
				st.exc = exc;
				st.excAt = now;
				// Seconds to the orgasm at the current rate; excitement has to be near the top for it to count at all, and a stalled
				// excitement (edging) is not approaching anything.
				if (exc >= 0.85f && st.excRate > 0.001f) {
					const float untilOrgasm = (1.0f - exc) / st.excRate;
					imminent = std::clamp(1.0f - untilOrgasm / std::max(1.0f, s.peakWindow), 0.0f, 1.0f);
				}
				if (exc >= 0.985f) imminent = 1.0f;
			} else {
				st.exc = st.excAt = st.excRate = 0.0f;
			}
			// Held at the edge by a dominant (or waiting for a partner): the orgasm is not about to come, however full the excitement is.
			if (imminent > 0.0f && Face::Engine::IsClimaxHeld(a)) imminent = 0.0f;
			// The orgasm itself: for the whole hold, or for a stoic the first four seconds of it, then it fades - fast for a stoic.
			constexpr float kStoicOrgasmSeconds = 4.0f;
			const bool inOrgasm = s.factors && now < st.climaxUntil && (!stoic || st.climaxUntil - now > std::max(0.0f, s.climaxHold - kStoicOrgasmSeconds));
			if (inOrgasm) st.boost = 1.0f;
			else if (dt > 0.0f) st.boost *= std::pow(0.5f, dt / (stoic ? 1.5f : 4.0f));
			if (st.boost < 0.01f) st.boost = 0.0f;
			const float level = std::clamp(st.level, 0.0f, 1.0f);
			if (stoic) {
				st.blushGate = std::max(imminent, st.boost);
				st.faceFlush = st.blushGate * s.stoicPeak;
				const float rest = s.stoicRest * std::clamp(level / 0.12f, 0.0f, 1.0f);
				const float top = rest + std::max(0.0f, s.stoicPeak - rest) * imminent;
				return std::max(top, st.boost * s.stoicPeak);
			}
			const float u = std::clamp(level / 0.9f, 0.0f, 1.0f);
			const float curve = std::log1p(9.0f * u) / std::log(10.0f);  // 0.30 at a tenth of the way, 0.62 at a third, 0.85 at two thirds
			float body = s.floorShown + (s.ceilingShown - s.floorShown) * curve;
			body *= std::clamp(level / 0.12f, 0.0f, 1.0f);  // the floor comes in over the first of the arousal, so it does not pop at zero
			const float top = body + (0.95f - body) * imminent;
			return std::max(top, st.boost);
		}
	}

	void Init()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		g_hasOSL = dh->LookupModByName("OSLAroused.esp") != nullptr;
		// OSL Aroused also ships a SexLabAroused.esm stub; SLO NG is identified by its native DLL.
		g_hasSLO = dh->LookupModByName("SexLabAroused.esm") != nullptr && GetModuleHandleW(L"SexlabArousedNG.dll") != nullptr;
		CSimpleIniA skee;
		if (skee.LoadFile("Data/SKSE/Plugins/skee64.ini") >= 0) {
			// skee64.ini has inline "; Default[6]" comments, which GetLongValue rejects.
			g_bodyOverlays = std::atoi(skee.GetValue("Overlays/Body", "iNumOverlays", "6"));
		}
		logger::info("Arousal: OSL Aroused {}, SLO Aroused NG {}, RaceMenu body overlay slots {}", g_hasOSL, g_hasSLO, g_bodyOverlays);
	}

	void OnGameLoad()
	{
		std::scoped_lock l(g_stateLock);
		g_states.clear();
		g_lastTick = 0.0f;
		// Loading a save is when the engine installs 3D for a cell full of actors and every
		// overlay mod piles work onto RaceMenu at once. Nothing of ours is urgent in that window,
		// so stay out of it: the first tick waits, rather than adding to the queue.
		g_nextTick = Scenes::Now() + kLoadGrace;
	}

	void RequestClearAll() { g_clearRequested = true; }

	void OnClimax(RE::Actor* a)
	{
		if (!a) return;
		float hold;
		{
			std::scoped_lock l(Settings::lock);
			hold = S::fClimaxHold;
		}
		std::scoped_lock l(g_stateLock);
		if (auto it = g_states.find(a->GetFormID()); it != g_states.end()) it->second.climaxUntil = Scenes::Now() + hold;
	}

	float Flush(RE::Actor* a)
	{
		if (!a) return 0.0f;
		std::scoped_lock l(g_stateLock);
		auto it = g_states.find(a->GetFormID());
		if (it != g_states.end() && it->second.pers == Face::Engine::Pers::kStoic) return it->second.faceFlush;  // a stoic flushes only at the end and in the orgasm
		return it != g_states.end() ? std::clamp(it->second.level * it->second.flushMult, 0.0f, 1.0f) : 0.0f;
	}

	void Tick()
	{
		const float now = Scenes::Now();
		float interval;
		{
			std::scoped_lock l(Settings::lock);
			interval = S::fInterval;
		}
		if (now < g_nextTick) return;
		g_nextTick = now + interval;

		if (g_clearRequested.exchange(false)) ClearAllNow();

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded()) return;
		const float dt = g_lastTick <= 0.0f ? 0.0f : std::clamp(now - g_lastTick, 0.0f, 5.0f);
		g_lastTick = now;

		const Snap s = CopySettings();
		const int source = ActiveSource();
		const bool haveSignal = source != Settings::Arousal::kAuto || s.ostim;
		if (!s.enabled || !haveSignal || Compat::Disabled(Compat::kArousal)) {
			ClearAllNow();
			return;
		}

		++g_tick;
		std::scoped_lock sl(Scenes::Lock());
		std::scoped_lock l(g_stateLock);
		for (auto* a : Gather(s)) {
			auto& st = g_states[a->GetFormID()];
			const bool fresh = st.lastTick == 0 || st.applied.size() != s.morphs.size();
			if (fresh) {
				if (!st.applied.empty()) {
					Papyrus::ClearBodyMorphKeys(a, kMorphKey);
					Papyrus::ClearBodyMorphKeys(a, kLegacyMorphKey);
				}
				st.handle = a->GetHandle();
				st.name = a->GetDisplayFullName();
				if (auto* base = a->GetActorBase()) st.female = base->GetSex() == RE::SEX::kFemale;
				st.applied.assign(s.morphs.size(), std::numeric_limits<float>::quiet_NaN());
			}
			if (!st.legacyCleared) {
				// Softbody Arousal 2.0 wrote the same sliders under its own key; drop them once.
				Papyrus::ClearBodyMorphKeys(a, kLegacyKey);
				st.legacyCleared = true;
			}
			st.lastTick = g_tick;
			st.lastSeen = now;

			RequestArousal(a, source);  // lands before the next tick

			float riseMult, fallMult;
			st.target = Target(a, st, s, now, riseMult, fallMult);
			// A new (or rebuilt) state starts at the actor's real arousal. Softbody Arousal
			// started every state at zero, so an actor rebuilt after an orgasm 3D reload
			// snapped to rest and crawled back up.
			if (!st.seeded) {
				const bool known = st.haveArousal || source == Settings::Arousal::kAuto;
				if (!known) continue;  // wait for the first reading
				st.level = st.target;
				st.seeded = true;
			}
			const float halfLife = st.target > st.level ? s.rise * riseMult : s.fall * fallMult;
			if (dt > 0.0f) st.level += (st.target - st.level) * (1.0f - std::pow(0.5f, dt / std::max(0.1f, halfLife)));
			if (std::abs(st.target - st.level) < 0.002f) st.level = st.target;
			st.shown = Shape(a, st, s, now, dt);

			ApplyBlush(a, st, s);
			// A male body has no softbody sliders worth driving, but it does have the genital
			// chain. Same level, applied as bones in the Body module. The enable comes from the
			// tick's settings snapshot: this runs under the state lock, and taking Settings::lock
			// here would invert the project's Scenes -> Settings -> module lock order.
			if (!st.female && s.genitals) Body::SetGenitalResponse(a, st.level * st.shown * std::clamp(s.intensity, 0.0f, 1.0f));

			bool changed = false;
			for (size_t i = 0; i < s.morphs.size(); ++i) {
				const auto& m = s.morphs[i];
				const bool applies = m.enabled && SexMatch(m.sex, st.female);
				const float prev = st.applied[i];
				if (!applies && std::isnan(prev)) {
					st.applied[i] = 0.0f;  // never set on this body: nothing to clear
					continue;
				}
				const float v = applies ? m.rest + (m.max - m.rest) * Ease(st.level, m.start, m.full) * s.intensity * st.shown : 0.0f;
				if (!std::isnan(prev) && std::abs(v - prev) <= 0.005f) continue;
				if (v == 0.0f) Papyrus::ClearBodyMorph(a, m.name, kMorphKey);
				else Papyrus::SetBodyMorph(a, m.name, kMorphKey, v);
				st.applied[i] = v;
				changed = true;
			}
			if (changed) {
				Papyrus::UpdateModelWeight(a);
				logger::debug("{:08X} {} arousal={:.0f} target={:.2f} ({}) level={:.3f} shown={:.2f}", a->GetFormID(), st.name, st.arousal, st.target, st.why, st.level, st.shown);
			}
		}
		// Anyone not processed this tick keeps their state through the grace period, then
		// returns to rest. Dead or deleted actors clear at once.
		std::erase_if(g_states, [now](auto& kv) {
			auto& st = kv.second;
			if (st.lastTick == g_tick) return false;
			auto ptr = st.handle.get();
			const bool gone = !ptr || ptr->IsDead() || ptr->IsDeleted();
			if (!gone && now - st.lastSeen < kGraceSeconds) return false;
			ClearActor(st);
			return true;
		});
	}

	int BodyOverlaySlots() { return g_bodyOverlays; }
	bool HasOSL() { return g_hasOSL; }
	bool HasSLO() { return g_hasSLO; }

	int ActiveSource()
	{
		int pref;
		{
			std::scoped_lock l(Settings::lock);
			pref = S::iSource;
		}
		if (pref == Settings::Arousal::kOStimOnly) return Settings::Arousal::kAuto;
		if (pref == Settings::Arousal::kOSL && g_hasOSL) return Settings::Arousal::kOSL;
		if (pref == Settings::Arousal::kSLO && g_hasSLO) return Settings::Arousal::kSLO;
		if (g_hasOSL) return Settings::Arousal::kOSL;
		if (g_hasSLO) return Settings::Arousal::kSLO;
		return Settings::Arousal::kAuto;
	}

	std::vector<StatusRow> Snapshot()
	{
		std::scoped_lock l(g_stateLock);
		std::vector<StatusRow> rows;
		for (auto& [id, st] : g_states) rows.push_back({ st.name, st.arousal, st.target, st.level, st.shown, st.why });
		return rows;
	}
}
