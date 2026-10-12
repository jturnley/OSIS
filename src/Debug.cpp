// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Debug.h"

#include <spdlog/sinks/base_sink.h>

#include "Arousal.h"
#include "Body.h"
#include "Compat.h"
#include "Face/Engine.h"
#include "Face/Output.h"
#include "LipSync.h"
#include "Release.h"
#include "Scenes.h"
#include "Settings.h"
#include "Skin.h"
#if !OSIS_LITE
#	include "Moans.h"
#endif

namespace Debug
{
	namespace
	{
		std::atomic_bool g_on{ false };
		float g_nextSnap = 0.0f;
		bool g_snapNow = false;

		constexpr std::array<const char*, static_cast<std::size_t>(Cat::Count)> kCatNames{
			"settings", "scene", "pers", "face", "breath", "mouth", "moan", "climax", "control", "body", "arousal", "skin", "event", "papyrus", "snap"
		};

		// A sink that holds no file until it is asked to. The logger always has it, so turning debug logging on and off needs no change to the logger
		// (changing a logger's sinks while other threads log is not safe), and a launch with debug logging off never touches the previous trace.
		class TraceSink final : public spdlog::sinks::base_sink<std::mutex>
		{
		public:
			explicit TraceSink(std::filesystem::path a_path) :
				path_(std::move(a_path)) {}

			bool Open()
			{
				std::scoped_lock l(mutex_);
				if (file_.is_open()) return true;
				std::error_code ec;
				if (!rotated_) {
					rotated_ = true;
					if (std::filesystem::exists(path_, ec) && std::filesystem::file_size(path_, ec) > 0) {
						auto previous = path_;
						previous.replace_filename("OSIS-Debug.previous.log");
						std::filesystem::remove(previous, ec);
						std::filesystem::rename(path_, previous, ec);
					}
					file_.open(path_, std::ios::binary | std::ios::trunc);
				} else {
					file_.open(path_, std::ios::binary | std::ios::app);
				}
				return file_.is_open();
			}

			void Close()
			{
				std::scoped_lock l(mutex_);
				if (!file_.is_open()) return;
				file_.flush();
				file_.close();
			}

			[[nodiscard]] const std::filesystem::path& Path() const { return path_; }

		protected:
			void sink_it_(const spdlog::details::log_msg& a_msg) override
			{
				if (!file_.is_open()) return;
				spdlog::memory_buf_t buf;
				formatter_->format(a_msg, buf);
				file_.write(buf.data(), static_cast<std::streamsize>(buf.size()));
				file_.flush();  // a trace is read after a crash: nothing may sit in a buffer
			}

			void flush_() override
			{
				if (file_.is_open()) file_.flush();
			}

		private:
			std::filesystem::path path_;
			std::ofstream file_;
			bool rotated_ = false;
		};

		std::shared_ptr<TraceSink> g_sink;

		const char* DomText(int a_dom)
		{
			switch (a_dom) {
			case Scenes::kAnticipation: return "Anticipation";
			case Scenes::kPleasure: return "Pleasure";
			case Scenes::kPlateau: return "Plateau";
			case Scenes::kDistress: return "Distress";
			case Scenes::kClimax: return "Climax";
			case Scenes::kAfterglow: return "Afterglow";
			default: return "?";
			}
		}

		void Header()
		{
			Write(Cat::Settings, std::format("OSIS {}{}{}, game {}", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."),
				Release::kLabel.empty() ? std::string() : " " + std::string(Release::kLabel), OSIS_LITE ? "" : " (LoversLab edition)",
				REL::Module::get().version().string(".")));
			std::string stood;
			for (int m = 0; m < Compat::kCount; ++m) {
				if (Compat::Disabled(static_cast<Compat::Module>(m))) stood += std::format(" {}={}", m, Compat::Reason(static_cast<Compat::Module>(m)));
			}
			Write(Cat::Settings, std::format("modules standing down:{}", stood.empty() ? " none" : stood));
			for (const auto& c : Compat::Conflicts()) Write(Cat::Settings, std::format("conflict: {}", c));
			for (const auto& [key, value] : Settings::DumpAll()) Write(Cat::Settings, std::format("{} = {}", key, value));
		}

		float Interval()
		{
			std::scoped_lock l(Settings::lock);
			return std::clamp(Settings::General::fDebugSnapshotSeconds, 0.25f, 30.0f);
		}

		// What one snapshot needs from a scene, copied under the scene lock; the modules are asked about each actor after it is released, so no module's lock is
		// ever taken while this thread holds the scene's.
		struct ActorRow
		{
			RE::ActorHandle handle;
			std::string line;   // the slot's own state
			bool painted = false;
		};
		struct ThreadRow
		{
			std::string line;
			std::vector<ActorRow> actors;
		};

