// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "SpellCast.h"

#include <deque>

#include "Scenes.h"

namespace SpellCast
{
	namespace
	{
		// Matchmaker starts its scene 5 s (Papyrus RegisterForSingleUpdate) after the last actor
		// is tagged; OStim then needs a moment to fire ostim_thread_start and hand us the actors.
		constexpr float kSceneWindow = 30.0f;  // the spell set the scene going at most this long before its actors were known
		constexpr float kCastSlack = 1.0f;     // an apply may be reported just before its cast
		constexpr float kCastFlight = 10.0f;   // aimed spells: projectile travel
		constexpr float kRestart = 10.0f;      // a spell-started thread stopped and restarted this soon is the same scene
		constexpr float kKeep = 600.0f;        // Matchmaker holds its targets until the player casts on the rest
		constexpr std::size_t kMaxEntries = 256;

		struct Cast
		{
			float time;
			RE::FormID spell;
			const RE::TESFile* file;          // the plugin that defines the spell
			std::vector<RE::FormID> effects;  // script-archetype base effects
		};
		struct Hit
		{
			float time;
			RE::FormID target;
			RE::FormID effect;
		};
		struct Ended
		{
			float time;
			std::vector<RE::FormID> victims;
		};

		std::mutex g_lock;
		// Seconds of unpaused game, the clock Papyrus updates run on. Real time would keep counting
		// while a menu holds the spell's script back.
		float g_clock = 0.0f;
		float g_lastReal = -1.0f;
		std::deque<Cast> g_casts;
		std::deque<Hit> g_hits;
		std::deque<Ended> g_ended;                      // spell-started threads that just ended
		std::unordered_map<RE::FormID, float> g_talks;  // NPC -> last time the player was in dialogue with them

		void Prune()
		{
			while (!g_casts.empty() && (g_casts.size() > kMaxEntries || g_clock - g_casts.front().time > kKeep)) g_casts.pop_front();
			while (!g_hits.empty() && (g_hits.size() > kMaxEntries || g_clock - g_hits.front().time > kKeep)) g_hits.pop_front();
			while (!g_ended.empty() && g_clock - g_ended.front().time > kRestart) g_ended.pop_front();
			std::erase_if(g_talks, [](const auto& t) { return g_clock - t.second > kKeep; });
		}

		// The player's cast that delivered this effect: one of the spell's own effects, landing
		// between just before the cast and the end of a projectile's flight.
		const Cast* CastOf(const Hit& a_hit)
		{
			for (const auto& c : g_casts) {
				if (a_hit.time < c.time - kCastSlack || a_hit.time > c.time + kCastFlight) continue;
				if (std::ranges::find(c.effects, a_hit.effect) != c.effects.end()) return &c;
			}
			return nullptr;
		}

		// The player's latest cast, after the hit, of another spell from the same plugin. Matchmaker
		// tags its targets one cast at a time and starts the scene only once the player has cast on
		// everyone in it, themselves included (a separate spell), however long that takes.
		const Cast* FollowUp(const Hit& a_hit, const Cast& a_cast)
		{
			const Cast* out = nullptr;
			if (!a_cast.file) return out;  // a runtime spell has no plugin to match
			for (const auto& c : g_casts) {
				if (&c != &a_cast && c.time >= a_hit.time && c.file == a_cast.file) out = &c;
			}
			return out;
		}

