#include "Voice.h"

#include "Face/Engine.h"
#include "LipSync.h"
#include "Papyrus.h"
#include "Scheduler.h"
#include "Settings.h"

namespace Voice
{
	namespace
	{
		namespace VS = Settings::Voice;
		using Reaction = Face::Engine::Reaction;

		// ------------------------------------------------------------ vanilla lines
		// Skyrim.esm dialogue in the speaker's voice type: sound\voice\skyrim.esm\<voice type>\<stem>.fuz.
		enum Pool : int { kHelp, kWitness, kGuard, kAlly, kPlead, kMild, kDefy, kScream, kBreath, kPoolCount };
		constexpr const char* kPoolNames[kPoolCount] = { "help", "witness", "guard", "ally", "plead", "mild", "defy", "scream", "breath" };

		constexpr std::string_view kHelpLines[] = {
			"dialoguegeneric__0002a45d_1",
			"dialoguegeneric__000d3614_1",
			"dialoguegeneric__000d3613_1",
			"dialoguegeneric__001038a5_1",
			"dialoguegeneric__000d4042_1",
			"dialoguegeneric__000a7b03_1",
			"dialoguegeneric__000a7b02_1",
		};
		constexpr std::string_view kWitnessLines[] = {
			"dialoguegeneric__0002a45c_1",
		};
		constexpr std::string_view kGuardLines[] = {
			"dialoguecrimeguards__0002c170_1",
			"dialoguecrimeguards__0002c172_1",
			"dialoguecr_dgcrimeforcegre_0002a37c_1",
		};
		constexpr std::string_view kAllyLines[] = {
			"dialoguegeneric__0002a460_1",
		};
		constexpr std::string_view kPleadLines[] = {
			"dialoguegeneric__00017708_1",
			"dialoguegeneric__0001770b_1",
			"dialoguegeneric__0001770d_1",
			"dialoguegeneric__00039b98_1",
			"dialoguegeneric__00039b97_1",
			"dialoguegeneric__00017711_1",
			"dialoguegeneric__000964e0_1",
			"dialoguegeneric__0002a467_1",
			"dialoguegeneric__000a7b00_1",
		};
		constexpr std::string_view kMildLines[] = {
			"dialoguege__00023c18_1",
			"dialoguege__00023c1c_1",
			"dialoguege__00023c17_1",
		};
		constexpr std::string_view kDefyLines[] = {
			"dialoguege_dialoguegeneric_000d3e92_1",
			"dialoguege_dialoguegeneric_000142b7_1",
			"dialoguegeneric__0002a460_1",
			"dialoguegeneric__0002a469_1",
			"dialoguegeneric__0001770e_1",
			"dialoguegeneric__00013ec1_1",
			"dialoguege__00023c15_1",
			"dialoguege__00023c16_1",
			"dialoguege__00023c14_1",
			"dialoguege__0009bc04_1",
			"dialoguegeneric__0009687e_1",
			"dialoguegeneric__00096846_1",
			"dialoguegeneric__00096838_1",
			"dialoguegeneric__00096849_1",
		};
		constexpr std::string_view kScreamLines[] = {
			"dialoguegeneric__00017703_1",
			"dialoguegeneric__00017702_1",
			"dialoguegeneric__000dba3a_1",
			"dialoguegeneric__0006f41e_1",
			"dialoguegeneric__00042ed4_1",
			"dialoguegeneric__0002e357_1",
		};
		// Out-of-breath grunts; only the generic voices have them (argonian, elfhaughty, eventoned, khajiit, orc).
		constexpr std::string_view kBreathLines[] = {
			"dialoguegeneric__0005dd78_1",
			"dialoguegeneric__000604e5_1",
			"dialoguegeneric__000604e6_1",
		};

		constexpr std::array<std::span<const std::string_view>, kPoolCount> kPools{
			std::span<const std::string_view>{ kHelpLines },
			std::span<const std::string_view>{ kWitnessLines },
			std::span<const std::string_view>{ kGuardLines },
			std::span<const std::string_view>{ kAllyLines },
			std::span<const std::string_view>{ kPleadLines },
			std::span<const std::string_view>{ kMildLines },
			std::span<const std::string_view>{ kDefyLines },
			std::span<const std::string_view>{ kScreamLines },
			std::span<const std::string_view>{ kBreathLines },
		};

		// ------------------------------------------------------------ random (main thread)
		std::mt19937& Rng()
		{
			static std::mt19937 rng{ std::random_device{}() };
			return rng;
		}

		float RandF(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(Rng()); }
		int RandI(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(Rng()); }

