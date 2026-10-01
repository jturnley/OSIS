// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Body.h"

#include "Compat.h"

#include "Scenes.h"
#include "Settings.h"

namespace Body
{
	namespace
	{
		constexpr float kRampIn = 0.25f;
		constexpr float kHold = 4.0f;  // through the climax; the original's 1.8 s was over before you noticed it
		constexpr float kRampOut = 0.6f;

		constexpr std::array kToes{ "NPC L Toe0 [LToe]", "NPC R Toe0 [RToe]" };
		// XPMSSE gives every toe two bones of its own under Toe0: "Joint N" at the base and "Toe N"
		// at the middle joint. Feet weighted to them (Aerosmith TJ Feet and similar) can curl toe
		// by toe; on other feet these bones move nothing. Base joint and middle joint bend by these
		// fractions of the toe angle on top of Toe0, the big toe (1) less than the small ones.
		constexpr float kToeBase = 0.35f;
		constexpr float kToeMiddle = 1.0f;
		constexpr std::array kPerToe{ 0.7f, 1.0f, 1.0f, 1.05f, 1.1f };
		constexpr std::array kFingers{
			"NPC L Finger10 [LF10]", "NPC L Finger11 [LF11]", "NPC L Finger20 [LF20]", "NPC L Finger21 [LF21]",
			"NPC L Finger30 [LF30]", "NPC L Finger31 [LF31]", "NPC L Finger40 [LF40]", "NPC L Finger41 [LF41]",
			"NPC R Finger10 [RF10]", "NPC R Finger11 [RF11]", "NPC R Finger20 [RF20]", "NPC R Finger21 [RF21]",
			"NPC R Finger30 [RF30]", "NPC R Finger31 [RF31]", "NPC R Finger40 [RF40]", "NPC R Finger41 [RF41]",
		};

		struct State
		{
			float magnitude = 0.0f;
			float start = 0.0f;
			float hold = kHold;
			const RE::NiAVObject* root = nullptr;
			bool reported = false;  // logged the first application of this curl
			std::vector<RE::NiAVObject*> toes;
			std::vector<RE::NiAVObject*> fingers;

			// A per-toe bone. No animation drives these, so nothing resets them each frame: the
			// curl is written as rest * rotation, and the rest pose is put back when it ends.
			struct ToeBone
			{
				RE::NiAVObject* node;
				RE::NiMatrix3 rest;
				float scale;  // fraction of the toe angle
				bool base;    // the toe's base joint (its middle joint follows it in the list)
			};
			std::vector<ToeBone> toeBones;
		};

		std::mutex g_lock;
		std::unordered_map<RE::FormID, State> g_states;
		std::atomic<std::size_t> g_count{ 0 };
		std::string g_status = "idle";

		float StyleScalar()
		{
			// The original's 0.35 for Realistic (the default style) left a ~9 degree toe curl:
			// invisible in a scene. Realistic now keeps most of the curl.
			if (!Settings::Body::bStyleGated) return 1.0f;
			const float style = Settings::StyleValue();
			if (style < 0.5f) return 0.80f;
			if (style < 1.5f) return 0.90f;
			return 1.0f;
		}

		void Start(RE::Actor* a, float mag, float hold)
		{
			if (!a || mag <= 0.0f) return;
			std::scoped_lock l(g_lock);
			auto& st = g_states[a->GetFormID()];
			st.magnitude = std::clamp(mag, 0.0f, 1.0f);
			st.start = Scenes::Now();
			st.hold = hold;
			st.reported = false;
			logger::info("Body: curl {:08X} {} at {:.0f}% for {:.1f} s", a->GetFormID(), a->GetDisplayFullName(), st.magnitude * 100.0f, hold);
			g_count = g_states.size();
			g_status = std::format("{}: curl {:.0f}%", a->GetDisplayFullName(), st.magnitude * 100.0f);
		}

		bool Animatable(RE::Actor* a)
		{
			return a && !a->IsDead() && !a->IsChild() && a->Is3DLoaded();
		}

		RE::NiMatrix3 AxisRotation(int axis, float degrees)
		{
			const float r = degrees * std::numbers::pi_v<float> / 180.0f;
			const float c = std::cos(r);
			const float s = std::sin(r);
			RE::NiMatrix3 m;
			m.entry[0][0] = m.entry[1][1] = m.entry[2][2] = 1.0f;
			m.entry[0][1] = m.entry[0][2] = m.entry[1][0] = m.entry[1][2] = m.entry[2][0] = m.entry[2][1] = 0.0f;
			if (axis == 0) {
				m.entry[1][1] = c, m.entry[1][2] = -s, m.entry[2][1] = s, m.entry[2][2] = c;
			} else if (axis == 1) {
				m.entry[0][0] = c, m.entry[0][2] = s, m.entry[2][0] = -s, m.entry[2][2] = c;
			} else {
				m.entry[0][0] = c, m.entry[0][1] = -s, m.entry[1][0] = s, m.entry[1][1] = c;
			}
			return m;
		}