		// Whoever the player is in dialogue with. Only while the Dialogue Menu is open: lastSpeaker
		// outlives the conversation.
		RE::FormID Speaker()
		{
			if (!RE::UI::GetSingleton()->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) return 0;
			auto* topics = RE::MenuTopicManager::GetSingleton();
			if (!topics) return 0;
			auto ref = topics->speaker.get();
			if (!ref) ref = topics->lastSpeaker.get();
			return ref && !ref->IsPlayerRef() ? ref->GetFormID() : 0;
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
				Cast c{ 0.0f, e->spell, m->GetFile(0), {} };
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
			// touches the player, who is never the victim). An attack is not a scene spell: hostile
			// effects, and anything cast on an enemy fighting the player, are left out, so a
			// defeat scene after a fight (Yamete) keeps the roles its mod gave it.
			RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* e, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
			{
				if (!e || !e->caster || !e->target || !e->caster->IsPlayerRef() || e->target->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
				auto* effect = RE::TESForm::LookupByID<RE::EffectSetting>(e->magicEffect);
				if (!IsScriptEffect(effect) || effect->IsHostile()) return RE::BSEventNotifyControl::kContinue;
				if (auto* a = e->target->As<RE::Actor>(); a && a->IsInCombat() && a->IsHostileToActor(RE::PlayerCharacter::GetSingleton())) return RE::BSEventNotifyControl::kContinue;
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
		const RE::FormID speaker = Speaker();
		std::scoped_lock l(g_lock);
		if (g_lastReal >= 0.0f && !paused) g_clock += std::clamp(real - g_lastReal, 0.0f, 1.0f);  // a hitch or load is not play time
		g_lastReal = real;
		if (speaker) g_talks[speaker] = g_clock;
	}

	void Clear()
	{
		std::scoped_lock l(g_lock);
		g_casts.clear();
		g_hits.clear();
		g_ended.clear();
		g_talks.clear();
	}

	void ThreadEnded(const std::vector<RE::FormID>& a_victims)
	{
		if (a_victims.empty()) return;
		std::scoped_lock l(g_lock);
		g_ended.push_back({ g_clock, a_victims });
		Prune();
	}

	std::vector<RE::FormID> StartedBySpell(const std::vector<RE::Actor*>& a_actors)
	{
		std::scoped_lock l(g_lock);
		Prune();
		std::vector<RE::Actor*> npcs;
		for (auto* a : a_actors) {
			if (a && !a->IsPlayerRef()) npcs.push_back(a);
		}
		const auto npc = [&](RE::FormID a_id) {
			auto it = std::ranges::find_if(npcs, [&](RE::Actor* a) { return a->GetFormID() == a_id; });
			return it != npcs.end() ? *it : nullptr;
		};
		// The player's latest conversation with this NPC, or -1.
		const auto talk = [&](RE::FormID a_id) {
			auto it = g_talks.find(a_id);
			return it != g_talks.end() ? it->second : -1.0f;
		};
		std::vector<RE::FormID> victims;
		const auto victim = [&](RE::FormID a_id) { return std::ranges::find(victims, a_id) != victims.end(); };

		// A spell-started thread just ended: its victims are still victims here, unless they have
		// talked with the player since (Followers Ask To Join stops the thread and starts a bigger
		// one once the follower has asked; the follower is no victim).
		for (auto e = g_ended.rbegin(); e != g_ended.rend(); ++e) {
			for (auto id : e->victims) {
				auto* a = npc(id);
				if (!a || victim(id)) continue;
				if (talk(id) > e->time) {
					logger::info("{:08X} {} talked with the player after the spell-started scene they were in ended: not a victim",
						id, a->GetDisplayFullName());
					continue;
				}
				logger::info("Scene continues a spell-started scene that ended {:.1f}s before it: {:08X} {} is still its victim",
					g_clock - e->time, id, a->GetDisplayFullName());
				victims.push_back(id);
			}
		}

		// Per NPC, the spell hit that set the scene going most recently: the hit itself, or a later
		// cast from the same plugin that completed it.
		struct Spelled
		{
			RE::FormID target;
			const Hit* hit;
			const Cast* cast;
			const Cast* later;
			float trigger;
		};
		std::vector<Spelled> spelled;
		for (const auto& h : g_hits) {
			if (!npc(h.target)) continue;
			const Cast* c = CastOf(h);
			if (!c) continue;
			const Cast* f = FollowUp(h, *c);
			const float t = f ? f->time : h.time;
			if (g_clock - t > kSceneWindow) continue;
			const Spelled s{ h.target, &h, c, f, t };
			if (auto it = std::ranges::find(spelled, h.target, &Spelled::target); it == spelled.end()) spelled.push_back(s);
			else if (t >= it->trigger) *it = s;
		}
		bool fromHits = false;
		for (const auto& s : spelled) {
			auto* target = npc(s.target);
			auto* spell = RE::TESForm::LookupByID<RE::MagicItem>(s.cast->spell);
			auto* laterSpell = s.later ? RE::TESForm::LookupByID<RE::MagicItem>(s.later->spell) : nullptr;
			const std::string completed = s.later ? std::format(", completed by {:08X} {} {:.1f}s before the scene", s.later->spell,
			                                                    laterSpell ? laterSpell->GetName() : "", g_clock - s.later->time) :
			                                        std::string{};
			// Talking came after the spell: the NPC asked, or was asked, and the scene came out of that.
			if (const float said = talk(s.target); said >= s.trigger) {
				logger::info("{:08X} {} asked for the scene in dialogue {:.1f}s before it, after the player's spell {:08X} {} hit them{}: not a victim",
					s.target, target->GetDisplayFullName(), g_clock - said, s.cast->spell, spell ? spell->GetName() : "", completed);
				continue;
			}
			fromHits = true;
			if (victim(s.target)) continue;
			logger::info("Scene started by the player's spell: {:08X} {} hit {:08X} {} {:.1f}s before the scene{}",
				s.cast->spell, spell ? spell->GetName() : "", s.target, target->GetDisplayFullName(), g_clock - s.hit->time, completed);
			victims.push_back(s.target);
		}
		if (victims.empty()) return victims;

		// Whoever else is in the scene came of their own accord.
		for (auto* a : npcs) {
			if (!victim(a->GetFormID())) logger::info("{:08X} {} is in the spell-started scene of their own accord: aggressor", a->GetFormID(), a->GetDisplayFullName());
		}
		// These hits have set their scene going; a later scene with these NPCs needs a new spell.
		if (fromHits) std::erase_if(g_hits, [&](const Hit& h) { return npc(h.target) != nullptr; });
		return victims;
	}
}