		std::string Lower(std::string s)
		{
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		// ------------------------------------------------------------ voice types
		bool IsFemale(RE::Actor* a)
		{
			auto* base = a->GetActorBase();
			return base && base->GetSex() == RE::SEX::kFemale;
		}

		// The NPC record's voice, else the race default, read as fields the way OStim does. (Not
		// TESBoundObject::GetObjectVoiceType: that vtable slot returned garbage on 1.6.1170 and
		// crashed the game.)
		std::string OwnVoiceType(RE::Actor* a, bool female)
		{
			RE::BGSVoiceType* vt = nullptr;
			if (auto* base = a->GetActorBase()) vt = base->voiceType;
			if (!vt) {
				if (auto* race = a->GetRace()) vt = race->defaultVoiceTypes[female ? RE::SEX::kFemale : RE::SEX::kMale];
			}
			const char* id = vt ? vt->GetFormEditorID() : nullptr;
			return id && *id ? Lower(id) : std::string{};
		}

		// The generic voices of the actor's race, for lines their own voice type never recorded.
		std::vector<std::string_view> RaceVoices(RE::Actor* a)
		{
			auto* race = a->GetRace();
			const char* rid = race ? race->GetFormEditorID() : nullptr;
			const std::string r = rid ? Lower(rid) : std::string{};
			if (r.contains("orc")) return { "orc" };
			if (r.contains("khajiit")) return { "khajiit" };
			if (r.contains("argonian")) return { "argonian" };
			if (r.contains("highelf") || r.contains("woodelf")) return { "elfhaughty" };
			if (r.contains("darkelf")) return { "darkelf", "elfhaughty" };
			return {};
		}

		// Voice types to try, in order: the actor's own, the guard voice for guards, the race's
		// generic voice, then even-toned.
		std::vector<std::string> Chain(RE::Actor* a, bool a_guard)
		{
			const bool female = IsFemale(a);
			const std::string sex = female ? "female" : "male";
			std::vector<std::string> out;
			auto add = [&](std::string v) {
				if (!v.empty() && std::ranges::find(out, v) == out.end()) out.push_back(std::move(v));
			};
			add(OwnVoiceType(a, female));
			if (a_guard) add(female ? "femalenord" : "maleguard");
			for (auto r : RaceVoices(a)) add(sex + std::string(r));
			add(sex + "eventoned");
			return out;
		}

		// ------------------------------------------------------------ clips
		struct Clip
		{
			bool exists = false;
			float seconds = 0.0f;
		};

		std::mutex g_clipLock;
		std::unordered_map<std::string, Clip> g_clips;  // "<voice type>\<stem>"

		std::uint32_t U32(const char* p)
		{
			std::uint32_t v;
			std::memcpy(&v, p, 4);
			return v;
		}

		// A .fuz is "FUZE", version, lip size, the .lip, then an xWMA RIFF. The dpds chunk's last
		// entry is the decoded byte count, which gives the length exactly.
		Clip ReadClip(const std::string& a_vt, std::string_view a_stem)
		{
			Clip c;
			RE::BSResourceNiBinaryStream s(std::format("Sound\\Voice\\Skyrim.esm\\{}\\{}.fuz", a_vt, a_stem));
			if (!s.good()) return c;
			c.exists = true;
			c.seconds = 3.0f;
			char head[12];
			if (!s.read(head, 12) || std::memcmp(head, "FUZE", 4) != 0) return c;
			if (const auto lip = U32(head + 8); lip) s.seek(static_cast<std::int32_t>(lip));
			if (!s.read(head, 12) || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "XWMA", 4) != 0) return c;
			std::uint16_t channels = 0;
			std::uint32_t rate = 0, avgBytes = 0;
			for (int i = 0; i < 16; ++i) {
				char ck[12];
				if (!s.read(ck, 8)) break;
				const std::uint32_t size = U32(ck + 4);
				const std::uint32_t padded = size + (size & 1);
				if (std::memcmp(ck, "fmt ", 4) == 0 && size >= 12) {
					if (!s.read(ck, 12)) break;
					std::memcpy(&channels, ck + 2, 2);
					rate = U32(ck + 4);
					avgBytes = U32(ck + 8);
					s.seek(static_cast<std::int32_t>(padded - 12));
				} else if (std::memcmp(ck, "dpds", 4) == 0 && size >= 4 && channels && rate) {
					s.seek(static_cast<std::int32_t>(size - 4));
					if (!s.read(ck, 4)) break;
					if (const auto total = U32(ck); total) {
						c.seconds = static_cast<float>(total) / (2.0f * channels * rate);
						break;
					}
					if (size & 1) s.seek(1);
				} else if (std::memcmp(ck, "data", 4) == 0) {
					if (avgBytes) c.seconds = static_cast<float>(size) / static_cast<float>(avgBytes);
					break;
				} else {
					s.seek(static_cast<std::int32_t>(padded));
				}
			}
			c.seconds = std::clamp(c.seconds, 0.4f, 15.0f);
			return c;
		}

		Clip ClipOf(const std::string& a_vt, std::string_view a_stem)
		{
			std::string key = a_vt;
			key += '\\';
			key += a_stem;
			{
				std::scoped_lock l(g_clipLock);
				if (auto it = g_clips.find(key); it != g_clips.end()) return it->second;
			}
			const Clip c = ReadClip(a_vt, a_stem);
			std::scoped_lock l(g_clipLock);
			g_clips.emplace(std::move(key), c);
			return c;
		}