		std::string ThreadLine(const Scenes::Thread& t)
		{
			std::string roster;
			for (const auto& s : t.slots) roster += std::format(" {}={}", s.pos, s.name);
			const float age = t.start > 0.0f ? Scenes::Now() - t.start : 0.0f;
			return std::format(
				"T{} scene={} ({}) speed={}/{} t={:.0f}s consent={} rough={} forced={} victimKnown={} spell={} accepted={} oral={} loving={} | orgasm={} count={} afterglow={} plateau={} | roster:{}",
				t.id, t.sceneID, std::string(t.meta ? SceneText(t.meta) : std::string("no data")),
				t.speed, t.maxSpeed, age, t.consent, t.toneRough, t.toneForced, t.victimKnown, t.spellNonConsent, t.acceptedBySubmissive, t.sceneOral, t.toneLoving,
				t.orgasm, t.orgCount, t.afterglow, t.plateau, roster);
		}

		std::string SlotLine(const Scenes::Thread& t, const Scenes::Slot& s, RE::Actor* a)
		{
			return std::format(
				"T{} {} {}{} | dom={} enj={} raw={} phrase={} rate=x{:.2f} | owners face={} mouth={} eye={} head={} | climaxing={} orgasms={} stall={} broken={} victim={} | "
				"ppaYield={} tongue={}/{} exprOverride={} takenOver={} extMouth={} | tilt={} blinkNext={:.1f}",
				t.id, Label(a), s.female ? "F" : "M", s.player ? " player" : "", DomText(s.dom), s.enj, s.raw, s.phrase,
				s.excitementFactor, s.faceOwner, s.mouthOwner, s.eyeOwner, s.headOwner, s.climaxing, s.orgasms, s.stallActive, s.broken, s.victim, s.ppaYield, s.tongueOut,
				s.tongueOn, s.exprOverride, s.takenOver, s.externalMouthUntil > Scenes::Now(), s.tilting, s.blinkNextAt > 0.0f ? s.blinkNextAt - Scenes::Now() : 0.0f);
		}

		void SnapActor(const ActorRow& row)
		{
			auto ref = row.handle.get();
			RE::Actor* a = ref.get();
			Write(Cat::Snap, row.line);
			if (!a) return;
			if (!row.painted) {
				Write(Cat::Snap, std::format("  {} is not painted (not human, a child, or NPCs are not included)", Label(a)));
				return;
			}
			// The personality as the engine sees it now (the slot's own copy is only what the Director last used).
			std::string source;
			const int pers = Face::Engine::Archetype(a, &source);
			Write(Cat::Snap, std::format("  personality {} ({})", Face::Engine::PersonalityName(pers), source));
			Write(Cat::Snap, std::format("  out  {}", Face::Output::Describe(a)));
			Write(Cat::Snap, std::format("  body {}", Body::Describe(a)));
			Write(Cat::Snap, std::format("  arousal {}", Arousal::Describe(a)));
			Write(Cat::Snap, std::format("  skin {}", Skin::Describe(a)));
			Write(Cat::Snap, std::format("  lip  {}", LipSync::Describe(a)));
#if !OSIS_LITE
			Write(Cat::Snap, std::format("  moan {}", Moans::Describe(a)));
#endif
		}
	}

	spdlog::sink_ptr MakeSink(const std::filesystem::path& a_dir)
	{
		g_sink = std::make_shared<TraceSink>(a_dir / "OSIS-Debug.log");
		g_sink->set_level(spdlog::level::debug);
		return g_sink;
	}

	void SetEnabled(bool a_on)
	{
		if (a_on && g_sink && g_sink->Open()) {
			if (!g_on.exchange(true)) {
				spdlog::set_level(spdlog::level::debug);
				Header();
				g_snapNow = true;
			}
			return;
		}
		if (g_on.exchange(false) && g_sink) {
			Write(Cat::Settings, "debug logging off");
			g_sink->Close();
		}
		spdlog::set_level(spdlog::level::info);
	}

	bool On() { return g_on.load(std::memory_order_relaxed); }

	std::string Path() { return g_sink ? g_sink->Path().string() : std::string(); }

	void Write(Cat a_cat, const std::string& a_text)
	{
		logger::debug("[{}] {}", kCatNames[static_cast<std::size_t>(a_cat)], a_text);
	}

	void SnapshotNow() { g_snapNow = true; }

	void Tick()
	{
		if (!On()) return;
		const float now = Scenes::Now();
		if (!g_snapNow && now < g_nextSnap) return;
		g_snapNow = false;
		g_nextSnap = now + Interval();
		std::vector<ThreadRow> rows;
		{
			std::scoped_lock l(Scenes::Lock());
			Scenes::ForEachThread([&](const Scenes::Thread& t) {
				ThreadRow row;
				row.line = ThreadLine(t);
				for (const auto& s : t.slots) {
					if (auto* a = s.Get()) row.actors.push_back({ a->GetHandle(), SlotLine(t, s, a), s.painted });
				}
				rows.push_back(std::move(row));
			});
		}
		for (const auto& row : rows) {
			Write(Cat::Snap, row.line);
			for (const auto& actor : row.actors) SnapActor(actor);
		}
	}

