// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Face/Output.h"

#include "Scenes.h"

namespace Face::Output
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		struct Channel
		{
			float cur = 0.0f;
			float target = 0.0f;
			float speed = 0.5f;
			bool used = false;  // written at least once; unused channels are left to the game

			void Set(float v, float s)
			{
				target = std::clamp(v, 0.0f, 1.0f);
				speed = s;
				used = true;
			}

			void Step(float dt)
			{
				if (speed <= 0.02f) {
					cur = target;
					return;
				}
				// Reaches ~95% of the way in `speed` seconds, like Mfg's smooth writes.
				const float k = 1.0f - std::exp(-dt * 3.0f / speed);
				cur += (target - cur) * k;
				if (std::abs(target - cur) < 0.002f) cur = target;
			}
		};

		struct State
		{
			std::array<Channel, kPhonemes> ph{};
			std::array<Channel, kModifiers> mod{};
			std::array<Channel, kExpressions> expr{};
			bool mouthOwned = true;
			bool reseedMouth = true;   // copy live phonemes before the first write
			float mouthFloor = 0.0f;   // a tongue is out: never close the jaw past this
			bool suspended = false;    // another mod owns this face; write nothing
			bool reseedAll = true;
			bool releasing = false;
			bool exprUsed = false;
			bool reported = false;     // logged the first write

			// lip-sync track
			std::shared_ptr<const Envelope> track;
			float trackStart = 0.0f;
			TrackParams trackParams;
			float trackOpen = 0.0f;     // smoothed loudness
			float trackBright = 0.0f;
			float trackBlend = 0.0f;    // 0 = base mouth, 1 = track mouth

			[[nodiscard]] bool TrackActive(float now) const { return track && now - trackStart < track->Duration(); }
		};

		std::mutex g_lock;
		std::unordered_map<RE::FormID, State> g_states;
		std::atomic<std::size_t> g_count{ 0 };

		State* Get(RE::Actor* a, bool create)
		{
			if (!a) return nullptr;
			auto it = g_states.find(a->GetFormID());
			if (it != g_states.end()) {
				if (create) it->second.releasing = false;
				return &it->second;
			}
			if (!create) return nullptr;
			auto& st = g_states[a->GetFormID()];
			g_count = g_states.size();
			return &st;
		}

		bool IsLook(int id) { return id >= kLookDown && id <= kLookUp; }

		void Seed(RE::BSFaceGenKeyframeMultiple& kf, auto& channels)
		{
			for (std::uint32_t i = 0; i < channels.size() && kf.values && i < kf.count; ++i) channels[i].cur = kf.values[i];
		}

		void Write(RE::BSFaceGenKeyframeMultiple& kf, std::uint32_t i, float v)
		{
			if (kf.values && i < kf.count) kf.SetValue(i, v);
		}
	}

	void SetPhoneme(RE::Actor* a, int id, float v, float speed)
	{
		if (id < 0 || id >= kPhonemes) return;
		std::scoped_lock l(g_lock);
		if (auto* st = Get(a, true)) st->ph[id].Set(v, speed);
	}

	void SetModifier(RE::Actor* a, int id, float v, float speed)
	{
		if (id < kBrowDownL || id >= kModifiers) return;  // never blink
		std::scoped_lock l(g_lock);
		if (auto* st = Get(a, true)) st->mod[id].Set(v, speed);
	}

	void SetMood(RE::Actor* a, int mood, float strength, float speed)
	{
		if (mood < 0 || mood >= kExpressions) return;
		std::scoped_lock l(g_lock);
		auto* st = Get(a, true);
		if (!st) return;
		for (int i = 0; i < kExpressions; ++i) st->expr[i].Set(i == mood ? strength : 0.0f, speed);
		st->exprUsed = true;
	}

	void ResetPhonemes(RE::Actor* a, float speed)
	{
		std::scoped_lock l(g_lock);
		if (auto* st = Get(a, true)) {
			for (auto& c : st->ph) c.Set(0.0f, speed);
		}
	}

	void ResetModifiers(RE::Actor* a, float speed)
	{
		std::scoped_lock l(g_lock);
		if (auto* st = Get(a, true)) {
			for (int i = kBrowDownL; i < kModifiers; ++i) st->mod[i].Set(0.0f, speed);
		}
	}

	void ApplyPreset(RE::Actor* a, const std::array<float, 32>& e, bool skipPhonemes, float exprStr, float modStr, float phStr, float speed)
	{
		std::scoped_lock l(g_lock);
		auto* st = Get(a, true);
		if (!st) return;
		if (!skipPhonemes) {
			for (int i = 0; i < kPhonemes; ++i) st->ph[i].Set(e[i] * phStr, speed);
		}
		for (int i = kBrowDownL; i < kModifiers; ++i) {
			const float v = e[16 + i] * modStr;
			if (IsLook(i) && v <= 0.0f && !st->mod[i].used) continue;
			st->mod[i].Set(v, speed);
		}
		const int mood = static_cast<int>(e[30]);
		if (mood >= 0 && mood < kExpressions) {
			for (int i = 0; i < kExpressions; ++i) st->expr[i].Set(i == mood ? e[31] * exprStr : 0.0f, speed);
			st->exprUsed = true;
		}
	}

	void SetSuspended(RE::Actor* a, bool suspended)
	{
		std::scoped_lock l(g_lock);
		auto* st = Get(a, false);
		if (!st || st->suspended == suspended) return;
		st->suspended = suspended;
		// Coming back: the other mod left its own values in the face data, so re-read them
		// before blending, or the first frame snaps.
		if (!suspended) st->reseedAll = true;
	}

	bool IsSuspended(RE::Actor* a)
	{
		std::scoped_lock l(g_lock);
		auto* st = Get(a, false);
		return st && st->suspended;
	}

	void SetMouthFloor(RE::Actor* a, float floor)
	{
		std::scoped_lock l(g_lock);
		auto* st = Get(a, floor > 0.0f);  // only worth a new state when there is a floor to hold
		if (st) st->mouthFloor = std::clamp(floor, 0.0f, 1.0f);
	}

	void SetMouthOwned(RE::Actor* a, bool owned)
	{
		std::scoped_lock l(g_lock);
		auto* st = Get(a, false);
		if (!st || st->mouthOwned == owned) return;
		st->mouthOwned = owned;
		if (owned) st->reseedMouth = true;
	}

	void SetMouthTrack(RE::Actor* a, std::shared_ptr<const Envelope> env, float start, const TrackParams& params)
	{
		if (!env || env->loud.empty()) return;
		std::scoped_lock l(g_lock);
		auto* st = Get(a, true);
		if (!st) return;
		if (st->track != env || std::abs(st->trackStart - start) > 0.05f) {
			st->track = std::move(env);
			st->trackStart = start;
		}
		st->trackParams = params;
	}

	void ClearMouthTrack(RE::Actor* a)
	{
		std::scoped_lock l(g_lock);
		if (auto* st = Get(a, false)) st->track.reset();
	}

	bool HasMouthOverride(RE::Actor* a)
	{
		std::scoped_lock l(g_lock);
		auto* st = Get(a, false);
		return st && st->TrackActive(Scenes::Now());
	}

	void Release(RE::Actor* a, float speed)
	{
		std::scoped_lock l(g_lock);
		auto* st = Get(a, false);
		if (!st) return;
		for (auto& c : st->ph) c.Set(0.0f, speed);
		for (auto& c : st->mod) {
			if (c.used) c.Set(0.0f, speed);
		}
		for (auto& c : st->expr) c.Set(0.0f, speed);
		st->track.reset();
		st->mouthFloor = 0.0f;
		st->suspended = false;
		st->mouthOwned = true;
		st->releasing = true;
	}

	void ReleaseAll(float speed)
	{
		std::vector<RE::FormID> ids;
		{
			std::scoped_lock l(g_lock);
			for (auto& [id, st] : g_states) ids.push_back(id);
		}
		for (auto id : ids) {
			if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id)) Release(a, speed);
			else Forget(id);
		}
	}

	void Forget(RE::FormID id)
	{
		std::scoped_lock l(g_lock);
		g_states.erase(id);
		g_count = g_states.size();
	}

	void ForgetAll()
	{
		std::scoped_lock l(g_lock);
		g_states.clear();
		g_count = 0;
	}

	bool IsPainted(RE::Actor* a)
	{
		std::scoped_lock l(g_lock);
		auto* st = Get(a, false);
		return st && !st->releasing;
	}

	std::size_t PaintedCount() { return g_count.load(); }

	void Update(RE::Actor* a, float dt)
	{
		if (g_count.load(std::memory_order_relaxed) == 0 || !a) return;

		std::scoped_lock l(g_lock);
		auto it = g_states.find(a->GetFormID());
		if (it == g_states.end()) return;
		auto& st = it->second;

		// Another mod owns this face: write nothing of our own, with one exception. While a tongue
		// is out the jaw clearance still goes on, because it can only open the mouth wider than
		// they asked for, never close it - and a mod that puts the tongue out without opening the
		// mouth itself would otherwise leave it clipping straight through the lips.
		if (st.suspended) {
			if (st.mouthFloor > 0.0f) {
				if (auto* sfg = a->GetFaceGenAnimationData()) {
					RE::BSSpinLockGuard guard(sfg->lock);
					auto& kf = sfg->phenomeKeyFrame;
					const auto live = [&](std::uint32_t i) { return kf.values && i < kf.count ? kf.values[i] : 1.0f; };
					if (live(kBigAah) < st.mouthFloor) Write(kf, kBigAah, st.mouthFloor);
					if (live(kAah) < st.mouthFloor * 0.35f) Write(kf, kAah, st.mouthFloor * 0.35f);
					if (live(kBMP) > 0.0f) Write(kf, kBMP, 0.0f);
				}
			}
			return;
		}
		auto* fg = a->GetFaceGenAnimationData();
		if (!fg) return;
		dt = std::clamp(dt, 0.0f, 0.1f);
		if (!st.reported) {
			st.reported = true;
			logger::info("Face: writing {:08X} {} every animation update", a->GetFormID(), a->GetDisplayFullName());
		}

		RE::BSSpinLockGuard guard(fg->lock);
		if (st.reseedAll) {
			Seed(fg->phenomeKeyFrame, st.ph);
			Seed(fg->modifierKeyFrame, st.mod);
			Seed(fg->expressionKeyFrame, st.expr);
			st.reseedAll = false;
			st.reseedMouth = false;
		} else if (st.reseedMouth) {
			Seed(fg->phenomeKeyFrame, st.ph);
			st.reseedMouth = false;
		}

		bool settled = true;

		// Lip-sync track: follow the moan's loudness envelope, blending in and out of the
		// base mouth so the hand-over never pops.
		const float now = Scenes::Now();
		std::array<float, kPhonemes> trackPh{};
		float squintBoost = 0.0f;
		const bool trackOn = st.TrackActive(now);
		if (st.track) {
			const auto& env = *st.track;
			const auto& p = st.trackParams;
			float loud = 0.0f;
			float bright = 0.0f;
			if (trackOn) {
				const auto frame = std::min(static_cast<std::size_t>((now - st.trackStart) / env.frameSeconds), env.loud.size() - 1);
				loud = env.loud[frame];
				bright = env.bright.empty() ? 0.0f : env.bright[frame];
			}
			const float tau = loud > st.trackOpen ? p.attack : p.release;
			const float k = 1.0f - std::exp(-dt / std::max(0.005f, tau));
			st.trackOpen += (loud - st.trackOpen) * k;
			st.trackBright += (bright - st.trackBright) * k;
			const float open = std::clamp(st.trackOpen * p.gain, std::min(p.minOpen, p.maxOpen), p.maxOpen);
			trackPh[kBigAah] = open * (0.55f + 0.45f * (1.0f - st.trackBright));
			trackPh[kAah] = open * 0.35f;
			trackPh[kOh] = open * 0.40f * (1.0f - st.trackBright);
			trackPh[kEee] = open * 0.50f * st.trackBright;
			if (p.holdEyes) squintBoost = open * 0.25f;
			if (!trackOn && st.trackOpen < 0.01f) st.track.reset();
		}
		const float blendTarget = trackOn ? 1.0f : 0.0f;
		st.trackBlend += (blendTarget - st.trackBlend) * (1.0f - std::exp(-dt / 0.08f));
		if (std::abs(blendTarget - st.trackBlend) < 0.01f) st.trackBlend = blendTarget;

		for (int i = 0; i < kPhonemes; ++i) {
			auto& c = st.ph[i];
			c.Step(dt);
			settled &= c.cur <= 0.005f;
			const bool baseWrites = st.mouthOwned && c.used;
			bool write = true;
			float v;
			if (st.trackBlend > 0.0f) {
				const float base = baseWrites ? c.cur : 0.0f;
				v = base + (trackPh[i] - base) * st.trackBlend;
			} else if (baseWrites) {
				v = c.cur;
			} else {
				v = 0.0f;
				write = false;
			}
			// A tongue is out. Whoever is driving the mouth, the jaw stays open and the lips stay
			// apart, or the tongue is pushed straight through them.
			if (st.mouthFloor > 0.0f) {
				// The jaw drop is BigAah with a little Aah behind it, the same shape the lip-sync
				// track uses to open a mouth; BigAah alone drops the jaw without parting the lips
				// much, which is what left a tongue resting on the bottom lip.
				if (i == kBigAah) {
					v = std::max(v, st.mouthFloor);
					write = true;
				} else if (i == kAah) {
					v = std::max(v, st.mouthFloor * 0.35f);
					write = true;
				} else if (i == kBMP) {
					v = 0.0f;
					write = true;
				} else if (i == kOh || i == kEee) {
					// Pursing or stretching the lips closes the gap again. Give way to the floor.
					v = std::min(v, 1.0f - st.mouthFloor);
					write = write || v > 0.0f;
				}
			}
			if (write) Write(fg->phenomeKeyFrame, i, v);
		}
		settled &= st.trackBlend <= 0.0f && st.mouthFloor <= 0.0f;
		for (int i = kBrowDownL; i < kModifiers; ++i) {
			auto& c = st.mod[i];
			if (!c.used) continue;
			c.Step(dt);
			settled &= c.cur <= 0.005f;
			const bool squint = i == kSquintL || i == kSquintR;
			Write(fg->modifierKeyFrame, i, squint ? std::min(1.0f, c.cur + squintBoost * st.trackBlend) : c.cur);
			// Look modifiers stop eye blinking while non-zero; hand them back once at rest.
			if (IsLook(i) && c.cur <= 0.0f && c.target <= 0.0f) c.used = false;
		}
		if (st.exprUsed) {
			for (int i = 0; i < kExpressions; ++i) {
				auto& c = st.expr[i];
				c.Step(dt);
				settled &= c.cur <= 0.005f;
				Write(fg->expressionKeyFrame, i, c.cur);
			}
			fg->exprOverride = true;
		}

		if (st.releasing && settled) {
			for (int i = 0; i < kPhonemes; ++i) Write(fg->phenomeKeyFrame, i, 0.0f);
			for (int i = kBrowDownL; i < kModifiers; ++i) {
				if (st.mod[i].used) Write(fg->modifierKeyFrame, i, 0.0f);
			}
			if (st.exprUsed) {
				for (int i = 0; i < kExpressions; ++i) Write(fg->expressionKeyFrame, i, 0.0f);
				fg->ClearExpressionOverride();
			}
			g_states.erase(it);
			g_count = g_states.size();
		}
	}

	void UpdateNPCs(float dt)
	{
		if (g_count.load(std::memory_order_relaxed) == 0) return;
		std::vector<RE::FormID> ids;
		{
			std::scoped_lock l(g_lock);
			ids.reserve(g_states.size());
			for (const auto& kv : g_states) ids.push_back(kv.first);
		}
		for (const auto id : ids) {
			auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
			if (a && !a->IsPlayerRef()) Update(a, dt);
		}
	}
}