		struct Line
		{
			std::string vt;
			std::string_view stem;
			float seconds = 0.0f;
			Pool pool = kBreath;
		};

		std::optional<Line> Find(const std::vector<std::string>& a_chain, std::string_view a_stem, Pool a_pool)
		{
			for (const auto& vt : a_chain) {
				if (const auto c = ClipOf(vt, a_stem); c.exists) return Line{ vt, a_stem, c.seconds, a_pool };
			}
			return std::nullopt;
		}

		// A random line of the pool that this voice has, not a_avoid unless it is the only one.
		std::optional<Line> Pick(const std::vector<std::string>& a_chain, Pool a_pool, std::string_view a_avoid = {})
		{
			const auto stems = kPools[a_pool];
			const int n = static_cast<int>(stems.size());
			const int first = RandI(0, n - 1);
			std::optional<Line> repeat;
			for (int i = 0; i < n; ++i) {
				const auto stem = stems[(first + i) % n];
				auto line = Find(a_chain, stem, a_pool);
				if (!line) continue;
				if (stem == a_avoid && n > 1) {
					repeat = std::move(line);
					continue;
				}
				return line;
			}
			return repeat;
		}

		// The console's SpeakSound, as OStim's dialogue does: the voice plays from the actor with lip-sync.
		void Speak(RE::Actor* a, const Line& a_line)
		{
			const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
			const auto script = factory ? factory->Create() : nullptr;
			if (!script) return;
			a->StopCurrentDialogue();
			script->SetCommand(std::format("SpeakSound \"voice/Skyrim.esm/{}/{}.fuz\"", a_line.vt, a_line.stem));
			script->CompileAndRun(a);
			delete script;
		}

		// ------------------------------------------------------------ victims
		enum class Phase : int { kHelp, kLines, kShock, kBroken };
		constexpr const char* kPhaseNames[] = { "about to cry for help", "struggling", "screaming", "shut down" };

		struct Victim
		{
			RE::ActorHandle handle;
			std::string name;
			int thread = -1;
			Reaction reaction = Reaction::kBalanced;
			RE::ActorHandle aggressor;
			std::vector<std::string> chain;
			Phase phase = Phase::kHelp;
			float start = 0.0f;
			float next = 0.0f;
			float busyUntil = 0.0f;
			float brokenAt = 0.0f;
			float resolve = 1.0f;  // falls over the scene: fewer curses, more pleading and breathing
			std::string_view lastStem;
			bool helped = false;
			bool muted = false;
			float calmUntil = 0.0f;  // after the cry is answered: keep the victim out of the fight
			float nextCalm = 0.0f;
		};

		// A victim whose thread restarts (a follower joining) keeps going where they were.
		struct Carry
		{
			float when = 0.0f;
			float resolve = 1.0f;
			bool helped = false;
			bool broken = false;
		};

		constexpr float kCarrySeconds = 60.0f;
		constexpr float kResolveSeconds = 120.0f;
		constexpr float kHelpWindow = 20.0f;  // the mouth stayed busy this long: no cry for help

		std::mutex g_lock;  // after Scenes::Lock() and Settings::lock
		std::unordered_map<RE::FormID, Victim> g_victims;
		std::unordered_map<RE::FormID, Carry> g_carry;
		std::string g_last = "none yet";
		std::string g_lastResponse = "none yet";
		bool g_voiceFound = false;
		float g_lastTick = 0.0f;

		// Read by the audio thread.
		std::atomic_int g_silenceCount = 0;
		std::mutex g_nodeLock;
		std::vector<RE::NiAVObject*> g_victimNodes;
		std::atomic_bool g_hooked = false;
		std::atomic<std::uint32_t> g_mutedAtStart = 0;
		std::atomic<std::uint32_t> g_mutedLate = 0;

		struct Config
		{
			bool enabled = false;
			bool noMoans = true;
			bool mute = true;
			bool help = true;
			bool scream = true;
			int mode = 2;
			int responders = 2;
			float interval = 9.0f;
			float range = 2048.0f;
		};

		Config ReadConfig()
		{
			std::scoped_lock l(Settings::lock);
			Config c;
			c.enabled = VS::bEnabled && Settings::General::bEnabled;
			c.noMoans = VS::bVictimNoMoans;
			c.mute = VS::bMuteDialogue;
			c.help = VS::bCallForHelp;
			c.scream = VS::bBreakScream;
			c.mode = VS::iVictimVoice;
			c.responders = VS::iResponders;
			c.interval = VS::fInterval;
			c.range = VS::fResponderRange;
			return c;
		}

