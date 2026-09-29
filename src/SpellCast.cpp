#include "SpellCast.h"

#include <deque>

#include "Scenes.h"

namespace SpellCast
{
	namespace
	{
		// Matchmaker starts its scene 5 s (Papyrus RegisterForSingleUpdate) after the last actor
		// is tagged; OStim then needs a moment to fire ostim_thread_start and hand us the actors.
		constexpr float kSceneWindow = 30.0f;  // effect landed at most this long before the scene's actors were known
		constexpr float kCastSlack = 1.0f;     // an apply may be reported just before its cast
		constexpr float kCastFlight = 10.0f;   // aimed spells: projectile travel
		constexpr float kKeep = 120.0f;

		struct Cast
		{
			float time;
			RE::FormID spell;
			std::vector<RE::FormID> effects;  // script-archetype base effects
		};
		struct Hit
		{
			float time;
			RE::FormID target;
			RE::FormID effect;
		};

		std::mutex g_lock;
		// Seconds of unpaused game, the clock Papyrus updates run on. Real time would keep counting
		// while a menu holds the spell's script back.
		float g_clock = 0.0f;
		float g_lastReal = -1.0f;
		std::deque<Cast> g_casts;
		std::deque<Hit> g_hits;

		void Prune()
		{
			while (!g_casts.empty() && g_clock - g_casts.front().time > kKeep) g_casts.pop_front();
			while (!g_hits.empty() && g_clock - g_hits.front().time > kKeep) g_hits.pop_front();
		}

		bool IsScriptEffect(const RE::EffectSetting* e) { return e && e->GetArchetype() == RE::EffectSetting::Archetype::kScript; }

		// Something the player actually casts: abilities, diseases, potions and enchantments
		// apply their effects without a cast.
		bool IsCastSpell(RE::MagicItem* m)
		{
			using T = RE::MagicSystem::SpellType;
			switch (m->GetSpellType()) {
			case T::kSpell:
			case T::kLesserPower:
			case T::kPower:
			case T::kVoicePower:
			case T::kScroll:
			case T::kStaffEnchantment:
				return m->GetCastingType() != RE::MagicSystem::CastingType::kConstantEffect;
			default:
				return false;
			}
		}

		class Sink final :
			public RE::BSTEventSink<RE::TESSpellCastEvent>,
			public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* e, RE::BSTEventSource<RE::TESSpellCastEvent>*) override
			{
				if (!e || !e->object || !e->object->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
				auto* m = RE::TESForm::LookupByID<RE::MagicItem>(e->spell);
				if (!m || !IsCastSpell(m)) return RE::BSEventNotifyControl::kContinue;
				Cast c{ 0.0f, e->spell, {} };
				for (auto* eff : m->effects) {
					if (eff && IsScriptEffect(eff->baseEffect)) c.effects.push_back(eff->baseEffect->GetFormID());
				}
				if (c.effects.empty()) return RE::BSEventNotifyControl::kContinue;
				std::scoped_lock l(g_lock);
				c.time = g_clock;
				g_casts.push_back(std::move(c));
				Prune();
				return RE::BSEventNotifyControl::kContinue;
			}

			// The player's script effects landing on someone else (a self-cast spell only ever
			// touches the player, who is never the victim).
			RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* e, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
			{
				if (!e || !e->caster || !e->target || !e->caster->IsPlayerRef() || e->target->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
				if (!IsScriptEffect(RE::TESForm::LookupByID<RE::EffectSetting>(e->magicEffect))) return RE::BSEventNotifyControl::kContinue;
				std::scoped_lock l(g_lock);
				g_hits.push_back({ g_clock, e->target->GetFormID(), e->magicEffect });
				Prune();
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		Sink g_sink;
	}

	void Init()
	{
		auto* src = RE::ScriptEventSourceHolder::GetSingleton();
		if (!src) return;
		src->AddEventSink<RE::TESSpellCastEvent>(&g_sink);
		src->AddEventSink<RE::TESMagicEffectApplyEvent>(&g_sink);
		logger::info("Watching the player's spells for scene starts");
	}

	void Tick()
	{
		const float real = Scenes::Now();
		const bool paused = RE::UI::GetSingleton()->GameIsPaused();
		std::scoped_lock l(g_lock);
		if (g_lastReal >= 0.0f && !paused) g_clock += std::clamp(real - g_lastReal, 0.0f, 1.0f);  // a hitch or load is not play time
		g_lastReal = real;
	}

	void Clear()
	{
		std::scoped_lock l(g_lock);
		g_casts.clear();
		g_hits.clear();
	}

	bool StartedBySpell(const std::vector<RE::Actor*>& a_actors)
	{
		std::scoped_lock l(g_lock);
		Prune();
		for (auto* a : a_actors) {
			if (!a || a->IsPlayerRef()) continue;
			const RE::FormID id = a->GetFormID();
			for (auto h = g_hits.rbegin(); h != g_hits.rend() && g_clock - h->time <= kSceneWindow; ++h) {
				if (h->target != id) continue;
				for (const auto& c : g_casts) {
					if (h->time < c.time - kCastSlack || h->time > c.time + kCastFlight) continue;
					if (std::ranges::find(c.effects, h->effect) == c.effects.end()) continue;
					auto* spell = RE::TESForm::LookupByID<RE::MagicItem>(c.spell);
					logger::info("Scene started by the player's spell: {:08X} {} hit {:08X} {} {:.1f}s before the scene",
						c.spell, spell ? spell->GetName() : "", id, a->GetDisplayFullName(), g_clock - h->time);
					return true;
				}
			}
		}
		return false;
	}
}
