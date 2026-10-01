// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Pulse.h"

#include "Arousal.h"
#include "Body.h"
#include "Scenes.h"
#include "Settings.h"
#include "Skin.h"

namespace Pulse
{
	namespace
	{
		bool BusOn()
		{
			std::scoped_lock l(Settings::lock);
			return Settings::General::bPulseBus;
		}
	}

	void Emit(const char* a_event, int a_thread, RE::Actor* a_actor, float a_value)
	{
		if (!BusOn()) return;
		auto* src = SKSE::GetModCallbackEventSource();
		if (!src) return;
		// Papyrus formatted the id as a signed Int ("" + a.GetFormID()), so ESL ids go negative.
		const auto id = a_actor ? static_cast<std::int32_t>(a_actor->GetFormID()) : 0;
		SKSE::ModCallbackEvent ev{ a_event, std::format("{}|{}", a_thread, id), a_value, nullptr };
		src->SendEvent(&ev);
	}

	void Paint(const Beat& b)
	{
		Body::OnPaint(b);
		Skin::OnPaint(b);
		Emit("SLED_Paint", b.thread, b.actor, static_cast<float>(b.enj));
		Emit("SLED_Phase", b.thread, b.actor, static_cast<float>(b.phrase));
	}

	void PhraseChanged(const Beat& b)
	{
		Emit("SLED_Beat", b.thread, b.actor, static_cast<float>(b.phrase));
	}

	void DomChanged(const Beat& b, int)
	{
		const float v = static_cast<float>(b.enj);
		switch (b.dom) {
		case Scenes::kClimax:
			Body::OnClimaxPeak(b.actor, std::clamp(v / 100.0f, 0.35f, 1.0f));
			Skin::OnClimaxPeak(b.actor);
			Emit("SLED_ClimaxPeak", b.thread, b.actor, v);
			break;
		case Scenes::kAfterglow:
			Body::ClearActor(b.actor);
			Skin::OnAfterglow(b.actor);
			Emit("SLED_Afterglow", b.thread, b.actor, v);
			break;
		case Scenes::kDistress:
			Body::ClearActor(b.actor);
			Skin::OnDistress(b.actor, b.victim);
			Emit("SLED_Distress", b.thread, b.actor, v);
			break;
		case Scenes::kPleasure:
		case Scenes::kPlateau:
			Emit("SLED_Rise", b.thread, b.actor, v);
			break;
		default:
			break;
		}
	}

	void Climax(RE::Actor* a, int thread)
	{
		Arousal::OnClimax(a);
		Body::OnClimaxPeak(a, 1.0f);
		Skin::OnClimaxPeak(a);
		Emit("SLED_ClimaxPeak", thread, a, 1.0f);
	}

	void ClearActor(RE::Actor* a, int thread)
	{
		Body::ClearActor(a);
		Skin::ClearActor(a);
		Emit("SLED_ClearActor", thread, a, 0.0f);
	}

	void SceneEnd(int thread)
	{
		if (!BusOn()) return;
		if (auto* src = SKSE::GetModCallbackEventSource()) {
			SKSE::ModCallbackEvent ev{ "SLED_SceneEnd", std::format("{}|0", thread), 0.0f, nullptr };
			src->SendEvent(&ev);
		}
	}
}