		// What a victim says next, from their personality and how much fight is left.
		Pool ChoosePool(const Victim& v, float now)
		{
			// Exhaustion: the longer it goes on, the more often there are no words left.
			if (RandF(0.0f, 1.0f) < (1.0f - v.resolve) * 0.35f) return kBreath;
			const float p = RandF(0.0f, 1.0f);
			const float elapsed = now - v.start;
			switch (v.reaction) {
			case Reaction::kDefiance:
				return p < 0.2f + 0.8f * v.resolve ? kDefy : kPlead;
			case Reaction::kFear:
				return elapsed < 15.0f && p < 0.3f ? kMild : kPlead;
			case Reaction::kPanic:
				return kPlead;
			case Reaction::kNumb:
				return p < 0.5f ? kBreath : kPlead;
			default:  // balanced: protest first, then fight while it lasts, then beg
				if (elapsed < 20.0f) return p < 0.6f ? kMild : kPlead;
				if (v.resolve > 0.5f && p < 0.3f) return kDefy;
				return kPlead;
			}
		}

		float IntervalScale(Reaction r)
		{
			switch (r) {
			case Reaction::kPanic: return 0.7f;
			case Reaction::kNumb: return 1.8f;
			default: return 1.0f;
			}
		}

		// ------------------------------------------------------------ responders
		struct Responder
		{
			RE::Actor* actor;
			float dist;
		};

		bool Friendly(RE::FIGHT_REACTION r) { return r == RE::FIGHT_REACTION::kAlly || r == RE::FIGHT_REACTION::kFriend; }

		void SpeakLater(RE::Actor* a, Pool a_pool, bool a_guard, float a_delay)
		{
			Scheduler::After(a_delay, [h = a->GetHandle(), a_pool, a_guard]() {
				auto actor = h.get();
				if (!actor || actor->IsDead()) return;
				if (auto line = Pick(Chain(actor.get(), a_guard), a_pool)) Speak(actor.get(), *line);
			});
		}

		// Vanilla's bounty for assault (iCrimeGoldAttack, 40).
		std::int32_t AssaultGold()
		{
			auto* settings = RE::GameSettingCollection::GetSingleton();
			auto* s = settings ? settings->GetSetting("iCrimeGoldAttack") : nullptr;
			return s ? s->GetInteger() : 40;
		}

		// Who heard the cry. Guards (and with iResponders 2, the victim's friends and allies) within
		// range come for the aggressor. When that is the player and a guard answered, the assault
		// also goes on their bounty in the victim's hold, so yielding to the guards ends it the
		// vanilla way. The victim herself is left out: ODragonSeed's SendAssaultAlarm is raised by
		// the victim and put her in combat too, which pulled her out of the scene. A bystander who
		// is neither guard nor ally shouts for the guards.
		void Respond(RE::ActorHandle a_victim, RE::ActorHandle a_aggressor)
		{
			const auto c = ReadConfig();
			if (!c.enabled || c.responders <= 0) return;
			auto victim = a_victim.get();
			auto aggressor = a_aggressor.get();
			if (!victim || !aggressor || victim->IsDead()) return;
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) return;