		// Rotation by a_degrees about a unit axis (Rodrigues).
		RE::NiMatrix3 AxisAngle(const RE::NiPoint3& a, float a_degrees)
		{
			const float r = a_degrees * std::numbers::pi_v<float> / 180.0f;
			const float c = std::cos(r);
			const float s = std::sin(r);
			const float t = 1.0f - c;
			RE::NiMatrix3 m;
			m.entry[0][0] = t * a.x * a.x + c, m.entry[0][1] = t * a.x * a.y - s * a.z, m.entry[0][2] = t * a.x * a.z + s * a.y;
			m.entry[1][0] = t * a.x * a.y + s * a.z, m.entry[1][1] = t * a.y * a.y + c, m.entry[1][2] = t * a.y * a.z - s * a.x;
			m.entry[2][0] = t * a.x * a.z - s * a.y, m.entry[2][1] = t * a.y * a.z + s * a.x, m.entry[2][2] = t * a.z * a.z + c;
			return m;
		}

		// A parent-frame axis in the frame of a bone with local rotation a_rot: rot^T * axis.
		RE::NiPoint3 ToLocal(const RE::NiMatrix3& a_rot, const RE::NiPoint3& a_axis)
		{
			return {
				a_rot.entry[0][0] * a_axis.x + a_rot.entry[1][0] * a_axis.y + a_rot.entry[2][0] * a_axis.z,
				a_rot.entry[0][1] * a_axis.x + a_rot.entry[1][1] * a_axis.y + a_rot.entry[2][1] * a_axis.z,
				a_rot.entry[0][2] * a_axis.x + a_rot.entry[1][2] * a_axis.y + a_rot.entry[2][2] * a_axis.z,
			};
		}

		void RestoreToes(State& st)
		{
			for (auto& b : st.toeBones) b.node->local.rotate = b.rest;
		}

		void Resolve(State& st, RE::NiAVObject* root)
		{
			st.root = root;
			st.toes.clear();
			st.fingers.clear();
			st.toeBones.clear();
			for (const char* n : kToes) {
				if (auto* o = root->GetObjectByName(n)) st.toes.push_back(o);
			}
			for (const auto& [side, foot] : { std::pair{ 'L', "Lft " }, std::pair{ 'R', "Rft " } }) {
				for (int i = 1; i <= 5; ++i) {
					auto* base = root->GetObjectByName(RE::BSFixedString(std::format("NPC {} Joint {} [{}]", side, i, foot)));
					auto* middle = root->GetObjectByName(RE::BSFixedString(std::format("NPC {} Toe {} [{}]", side, i, foot)));
					if (base) st.toeBones.push_back({ base, base->local.rotate, kToeBase * kPerToe[i - 1], true });
					if (middle) st.toeBones.push_back({ middle, middle->local.rotate, kToeMiddle * kPerToe[i - 1], false });
				}
			}
			for (const char* n : kFingers) {
				if (auto* o = root->GetObjectByName(n)) st.fingers.push_back(o);
			}
		}
	}

	void OnClimaxPeak(RE::Actor* a, float value)
	{
		float mag;
		{
			std::scoped_lock l(Settings::lock);
			if (Compat::Disabled(Compat::kBody) || !Settings::Body::bEnabled || !Settings::General::bEnabled) {
				logger::info("Body: climax ignored (module off: compat {}, enabled {}, general {})",
					Compat::Disabled(Compat::kBody), Settings::Body::bEnabled, Settings::General::bEnabled);
				return;
			}
			mag = std::clamp(Settings::Body::fStrength * StyleScalar() * std::clamp(value, 0.35f, 1.0f), 0.0f, 1.0f);
		}
		if (!Animatable(a) || !Scenes::InAnyScene(a)) {
			logger::info("Body: climax ignored for {:08X} (animatable {}, in scene {})", a ? a->GetFormID() : 0, Animatable(a), a && Scenes::InAnyScene(a));
			return;
		}
		Start(a, mag, kHold);
	}

	void Test(RE::Actor* a)
	{
		if (!Animatable(a)) return;
		Start(a, 0.85f, 2.0f);
	}

	void ClearActor(RE::Actor* a)
	{
		if (!a) return;
		std::scoped_lock l(g_lock);
		// Letting the ramp-out run avoids a snap; an immediate stop is only needed on unload.
		if (auto it = g_states.find(a->GetFormID()); it != g_states.end()) {
			auto& st = it->second;
			const float now = Scenes::Now();
			if (now - st.start < kRampIn + st.hold) st.start = now - (kRampIn + st.hold);
		}
	}