	// ---------------------------------------------------------------- formatting
	const char* PhonemeName(int a_id)
	{
		static constexpr std::array n{ "Aah", "BigAah", "BMP", "ChJSh", "DST", "Eee", "Eh", "FV", "I", "K", "N", "Oh", "OohQ", "R", "Th", "W" };
		return a_id >= 0 && a_id < static_cast<int>(n.size()) ? n[static_cast<std::size_t>(a_id)] : "?";
	}

	const char* ModifierName(int a_id)
	{
		static constexpr std::array n{ "BlinkL", "BlinkR", "BrowDownL", "BrowDownR", "BrowInL", "BrowInR", "BrowUpL", "BrowUpR", "LookDown", "LookLeft", "LookRight", "LookUp", "SquintL", "SquintR" };
		return a_id >= 0 && a_id < static_cast<int>(n.size()) ? n[static_cast<std::size_t>(a_id)] : "?";
	}

	const char* ExpressionName(int a_id)
	{
		static constexpr std::array n{ "DialogueAnger", "DialogueFear", "DialogueHappy", "DialogueSad", "DialogueSurprise", "DialoguePuzzled", "DialogueDisgusted", "MoodNeutral",
			"MoodAnger", "MoodFear", "MoodHappy", "MoodSad", "MoodSurprise", "MoodPuzzled", "MoodDisgusted", "CombatAnger", "CombatShout" };
		return a_id >= 0 && a_id < static_cast<int>(n.size()) ? n[static_cast<std::size_t>(a_id)] : "?";
	}

	std::string Channels(const float* a_values, int a_count, const char* (*a_name)(int), float a_min)
	{
		std::string out;
		for (int i = 0; i < a_count; ++i) {
			if (a_values[i] >= a_min) out += std::format("{}{}={:.2f}", out.empty() ? "" : " ", a_name(i), a_values[i]);
		}
		return out;
	}

	std::string Preset(const std::array<float, 32>& a_preset)
	{
		const auto ph = Channels(a_preset.data(), 16, PhonemeName);
		const auto mod = Channels(a_preset.data() + 16, 14, ModifierName);
		const int mood = static_cast<int>(a_preset[30] + 0.5f);
		return std::format("mouth[{}] eyes+brows[{}] mood={} x{:.2f}", ph, mod, ExpressionName(mood), a_preset[31]);
	}

	ActorRef Label(RE::Actor* a_actor)
	{
		return ActorRef{ a_actor ? a_actor->GetFormID() : 0u, a_actor };
	}

	std::string NameOf(const ActorRef& a_ref)
	{
		auto* a = a_ref.actor ? a_ref.actor : (a_ref.id ? RE::TESForm::LookupByID<RE::Actor>(a_ref.id) : nullptr);
		if (!a) return a_ref.id ? std::format("({:08X})", a_ref.id) : std::string("none");
		return std::format("{}({:08X})", a->GetDisplayFullName(), a->GetFormID());
	}

	std::string SceneText(const OStimData::ScenePtr& a_scene)
	{
		if (!a_scene) return "no scene data";
		if (!a_scene->known) return std::format("{} (no scene file found: nothing is known about it)", a_scene->id);
		std::string tags, acts, actors;
		for (const auto& t : a_scene->tags) tags += (tags.empty() ? "" : ",") + t;
		for (const auto& a : a_scene->actions) {
			acts += std::format("{}{} (actor {} target {} performer {})", acts.empty() ? "" : "; ", a.type, a.actor, a.target, a.performer);
		}
		for (std::size_t i = 0; i < a_scene->actors.size(); ++i) {
			const auto& sa = a_scene->actors[i];
			std::string atags;
			for (const auto& t : sa.tags) atags += (atags.empty() ? "" : ",") + t;
			actors += std::format("{}{}: sex={} tags=[{}] pool={}{}{}", actors.empty() ? "" : " | ", i, sa.intendedSex, atags, sa.underlyingExpression.empty() ? "-" : sa.underlyingExpression,
				sa.expressionOverride.empty() ? "" : " override=", sa.expressionOverride);
		}
		return std::format("{} tags=[{}] speeds 0..{} (default {}) actions: {} | actors: {}", a_scene->id, tags, a_scene->maxSpeed, a_scene->defaultSpeed, acts.empty() ? "none" : acts, actors);
	}

	ActorRef Label(RE::FormID a_id)
	{
		return ActorRef{ a_id, nullptr };
	}
}