			std::scoped_lock sl(Scenes::Lock());
			if (!Scenes::InAnyScene(victim.get())) return;  // it is already over
			const bool playerVictim = victim->IsPlayerRef();
			const bool playerAggressor = aggressor->IsPlayerRef();
			const auto origin = victim->GetPosition();
			std::vector<Responder> guards, allies;
			RE::Actor* witness = nullptr;
			float witnessDist = c.range;
			lists->ForEachHighActor([&](RE::Actor* x) {
				if (!x || x == victim.get() || x == aggressor.get() || x->IsPlayerRef()) return RE::BSContainer::ForEachResult::kContinue;
				if (x->IsDead() || x->IsChild() || !x->Is3DLoaded() || Scenes::InAnyScene(x)) return RE::BSContainer::ForEachResult::kContinue;
				const float d = x->GetPosition().GetDistance(origin);
				if (d > c.range) return RE::BSContainer::ForEachResult::kContinue;
				if (x->IsGuard()) {
					guards.push_back({ x, d });
					return RE::BSContainer::ForEachResult::kContinue;
				}
				bool ally = false;
				if (c.responders >= 2) {
					ally = playerVictim ? x->IsPlayerTeammate() : Friendly(x->GetFactionReaction(victim.get()));
					// Not someone on the aggressor's side.
					if (ally && (playerAggressor ? x->IsPlayerTeammate() : Friendly(x->GetFactionReaction(aggressor.get())))) ally = false;
				}
				if (ally) {
					allies.push_back({ x, d });
				} else if (d < witnessDist && !x->IsInCombat() && Face::Engine::IsHuman(x) && x->GetFactionReaction(victim.get()) != RE::FIGHT_REACTION::kEnemy) {
					witness = x;
					witnessDist = d;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			auto byDistance = [](const Responder& a, const Responder& b) { return a.dist < b.dist; };
			std::ranges::sort(guards, byDistance);
			std::ranges::sort(allies, byDistance);
			if (guards.size() > 4) guards.resize(4);
			if (allies.size() > 4) allies.resize(4);

			if (guards.empty() && allies.empty() && !witness) {
				std::scoped_lock l(g_lock);
				g_lastResponse = std::format("{}: nobody in range", victim->GetDisplayFullName());
				return;
			}
			for (const auto& r : guards) Papyrus::StartCombat(r.actor, aggressor.get());
			for (const auto& r : allies) Papyrus::StartCombat(r.actor, aggressor.get());
			bool bounty = false;
			if (playerAggressor && !guards.empty()) {
				if (auto* crime = victim->GetCrimeFaction()) {
					Papyrus::AddCrimeGold(crime, AssaultGold(), true);
					bounty = true;
				}
			}
			if (!guards.empty()) SpeakLater(guards.front().actor, kGuard, true, 0.0f);
			if (witness) SpeakLater(witness, kWitness, false, 0.4f);
			if (!allies.empty()) SpeakLater(allies.front().actor, kAlly, false, 0.8f);
			const auto text = std::format("{}: {} guard(s), {} all(y/ies){}{}", victim->GetDisplayFullName(), guards.size(), allies.size(),
				witness ? ", a witness" : "", bounty ? " (assault bounty)" : "");
			logger::info("Victim voice: cry for help answered by {}", text);
			std::scoped_lock l(g_lock);
			g_lastResponse = text;
		}

		// ------------------------------------------------------------ muting OStim's moans
		// SEH: nothing with a destructor may live in these functions.
		int FollowChain(RE::BSAudioManager* am, RE::BSGameSound* snd, RE::NiAVObject** out, int max) noexcept
		{
			int n = 0;
			__try {
				std::uint32_t id = 0;
				bool found = false;
				for (auto& [soundID, s] : am->activeSounds) {
					if (s == snd) {
						id = soundID;
						found = true;
						break;
					}
				}
				if (!found) return 0;
				auto it = am->movingSounds.find(id);
				if (it == am->movingSounds.end()) return 0;
				for (RE::NiAVObject* node = it->second.get(); node && n < max; node = node->parent) out[n++] = node;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return n;
			}
			return n;
		}

		struct Moan
		{
			std::uint32_t soundID;
			RE::NiAVObject* node;
			RE::BSResource::ID res;
		};

		int CollectLoudMoans(RE::BSAudioManager* am, Moan* out, int max) noexcept
		{
			int n = 0;
			__try {
				for (auto& [soundID, nodePtr] : am->movingSounds) {
					if (n >= max) break;
					RE::NiAVObject* node = nodePtr.get();
					if (!node) continue;
					auto it = am->activeSounds.find(soundID);
					if (it == am->activeSounds.end() || !it->second) continue;
					RE::BSGameSound* snd = it->second;
					if (snd->volume <= 0.001f) continue;
					out[n].soundID = soundID;
					out[n].node = node;
					std::memcpy(&out[n].res, &snd->resourceID, sizeof(RE::BSResource::ID));
					++n;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return n;
			}
			return n;
		}

		// Audio side: a voice-set sound starting on a node of a victim's body.
		bool ShouldSilence(RE::BSGameSound* snd)
		{
			if (!LipSync::IsMoanResource(snd->resourceID)) return false;
			auto* am = RE::BSAudioManager::GetSingleton();
			if (!am) return false;
			RE::NiAVObject* chain[24];
			const int n = FollowChain(am, snd, chain, 24);
			if (n == 0) return false;
			std::scoped_lock l(g_nodeLock);
			for (int i = 0; i < n; ++i) {
				if (std::ranges::find(g_victimNodes, chain[i]) != g_victimNodes.end()) return true;
			}
			return false;
		}

		struct PlayHook
		{
			static void thunk(RE::BSGameSound* a_sound)
			{
				const bool mute = a_sound && g_silenceCount.load(std::memory_order_relaxed) > 0 && ShouldSilence(a_sound);
				if (mute) a_sound->volume = 1e-5f;
				func(a_sound);
				if (mute) {
					a_sound->SetVolume(0.0f);
					g_mutedAtStart.fetch_add(1, std::memory_order_relaxed);
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Main thread: anything that started before the victim was known, or that the start hook
		// could not place, is turned down here. A moan is never stopped, only muted, so OStim's
		// own bookkeeping sees it finish normally.
		void SweepMoans(const std::unordered_set<RE::FormID>& a_victims)
		{
			auto* am = RE::BSAudioManager::GetSingleton();
			if (!am || a_victims.empty()) return;
			std::array<Moan, 64> moans{};
			const int n = CollectLoudMoans(am, moans.data(), static_cast<int>(moans.size()));
			for (int i = 0; i < n; ++i) {
				if (!LipSync::IsMoanResource(moans[i].res)) continue;
				auto* owner = LipSync::ActorOf(moans[i].node);
				if (!owner || !a_victims.contains(owner->GetFormID())) continue;
				RE::BSSoundHandle h;
				h.soundID = moans[i].soundID;
				h.assumeSuccess = false;
				h.state = RE::BSSoundHandle::AssumedState::kPlaying;
				h.SetVolume(0.0f);
				g_mutedLate.fetch_add(1, std::memory_order_relaxed);
			}
		}

		// ------------------------------------------------------------ tick
		enum class Act { kSpeak, kMute, kRespond, kCalm };

		struct Action
		{
			Act act;
			RE::ActorHandle who;
			Line line;
			RE::ActorHandle other;
		};

		struct Seen
		{
			RE::FormID id = 0;
			RE::ActorHandle handle;
			int thread = -1;
			RE::Actor* actor = nullptr;
			bool alive = false;
			bool broken = false;
			bool mouthBusy = false;
			bool inCombat = false;
		};

		void Forget(RE::FormID a_id, const Victim& v, float now)
		{
			g_carry[a_id] = { now, v.resolve, v.helped, v.phase >= Phase::kShock };
		}

		// One victim's next step. g_lock held.
		void Step(Victim& v, const Seen& in, const Config& c, float now, std::vector<Action>& out)
		{
			if (in.broken && v.phase < Phase::kShock) {
				v.phase = Phase::kBroken;
				v.brokenAt = now;
			}
			if (c.mute && !v.muted) {
				out.push_back({ Act::kMute, v.handle, {}, {} });
				v.muted = true;
			}
			if (c.mode <= 0 || now < v.next || now < v.busyUntil) return;

			std::optional<Line> line;
			float gap = c.interval * IntervalScale(v.reaction) * RandF(0.6f, 1.4f);
			switch (v.phase) {
			case Phase::kHelp:
				if (c.mode >= 2 && c.help && !v.helped && now - v.start < kHelpWindow) {
					if (in.mouthBusy) {
						v.next = now + 1.0f;
						return;
					}
					line = Pick(v.chain, kHelp);
					v.helped = true;
					if (line && c.responders > 0 && v.aggressor) {
						out.push_back({ Act::kRespond, v.handle, {}, v.aggressor });
						v.calmUntil = now + 45.0f;
					}
				}
				v.phase = Phase::kLines;
				if (!line) gap = RandF(1.0f, 3.0f);
				break;
			case Phase::kLines:
				if (in.mouthBusy) {  // mouth full: nothing (the muffled moans are muted too)
					v.next = now + 1.5f;
					return;
				}
				{
					const Pool pool = c.mode >= 2 ? ChoosePool(v, now) : kBreath;
					line = Pick(v.chain, pool, v.lastStem);
					if (!line && pool != kBreath) line = Pick(v.chain, kBreath, v.lastStem);
					if (pool != kBreath) v.resolve = std::max(0.0f, v.resolve - 0.02f);
				}
				break;
			case Phase::kShock:
				if (!in.mouthBusy) line = Pick(v.chain, c.mode >= 2 && c.scream ? kScream : kBreath);
				v.phase = Phase::kBroken;
				v.brokenAt = now;
				gap = RandF(0.3f, 0.8f);
				break;
			case Phase::kBroken:
				if (in.mouthBusy) {
					v.next = now + 1.5f;
					return;
				}
				line = Pick(v.chain, kBreath, v.lastStem);
				// Hard breathing that slowly settles, never words.
				gap = RandF(0.4f, 1.6f) + std::min(3.0f, (now - v.brokenAt) / 30.0f);
				break;
			}
			if (!line) {
				v.next = now + gap;
				return;
			}
			v.lastStem = line->stem;
			v.busyUntil = now + line->seconds;
			v.next = v.busyUntil + gap;
			g_last = std::format("{}: {} ({} {})", v.name, kPoolNames[line->pool], line->vt, line->stem);
			out.push_back({ Act::kSpeak, v.handle, std::move(*line), {} });
		}

		void Publish(std::vector<RE::NiAVObject*> a_nodes, int a_count)
		{
			{
				std::scoped_lock l(g_nodeLock);
				g_victimNodes = std::move(a_nodes);
			}
			g_silenceCount.store(a_count, std::memory_order_relaxed);
		}

		void TestPool(RE::Actor* a, Pool a_pool)
		{
			if (!a) {
				Papyrus::Notify("OSED Reborn: aim at an actor first");
				return;
			}
			const auto chain = Chain(a, a->IsGuard());
			const auto line = Pick(chain, a_pool);
			if (!line) {
				Papyrus::Notify(std::format("OSED Reborn: no {} line for voice {}", kPoolNames[a_pool], chain.empty() ? "?" : chain.front()));
				return;
			}
			Speak(a, *line);
			std::scoped_lock l(g_lock);
			g_last = std::format("test on {}: {} ({} {}, {:.1f} s)", a->GetDisplayFullName(), kPoolNames[a_pool], line->vt, line->stem, line->seconds);
		}
	}

	void InstallHooks()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_BSXAudio2GameSound[0] };
		PlayHook::func = vtbl.write_vfunc(0x15, PlayHook::thunk);
		g_hooked = true;
		logger::info("Victim voice: sound start hook installed");
	}

	void OnDataLoaded()
	{
		// The vanilla voice archive must be readable for any of this to be heard.
		g_voiceFound = ClipOf("femaleeventoned", kHelpLines[0]).exists || ClipOf("maleeventoned", kHelpLines[0]).exists;
		if (g_voiceFound) logger::info("Victim voice: vanilla voice lines found");
		else logger::warn("Victim voice: vanilla voice lines not found (Skyrim - Voices_en0.bsa); the victim stays silent");
	}

	void Tick()
	{
		const auto c = ReadConfig();
		const float now = Scenes::Now();
		const float dt = g_lastTick > 0.0f ? std::clamp(now - g_lastTick, 0.0f, 1.0f) : 0.05f;
		g_lastTick = now;
		if (!c.enabled) {
			bool had;
			{
				std::scoped_lock l(g_lock);
				had = !g_victims.empty();
				g_victims.clear();
			}
			if (had || g_silenceCount.load(std::memory_order_relaxed)) Publish({}, 0);
			return;
		}

		std::vector<Action> actions;
		std::vector<RE::NiAVObject*> nodes;
		std::unordered_set<RE::FormID> silenced;
		{
			std::scoped_lock sl(Scenes::Lock());
			std::vector<Seen> seen;
			{
				std::scoped_lock l(g_lock);
				if (g_victims.empty() && !g_silenceCount.load(std::memory_order_relaxed)) return;
				for (const auto& [id, v] : g_victims) seen.push_back({ id, v.handle, v.thread });
			}
			// Facts about each victim, gathered outside g_lock (the face engine takes its own locks).
			for (auto& in : seen) {
				in.actor = in.handle.get().get();
				auto* t = in.actor ? Scenes::ThreadOf(in.actor) : nullptr;
				auto* s = t ? t->Find(in.actor) : nullptr;
				in.alive = s && t->active && t->id == in.thread && !in.actor->IsDead();
				if (!in.alive) continue;
				in.broken = s->broken;
				in.mouthBusy = Face::Engine::MouthYielded(*t, *s, in.actor);
				in.inCombat = in.actor->IsInCombat();
				if (c.noMoans) {
					silenced.insert(in.id);
					for (auto* n : { in.actor->Get3D(), in.actor->Get3D1(false), in.actor->Get3D1(true) }) {
						if (n && std::ranges::find(nodes, n) == nodes.end()) nodes.push_back(n);
					}
				}
			}
			{
				std::scoped_lock l(g_lock);
				for (const auto& in : seen) {
					auto it = g_victims.find(in.id);
					if (it == g_victims.end()) continue;
					if (!in.alive) {
						Forget(in.id, it->second, now);
						g_victims.erase(it);
						continue;
					}
					auto& v = it->second;
					v.resolve = std::max(0.0f, v.resolve - dt / kResolveSeconds);
					// The fight we started is between the responders and the aggressor. A victim who
					// gets drawn in (helping an allied guard) is taken back out, so she stays in the scene.
					if (in.inCombat && now < v.calmUntil && now >= v.nextCalm) {
						actions.push_back({ Act::kCalm, v.handle, {}, {} });
						v.nextCalm = now + 1.0f;
					}
					if (g_voiceFound) Step(v, in, c, now, actions);
				}
			}
			for (auto& a : actions) {
				auto who = a.who.get();
				if (!who) continue;
				switch (a.act) {
				case Act::kSpeak:
					Speak(who.get(), a.line);
					Face::Engine::SetExternalMouth(who.get(), now + a.line.seconds);
					break;
				case Act::kMute:
					Papyrus::MuteOStim(who.get());
					break;
				case Act::kCalm:
					Papyrus::StopCombat(who.get());
					logger::info("Victim voice: {:08X} {} was drawn into the fight; taken back out", who->GetFormID(), who->GetDisplayFullName());
					break;
				case Act::kRespond:
					Scheduler::After(1.5f, [v = a.who, g = a.other]() { Respond(v, g); });
					break;
				}
			}
		}
		const int count = static_cast<int>(silenced.size());
		Publish(std::move(nodes), count);
		if (count) SweepMoans(silenced);
	}

	void Clear()
	{
		{
			std::scoped_lock l(g_lock);
			g_victims.clear();
			g_carry.clear();
		}
		Publish({}, 0);
	}

	void Sync(Scenes::Thread& t)
	{
		const auto c = ReadConfig();
		if (!c.enabled || !t.active) return;
		const float now = Scenes::Now();
		// The aggressor the guards go after: the player if they take part, else the first non-victim.
		RE::Actor* aggressor = nullptr;
		for (auto& s : t.slots) {
			if (s.victim || s.broken) continue;
			auto* a = s.Get();
			if (a && (!aggressor || s.player)) aggressor = a;
		}
		const RE::ActorHandle aggressorHandle = aggressor ? aggressor->GetHandle() : RE::ActorHandle{};

		std::vector<std::pair<RE::FormID, Victim>> fresh;
		for (auto& s : t.slots) {
			if (!s.victim && !s.broken) continue;
			auto* a = s.Get();
			if (!a || a->IsChild() || !Face::Engine::IsHuman(a)) continue;
			{
				std::scoped_lock l(g_lock);
				if (auto it = g_victims.find(s.id); it != g_victims.end()) {
					auto& v = it->second;
					v.thread = t.id;
					if (s.broken && v.phase < Phase::kShock) {
						v.phase = Phase::kBroken;
						v.brokenAt = now;
					}
					if (!v.aggressor.get()) v.aggressor = aggressorHandle;
					continue;
				}
			}
			Victim v;
			v.handle = s.handle;
			v.name = a->GetDisplayFullName();
			v.thread = t.id;
			v.reaction = Face::Engine::VictimReaction(Face::Engine::Archetype(a));
			v.aggressor = aggressorHandle;
			v.chain = Chain(a, false);
			v.start = now;
			v.next = now + RandF(0.4f, 1.2f);  // the cry comes at once
			if (s.broken) {
				v.phase = Phase::kBroken;
				v.brokenAt = now;
			}
			fresh.emplace_back(s.id, std::move(v));
		}
		if (fresh.empty()) return;
		std::scoped_lock l(g_lock);
		std::erase_if(g_carry, [&](const auto& kv) { return now - kv.second.when >= kCarrySeconds; });
		for (auto& [id, v] : fresh) {
			if (auto it = g_carry.find(id); it != g_carry.end()) {
				v.helped = it->second.helped;
				v.resolve = it->second.resolve;
				if (v.phase < Phase::kShock) v.phase = it->second.broken ? Phase::kBroken : Phase::kLines;
				v.brokenAt = now;
				v.next = now + RandF(1.0f, 3.0f);
				g_carry.erase(it);
			}
			logger::info("thread {}: {:08X} {} is the victim ({}, voice {})", t.id, id, v.name, Face::Engine::ReactionName(v.reaction),
				v.chain.empty() ? "?" : v.chain.front());
			g_victims.emplace(id, std::move(v));
		}
	}

	void OnBreak(Scenes::Thread&, RE::Actor* a)
	{
		if (!a) return;
		std::scoped_lock l(g_lock);
		auto it = g_victims.find(a->GetFormID());
		if (it == g_victims.end() || it->second.phase >= Phase::kShock) return;
		auto& v = it->second;
		v.phase = Phase::kShock;  // scream now, over whatever was being said
		v.next = 0.0f;
		v.busyUntil = 0.0f;
	}

	void OnSceneEnd(Scenes::Thread& t)
	{
		const float now = Scenes::Now();
		std::scoped_lock l(g_lock);
		for (auto it = g_victims.begin(); it != g_victims.end();) {
			if (it->second.thread != t.id) {
				++it;
				continue;
			}
			Forget(it->first, it->second, now);
			it = g_victims.erase(it);
		}
	}

	bool IsSilenced(RE::Actor* a)
	{
		if (!a || g_silenceCount.load(std::memory_order_relaxed) == 0) return false;
		std::scoped_lock l(g_lock);
		return g_victims.contains(a->GetFormID());
	}

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		std::string out = std::format("{}; {} victim(s); OStim moans muted: {} at start, {} late{}\nLast line: {}\nLast cry answered: {}",
			g_voiceFound ? "vanilla voice lines found" : "vanilla voice lines NOT found", g_victims.size(), g_mutedAtStart.load(), g_mutedLate.load(),
			g_hooked ? "" : " (start hook missing)", g_last, g_lastResponse);
		for (const auto& [id, v] : g_victims) {
			out += std::format("\n  {}: {} ({}), resolve {:.0f}%, voice {}", v.name, kPhaseNames[static_cast<int>(v.phase)], Face::Engine::ReactionName(v.reaction),
				v.resolve * 100.0f, v.chain.empty() ? "?" : v.chain.front());
		}
		return out;
	}

	void TestHelp(RE::Actor* a) { TestPool(a, kHelp); }

	void TestLine(RE::Actor* a)
	{
		if (!a) return TestPool(a, kPlead);
		Victim v;
		v.reaction = Face::Engine::VictimReaction(Face::Engine::Archetype(a));
		v.start = Scenes::Now() - 30.0f;
		v.resolve = RandF(0.3f, 1.0f);
		Pool pool = ChoosePool(v, Scenes::Now());
		if (pool == kBreath) pool = kPlead;
		TestPool(a, pool);
	}

	void TestScream(RE::Actor* a) { TestPool(a, kScream); }

	void TestBreath(RE::Actor* a) { TestPool(a, kBreath); }
}