	void ClearAll()
	{
		std::scoped_lock l(g_lock);
		for (auto& [id, st] : g_states) {
			auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
			if (a && st.root && a->Get3D1(false) == st.root) RestoreToes(st);
		}
		g_states.clear();
		g_count = 0;
		g_status = "cleared";
	}

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		return std::format("{} ({} actor(s) curling)", g_status, g_states.size());
	}

	void Update(RE::Actor* a, float)
	{
		if (g_count.load(std::memory_order_relaxed) == 0 || !a) return;
		{
			// Runs for every animated actor on the job threads; skip the settings lock for
			// the ones that aren't curling.
			std::scoped_lock l(g_lock);
			if (!g_states.contains(a->GetFormID())) return;
		}
		bool toe, hand;
		float toeDeg, fingerDeg, perToe;
		int axis;
		{
			std::scoped_lock l(Settings::lock);
			perToe = Settings::Body::fPerToe;
			toe = Settings::Body::bToe;
			hand = Settings::Body::bHand;
			toeDeg = Settings::Body::fToeDegrees;
			fingerDeg = Settings::Body::fFingerDegrees;
			axis = Settings::Body::iCurlAxis;
		}
		std::scoped_lock l(g_lock);
		auto it = g_states.find(a->GetFormID());
		if (it == g_states.end()) return;
		auto& st = it->second;

		const float t = Scenes::Now() - st.start;
		float env;
		if (t < kRampIn) env = t / kRampIn;
		else if (t < kRampIn + st.hold) env = 1.0f;
		else env = 1.0f - (t - kRampIn - st.hold) / kRampOut;
		auto* root = a->Get3D1(false);
		if (env <= 0.0f) {
			if (root && root == st.root) {
				RestoreToes(st);
				RE::NiUpdateData done{ 0.0f, RE::NiUpdateData::Flag::kNone };
				for (auto* n : st.toes) n->UpdateDownwardPass(done, 0);
			}
			g_states.erase(it);
			g_count = g_states.size();
			return;
		}
		env = env * env * (3.0f - 2.0f * env);  // smoothstep

		if (!root) return;
		if (root != st.root) Resolve(st, root);
		if (!st.reported) {
			st.reported = true;
			logger::info("Body: applying curl on {:08X}: {} toe bone(s) + {} per-toe bone(s), {} finger bone(s), axis {}, toes {:.0f} deg, fingers {:.0f} deg at peak",
				a->GetFormID(), st.toes.size(), st.toeBones.size(), st.fingers.size(), axis, toeDeg * st.magnitude, fingerDeg * st.magnitude);
		}

		const float k = st.magnitude * env;
		if (toe) {
			const auto r = AxisRotation(axis, toeDeg * k);  // toes curl down (the negative angle bent them up)
			for (auto* n : st.toes) n->local.rotate = n->local.rotate * r;
		}
		// Each toe bends further at its own joints, about the same side-to-side axis as Toe0
		// (the base joints hang off Toe0, so its axis is taken into each bone's own frame).
		RE::NiPoint3 footAxis{ 0.0f, 0.0f, 0.0f };
		(axis == 0 ? footAxis.x : axis == 1 ? footAxis.y : footAxis.z) = 1.0f;
		RE::NiPoint3 baseAxis = footAxis;
		for (auto& b : st.toeBones) {
			// The middle joint's parent is its base joint.
			const auto local = ToLocal(b.rest, b.base ? footAxis : baseAxis);
			if (b.base) baseAxis = local;
			const float deg = toe ? toeDeg * k * perToe * b.scale : 0.0f;
			b.node->local.rotate = b.rest * AxisAngle(local, deg);
		}
		if (hand) {
			const auto r = AxisRotation(axis, fingerDeg * k);  // fingers close
			for (auto* n : st.fingers) n->local.rotate = n->local.rotate * r;
		}
		// Push the new local rotations to world space now, bone and children (the fingertip
		// segments). If the engine already ran its world pass for this skeleton, a local-only
		// change would be overwritten by the next animation pose before it was ever drawn.
		// Downward only: parents are shared with the rest of the scene.
		RE::NiUpdateData ctx{ 0.0f, RE::NiUpdateData::Flag::kNone };
		if (toe || !st.toeBones.empty()) {
			for (auto* n : st.toes) n->UpdateDownwardPass(ctx, 0);  // covers the per-toe bones below it
		}
		if (hand) {
			for (auto* n : st.fingers) n->UpdateDownwardPass(ctx, 0);
		}
	}
}
