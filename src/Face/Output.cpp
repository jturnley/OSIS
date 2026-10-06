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

			// Layered writing, for when OStim's own face writer is on and the face is shared. `last` is
			// what we wrote here, `theirs` what was there before that or whatever replaced it since.
			float last = 0.0f;
			float theirs = 0.0f;
			bool engaged = false;  // written by us and not yet handed back
			float mine = 0.0f;      // our part of what we wrote, before taking the larger with `theirs`

			void See(float found)
			{
				// A value that is not the one we left was written by someone else. Our own write is
				// not theirs, so it is not taken as their value: that would pin our boost in place.
				if (!engaged || std::abs(found - last) > 0.004f) theirs = found;
			}

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

		// The rendered face frame to frame, for one group of channels (mouth, eyes and brows, mood).
		// The total jumping up and down is flutter; one channel dropping hard in a single frame is a
		// snap, and is logged with the keyframe and our own last write beside it so it can be attributed.
		struct Series
		{
			std::array<float, 17> fin{};
			float sum = 0.0f;
			float lastDelta = 0.0f;
			bool have = false;
			int jumps = 0, reversals = 0, snaps = 0;
		};

		// What one actor's face did between two probe lines. "Ours" is what we wrote last frame,
		// compared at the start of the next frame against what is in the face data by then.
		struct Probe
		{
			std::array<float, kPhonemes> ph{};
			std::array<float, kModifiers> mod{};
			std::array<float, kExpressions> expr{};
			std::uint32_t phMask = 0;
			std::uint32_t modMask = 0;
			bool exprWritten = false;
			bool valid = false;  // the arrays hold last frame's writes
			int frames = 0;
			int hitPh = 0, hitMod = 0, hitExpr = 0, overrideOff = 0;
			float dPh = 0.0f, dMod = 0.0f, dExpr = 0.0f;
			int chPh = -1, chMod = -1, chExpr = -1;
			float foundPh = 0.0f, oursPh = 0.0f, foundMod = 0.0f, oursMod = 0.0f, foundExpr = 0.0f, oursExpr = 0.0f;
			float dlgPh = 0.0f, dlgMod = 0.0f;  // dialogue lip-sync, the game's own second mouth
			int trackStarts = 0;
			float next = 0.0f;
			bool countsLogged = false;
			std::array<Series, 3> ser{};  // mouth, eyes and brows, mood
			int snapsLogged = 0;
			int movedInside = 0, preChecked = 0;
			int restores = 0;       // times an outside change to an owned face was put back before render
			float restoreWorst = 0.0f;
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
			bool layered = false;      // OStim's writer is on: add to its face, never write over it
			const RE::BSFaceGenAnimationData* fg = nullptr;  // for finding this state from the face node hook
			bool reseedAll = true;
			bool releasing = false;
			bool exprUsed = false;
			bool exprRelease = false;  // easing the mood back to zero, then stop writing it
			bool reported = false;     // logged the first write
			Probe probe;

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

		std::atomic<float> g_probeArm{ 0.0f };    // seconds requested; the run starts at the next write
		std::atomic<float> g_probeUntil{ 0.0f };
		std::atomic<bool> g_probeDone{ true };

		float Val(const RE::BSFaceGenKeyframeMultiple& kf, std::uint32_t i) { return kf.values && i < kf.count ? kf.values[i] : 0.0f; }

		int Top(const RE::BSFaceGenKeyframeMultiple& kf, std::uint32_t n, float& v)
		{
			int best = -1;
			v = 0.0f;
			for (std::uint32_t i = 0; i < n; ++i) {
				if (const float x = Val(kf, i); x > v) {
					v = x;
					best = static_cast<int>(i);
				}
			}
			return best;
		}

		template <std::size_t N>
		int Top(const std::array<float, N>& a, float& v)
		{
			int best = -1;
			v = 0.0f;
			for (std::size_t i = 0; i < N; ++i) {
				if (a[i] > v) {
					v = a[i];
					best = static_cast<int>(i);
				}
			}
			return best;
		}

		float FinalSum(const RE::BSFaceGenAnimationData& fg)
		{
			float s = 0.0f;
			for (std::uint32_t i = 0; i < kPhonemes; ++i) s += Val(fg.phoneme3, i);
			for (std::uint32_t i = kBrowDownL; i < kModifiers; ++i) s += Val(fg.modifier3, i);
			return s;
		}

		void ScanGroup(RE::Actor* a, Probe& p, Series& s, const char* name, const RE::BSFaceGenKeyframeMultiple& fin,
				const RE::BSFaceGenKeyframeMultiple& key, std::uint32_t first, std::uint32_t end, const float* ours, std::uint32_t mask)
		{
			std::array<float, 17> now{};
			float sum = 0.0f;
			for (std::uint32_t i = first; i < end && i < 17; ++i) {
				now[i] = Val(fin, i);
				sum += now[i];
			}
			if (s.have) {
				const float d = sum - s.sum;
				if (std::abs(d) > 0.15f) {
					++s.jumps;
					if (s.lastDelta != 0.0f && (d > 0.0f) != (s.lastDelta > 0.0f)) ++s.reversals;
					s.lastDelta = d;
				}
				for (std::uint32_t i = first; i < end && i < 17; ++i) {
					if (s.fin[i] - now[i] <= 0.20f || p.snapsLogged >= 80) continue;
					++p.snapsLogged;
					++s.snaps;
					logger::info("Probe {:08X} {}: SNAP {} #{} {:.2f} -> {:.2f} in one frame; keyframe now {:.2f}; we wrote {}", a->GetFormID(),
						a->GetDisplayFullName(), name, i, s.fin[i], now[i], Val(key, i),
						(ours && (mask & (1u << i))) ? std::format("{:.2f}", ours[i]) : std::string("nothing last frame"));
				}
			}
			s.fin = now;
			s.sum = sum;
			s.have = true;
		}

		// Start of a frame: did anything change what we wrote last frame?
		void ProbeEntry(RE::Actor* a, Probe& p, const RE::BSFaceGenAnimationData& fg)
		{
			++p.frames;
			ScanGroup(a, p, p.ser[0], "mouth", fg.phoneme3, fg.phenomeKeyFrame, 0, kPhonemes, p.ph.data(), p.valid ? p.phMask : 0u);
			ScanGroup(a, p, p.ser[1], "eyes/brows", fg.modifier3, fg.modifierKeyFrame, kBrowDownL, kModifiers, p.mod.data(), p.valid ? p.modMask : 0u);
			ScanGroup(a, p, p.ser[2], "mood", fg.expression3, fg.expressionKeyFrame, 0, kExpressions, p.expr.data(),
					p.valid && p.exprWritten ? 0x1FFFFu : 0u);
			if (p.exprWritten && !fg.exprOverride) ++p.overrideOff;
			for (std::uint32_t i = 0; i < kPhonemes; ++i) p.dlgPh = std::max(p.dlgPh, Val(fg.phoneme1, i));
			for (std::uint32_t i = kBrowDownL; i < kModifiers; ++i) p.dlgMod = std::max(p.dlgMod, Val(fg.modifier1, i));
			if (!p.valid) return;
			const auto check = [](const RE::BSFaceGenKeyframeMultiple& kf, const auto& ours, std::uint32_t mask, bool all,
								   int& hits, float& d, int& ch, float& found, float& o) {
				bool hit = false;
				for (std::uint32_t i = 0; i < ours.size(); ++i) {
					if (!all && !(mask & (1u << i))) continue;
					const float f = Val(kf, i);
					const float delta = std::abs(f - ours[i]);
					if (delta <= 0.01f) continue;
					hit = true;
					if (delta > d) {
						d = delta;
						ch = static_cast<int>(i);
						found = f;
						o = ours[i];
					}
				}
				if (hit) ++hits;
			};
			check(fg.phenomeKeyFrame, p.ph, p.phMask, false, p.hitPh, p.dPh, p.chPh, p.foundPh, p.oursPh);
			check(fg.modifierKeyFrame, p.mod, p.modMask, false, p.hitMod, p.dMod, p.chMod, p.foundMod, p.oursMod);
			if (p.exprWritten) check(fg.expressionKeyFrame, p.expr, 0, true, p.hitExpr, p.dExpr, p.chExpr, p.foundExpr, p.oursExpr);
		}

		// Once a second: our last writes against what the game rendered from them. The game's
		// final values (expression3 / modifier3 / phoneme3) are what reaches the face mesh.
		void ProbeLog(RE::Actor* a, Probe& p, const RE::BSFaceGenAnimationData& fg)
		{
			if (!p.countsLogged) {
				p.countsLogged = true;
				logger::info("Probe {:08X} {}: keyframe sizes expr {}/{} mod {}/{} mouth {}/{} (input/rendered), transition target {}",
					a->GetFormID(), a->GetDisplayFullName(), fg.expressionKeyFrame.count, fg.expression3.count, fg.modifierKeyFrame.count,
					fg.modifier3.count, fg.phenomeKeyFrame.count, fg.phoneme3.count, fg.transitionTargetKeyFrame ? "present" : "none");
			}
			float ov, rv, opv, rpv;
			const int om = p.exprWritten ? Top(p.expr, ov) : -1;
			const int rm = Top(fg.expression3, kExpressions, rv);
			int op = -1;
			opv = 0.0f;
			for (int i = 0; i < kPhonemes; ++i) {
				if ((p.phMask & (1u << i)) && p.ph[i] > opv) {
					opv = p.ph[i];
					op = i;
				}
			}
			const int rp = Top(fg.phoneme3, kPhonemes, rpv);
			const auto m = [&](int i) { return (p.modMask & (1u << i)) ? p.mod[i] : -1.0f; };
			logger::info(
				"Probe {:08X} {}: mood ours {}@{:.2f} rendered {}@{:.2f} | brows ours dn {:.2f} in {:.2f} up {:.2f} sq {:.2f}, rendered dn {:.2f} in {:.2f} up {:.2f} sq {:.2f}, blink ours {:.2f} rendered {:.2f} | "
				"mouth ours {}@{:.2f} rendered {}@{:.2f}, dialogue lip-sync max {:.2f} | changed by something else between our writes: mood {}/{} brows {}/{} mouth {}/{} frames, "
				"override found off {} | lip-sync clips started {}",
				a->GetFormID(), a->GetDisplayFullName(), om, ov, rm, rv, m(kBrowDownL), m(kBrowInL), m(kBrowUpL), m(kSquintL),
				Val(fg.modifier3, kBrowDownL), Val(fg.modifier3, kBrowInL), Val(fg.modifier3, kBrowUpL), Val(fg.modifier3, kSquintL),
				m(kBlinkL), Val(fg.modifier3, kBlinkL),
				op, opv, rp, rpv, p.dlgPh, p.hitExpr, p.frames, p.hitMod, p.frames, p.hitPh, p.frames, p.overrideOff, p.trackStarts);
			if (p.chExpr >= 0 || p.chMod >= 0 || p.chPh >= 0) {
				logger::info("Probe {:08X}: largest outside changes: mood #{} {:.2f} where we wrote {:.2f}; modifier #{} {:.2f} where we wrote {:.2f}; phoneme #{} {:.2f} where we wrote {:.2f}",
					a->GetFormID(), p.chExpr, p.foundExpr, p.oursExpr, p.chMod, p.foundMod, p.oursMod, p.chPh, p.foundPh, p.oursPh);
			}
			logger::info("Probe {:08X}: rendered face frame to frame - mouth {} jumps / {} reversals / {} snaps, eyes+brows {} / {} / {}, mood {} / {} / {} ({} frames); "
				"the game's final values changed inside the face node update in {} of {} frames; "
				"outside changes to our face put back before render: {} (largest {:.2f})",
				a->GetFormID(), p.ser[0].jumps, p.ser[0].reversals, p.ser[0].snaps, p.ser[1].jumps, p.ser[1].reversals, p.ser[1].snaps,
				p.ser[2].jumps, p.ser[2].reversals, p.ser[2].snaps, p.frames, p.movedInside, p.preChecked, p.restores, p.restoreWorst);
			const auto keep = p;
			p = Probe{};
			for (std::size_t g = 0; g < keep.ser.size(); ++g) {
				p.ser[g].fin = keep.ser[g].fin;
				p.ser[g].sum = keep.ser[g].sum;
				p.ser[g].have = keep.ser[g].have;
			}
			p.snapsLogged = keep.snapsLogged;
			p.ph = keep.ph;
			p.mod = keep.mod;
			p.expr = keep.expr;
			p.phMask = keep.phMask;
			p.modMask = keep.modMask;
			p.exprWritten = keep.exprWritten;
			p.valid = keep.valid;
			p.countsLogged = true;
		}

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

		// The eyelids close through Blink, which the engine animates on its own: a blink channel is written only while a face
		// holds the lids shut (or part shut), and handed back once it is at rest, so the actor goes on blinking naturally
		// the rest of the time. Look is handled the same way, and for the same reason.
		bool IsBlink(int id) { return id == kBlinkL || id == kBlinkR; }
		bool IsGated(int id) { return IsLook(id) || IsBlink(id); }

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
		st->exprRelease = false;
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
			for (int i = kBlinkL; i < kModifiers; ++i) {
				if (IsGated(i) && !st->mod[i].used) continue;
				st->mod[i].Set(0.0f, speed);
			}
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
		for (int i = kBlinkL; i < kModifiers; ++i) {
			// The lids close as far as the face says: the strength setting and the personality scale how much of an expression
			// shows, and a face that shuts the eyes should shut them (hard squeezed is the whole range's top, not a share of it).
			const float v = e[16 + i] * (IsBlink(i) ? 1.0f : modStr);
			if (IsGated(i) && v <= 0.0f && !st->mod[i].used) continue;
			// Lids close and open quickly whatever the pose's own ease: at the pose's 0.8 s a short clip's hard squeeze reached only about
			// 0.8 of shut before the clip began to open again (1.0 written, 0.79 rendered in the 2.0.3 test).
			st->mod[i].Set(v, IsBlink(i) ? std::min(speed, 0.35f) : speed);
		}
		const int mood = static_cast<int>(e[30]);
		if (mood >= 0 && mood < kExpressions) {
			for (int i = 0; i < kExpressions; ++i) st->expr[i].Set(i == mood ? e[31] * exprStr : 0.0f, speed);
			st->exprUsed = true;
			st->exprRelease = false;
		}
		if (Scenes::Now() < g_probeUntil.load()) {
			logger::info("Probe {:08X} {}: preset mood {}@{:.2f} brows dn {:.2f} in {:.2f} up {:.2f} squint {:.2f} look down {:.2f} up {:.2f}, mouth {} (strengths expr {:.2f} mod {:.2f})",
				a->GetFormID(), a->GetDisplayFullName(), mood, e[31] * exprStr, e[16 + kBrowDownL] * modStr, e[16 + kBrowInL] * modStr,
				e[16 + kBrowUpL] * modStr, e[16 + kSquintL] * modStr, e[16 + kLookDown] * modStr, e[16 + kLookUp] * modStr,
				skipPhonemes ? "left alone" : "set", exprStr, modStr);
		}
	}

	void ReleaseMood(RE::Actor* a, float speed)
	{
		std::scoped_lock l(g_lock);
		auto* st = Get(a, false);
		if (!st || !st->exprUsed || st->exprRelease) return;
		for (auto& c : st->expr) c.Set(0.0f, speed);
		st->exprRelease = true;
	}

	void SetLayered(RE::Actor* a, bool layered)
	{
		std::scoped_lock l(g_lock);
		auto* st = Get(a, true);
		if (st) st->layered = layered;
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
			if (Scenes::Now() < g_probeUntil.load()) {
				++st->probe.trackStarts;
				if (st->track == env) {
					logger::info("Probe {:08X} {}: the same lip-sync clip restarted, start moved {:+.2f} s", a->GetFormID(), a->GetDisplayFullName(), start - st->trackStart);
				} else {
					logger::info("Probe {:08X} {}: lip-sync clip started ({:.2f} s long, {:.2f} s in){}", a->GetFormID(), a->GetDisplayFullName(),
						env->Duration(), Scenes::Now() - start, st->TrackActive(Scenes::Now()) ? " while the previous clip was still playing" : "");
				}
			}
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

	bool ReadPhonemes(RE::Actor* a, std::array<float, kPhonemes>& out)
	{
		auto* fg = a ? a->GetFaceGenAnimationData() : nullptr;
		if (!fg) return false;
		for (std::uint32_t i = 0; i < kPhonemes; ++i) out[i] = Val(fg->phoneme3, i);
		return true;
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

	void ArmProbe(float seconds)
	{
		g_probeUntil = 0.0f;
		g_probeDone = false;
		g_probeArm = std::max(1.0f, seconds);
		logger::info("Face probe: armed for {:.0f} s; starts with the next face write ({} face(s) being written)", seconds, g_count.load());
	}

	bool Probing() { return Scenes::Now() < g_probeUntil.load() || g_probeArm.load() > 0.0f; }

	void Update(RE::Actor* a, float dt)
	{
		if (g_count.load(std::memory_order_relaxed) == 0 || !a) return;

		std::scoped_lock l(g_lock);
		auto it = g_states.find(a->GetFormID());
		if (it == g_states.end()) return;
		auto& st = it->second;

		if (st.suspended) return;  // another mod owns this face
		auto* fg = a->GetFaceGenAnimationData();
		if (!fg) return;
		dt = std::clamp(dt, 0.0f, 0.1f);
		if (!st.reported) {
			st.reported = true;
			logger::info("Face: writing {:08X} {} every animation update", a->GetFormID(), a->GetDisplayFullName());
		}

		st.fg = fg;
		RE::BSSpinLockGuard guard(fg->lock);
		const float now = Scenes::Now();
		if (const float arm = g_probeArm.exchange(0.0f); arm > 0.0f) {
			g_probeUntil = now + arm;
			logger::info("Face probe: running for {:.0f} s", arm);
		}
		const bool probing = now < g_probeUntil.load();
		auto& pr = st.probe;
		if (probing) {
			ProbeEntry(a, pr, *fg);
			if (pr.next <= 0.0f) {
				pr.next = now + 1.0f;
			} else if (now >= pr.next) {
				ProbeLog(a, pr, *fg);
				pr.next = now + 1.0f;
			}
			pr.phMask = pr.modMask = 0;
			pr.exprWritten = false;
		} else {
			if (pr.valid || pr.frames) pr = Probe{};
			if (!g_probeDone.exchange(true)) logger::info("Face probe: done");
		}

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
			if (st.layered) c.See(Val(fg->phenomeKeyFrame, i));
			const bool baseWrites = st.mouthOwned && c.used;
			bool write = true;
			float v;
			if (st.trackBlend > 0.0f) {
				// Additive, not a cross-fade. The moan opens the mouth further than the face the
				// grammar is wearing, and leaves it alone where the pose is already wider - so an
				// open-mouthed cry keeps its shape through a quiet moment in the clip. Replacing
				// the pose instead meant that once lip-sync had envelopes for nearly every moan,
				// the mouth belonged to the moan the whole scene and the grammar never showed.
				const float base = baseWrites ? c.cur : 0.0f;
				const float moan = trackPh[i] * st.trackBlend;
				v = std::max(base, moan);
				write = baseWrites || moan > 0.0f;
			} else if (baseWrites) {
				v = c.cur;
			} else {
				v = 0.0f;
				write = false;
			}
			// OStim's face is on and shares this one. Its moan expressions and licking mouths set
			// phonemes of their own, and writing our small or zero values over them every frame is
			// what made the mouth stutter after a moan. Ours goes on top of theirs, not instead.
			if (st.layered && write) {
				c.mine = v;
				v = std::max(v, c.theirs);
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
			// Nothing of ours left in this channel: give it back as OStim has it, once.
			bool handBack = false;
			if (st.layered && !write && c.engaged) {
				v = c.theirs;
				write = true;
				handBack = true;
			}
			if (!write && !st.layered) c.engaged = false;  // not ours this frame (the mouth is yielded): nothing to restore
			if (write) {
				Write(fg->phenomeKeyFrame, i, v);
				c.last = v;
				c.engaged = !handBack;
				if (probing) {
					pr.ph[i] = v;
					pr.phMask |= 1u << i;
				}
			}
		}
		settled &= st.trackBlend <= 0.0f && st.mouthFloor <= 0.0f;
		for (int i = kBlinkL; i < kModifiers; ++i) {
			auto& c = st.mod[i];
			if (!c.used) continue;
			c.Step(dt);
			settled &= c.cur <= 0.005f;
			const bool squint = i == kSquintL || i == kSquintR;
			float mv = squint ? std::min(1.0f, c.cur + squintBoost * st.trackBlend) : c.cur;
			// Layered: OStim's expressions set brows, lids and eyes too, and a zero of ours is a
			// real write that wipes them - and OStim stops rewriting a value once it has reached
			// its goal, so nothing would ever put it back. Add to theirs instead.
			if (st.layered) {
				c.See(Val(fg->modifierKeyFrame, i));
				c.mine = mv;
				mv = std::max(mv, c.theirs);
			}
			Write(fg->modifierKeyFrame, i, mv);
			c.last = mv;
			c.engaged = true;
			if (probing) {
				pr.mod[i] = mv;
				pr.modMask |= 1u << i;
			}
			// Look modifiers stop eye blinking while non-zero, and a blink we write is the lids' to own; hand both back once at rest.
			if (IsGated(i) && c.cur <= 0.0f && c.target <= 0.0f) c.used = false;
		}
		if (st.exprUsed) {
			for (int i = 0; i < kExpressions; ++i) {
				auto& c = st.expr[i];
				c.Step(dt);
				settled &= c.cur <= 0.005f;
				Write(fg->expressionKeyFrame, i, c.cur);
				if (probing) pr.expr[i] = c.cur;
			}
			fg->exprOverride = true;
			if (probing) pr.exprWritten = true;
			// Eased out: stop writing the mood altogether. Writing zeros for the rest of the scene
			// would still overwrite whatever mood OStim sets, which is what this release is for.
			if (st.exprRelease) {
				bool zero = true;
				for (const auto& c : st.expr) zero &= c.cur <= 0.005f;
				if (zero) {
					st.exprUsed = false;
					st.exprRelease = false;
				}
			}
		}
		if (probing) pr.valid = true;

		if (st.releasing && settled) {
			for (int i = 0; i < kPhonemes; ++i) Write(fg->phenomeKeyFrame, i, 0.0f);
			for (int i = kBlinkL; i < kModifiers; ++i) {
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

	float ReassertFace(RE::BSFaceGenAnimationData* data)
	{
		if (g_count.load(std::memory_order_relaxed) == 0 || !data) return -1.0f;

		std::scoped_lock l(g_lock);
		State* found = nullptr;
		for (auto& [id, st] : g_states) {
			if (st.fg == data) {
				found = &st;
				break;
			}
		}
		if (!found || found->suspended) return -1.0f;
		auto& st = *found;

		RE::BSSpinLockGuard guard(data->lock);
		const bool probing = Scenes::Now() < g_probeUntil.load();
		const float pre = probing ? FinalSum(*data) : -1.0f;
		if (!st.layered) {
			// This face is ours alone (OStim's writer is off). Put back whatever we last wrote over anything that
			// changed it since, so the game reads ours: an outside reset - another mod's script, or the engine on an
			// animation change - zeroed whole faces for a frame at every node change (seen in the 1.8.2 probe: every
			// channel of one actor to zero in one frame, 0.4-0.8 s after the scene node changed).
			int restored = 0;
			float worst = 0.0f;
			const auto check = [&](RE::BSFaceGenKeyframeMultiple& kf, int i, float want) {
				const float d = std::abs(Val(kf, static_cast<std::uint32_t>(i)) - want);
				if (d <= 0.02f) return;
				++restored;
				worst = std::max(worst, d);
				Write(kf, static_cast<std::uint32_t>(i), want);
			};
			for (int i = 0; i < kPhonemes; ++i) {
				if (st.ph[i].engaged) check(data->phenomeKeyFrame, i, st.ph[i].last);
			}
			for (int i = kBlinkL; i < kModifiers; ++i) {
				if (st.mod[i].used && st.mod[i].engaged) check(data->modifierKeyFrame, i, st.mod[i].last);
			}
			if (st.exprUsed) {
				for (int i = 0; i < kExpressions; ++i) check(data->expressionKeyFrame, i, st.expr[i].cur);
				data->exprOverride = true;
			}
			if (restored > 0 && probing) {
				++st.probe.restores;
				st.probe.restoreWorst = std::max(st.probe.restoreWorst, worst);
				if (restored >= 4) {
					RE::Actor* who = nullptr;
					for (auto& [id, other] : g_states) {
						if (&other == &st) who = RE::TESForm::LookupByID<RE::Actor>(id);
					}
					logger::info("Probe {:08X} {}: face reset by something else, {} channels (largest {:.2f}); put back before it rendered",
							who ? who->GetFormID() : 0u, who ? who->GetDisplayFullName() : "?", restored, worst);
				}
			}
			return pre;
		}
		// OStim's updater runs on its own thread and writes these same channels about every 50 ms.
		// The game turns the keyframes into the values it renders when the face node updates, which
		// is outside the actor's animation update (measured: 0 of 5440 frames changed inside it), so
		// a write after the animation update leaves a window in which OStim's value is the one read.
		// This runs from the face node's own update, immediately before that read: whatever OStim wrote
		// since our last pass is taken as its value and ours goes back over it.
		// A tongue held out is left to the full pass: its jaw rules are not repeated here.
		if (st.mouthFloor <= 0.0f) {
			for (int i = 0; i < kPhonemes; ++i) {
				auto& c = st.ph[i];
				if (!c.engaged) continue;
				c.See(Val(data->phenomeKeyFrame, i));
				const float v = std::max(c.mine, c.theirs);
				Write(data->phenomeKeyFrame, i, v);
				c.last = v;
			}
		}
		for (int i = kBlinkL; i < kModifiers; ++i) {
			auto& c = st.mod[i];
			if (!c.used || !c.engaged) continue;
			c.See(Val(data->modifierKeyFrame, i));
			const float v = std::max(c.mine, c.theirs);
			Write(data->modifierKeyFrame, i, v);
			c.last = v;
		}
		return pre;
	}

	void NoteFaceRead(RE::BSFaceGenAnimationData* data, float pre)
	{
		if (pre < 0.0f || !data) return;
		std::scoped_lock l(g_lock);
		for (auto& [id, st] : g_states) {
			if (st.fg != data) continue;
			++st.probe.preChecked;
			if (std::abs(FinalSum(*data) - pre) > 0.02f) ++st.probe.movedInside;
			return;
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
