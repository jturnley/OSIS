#include "Arousal.h"
#include "Body.h"
#include "Compat.h"
#include "Face/Engine.h"
#include "Face/Output.h"
#include "Hooks.h"
#include "LipSync.h"
#include "OStimData.h"
#include "Scenes.h"
#include "Scheduler.h"
#include "Serialization.h"
#include "Settings.h"
#include "Skin.h"
#include "SpellCast.h"
#include "UI.h"
#include "Voice.h"

namespace Scheduler
{
	namespace
	{
		std::atomic_bool g_pending = false;
		std::mutex g_lock;
		std::vector<std::pair<float, std::function<void()>>> g_delayed;
		float g_lastTick = 0.0f;

		void MainTick()
		{
			g_pending = false;
			const float tickNow = Scenes::Now();
			const float tickDt = g_lastTick > 0.0f ? tickNow - g_lastTick : 0.05f;
			g_lastTick = tickNow;
			// Without the NPC animation hook, faces still get written, just at 20 Hz.
			if (!Hooks::NPCHooked() && !RE::UI::GetSingleton()->GameIsPaused()) Face::Output::UpdateNPCs(tickDt);
			SpellCast::Tick();
			Scenes::Tick();
			LipSync::Poll();
			Voice::Tick();
			Skin::Tick();
			Arousal::Tick();

			std::vector<std::function<void()>> due;
			{
				std::scoped_lock l(g_lock);
				const float now = Scenes::Now();
				for (auto it = g_delayed.begin(); it != g_delayed.end();) {
					if (it->first <= now) {
						due.push_back(std::move(it->second));
						it = g_delayed.erase(it);
					} else {
						++it;
					}
				}
			}
			for (auto& fn : due) fn();
		}
	}

	void Start()
	{
		std::thread([]() {
			for (;;) {
				std::this_thread::sleep_for(50ms);
				if (!g_pending.exchange(true)) SKSE::GetTaskInterface()->AddTask(MainTick);
			}
		}).detach();
	}

	void After(float seconds, std::function<void()> fn)
	{
		std::scoped_lock l(g_lock);
		g_delayed.emplace_back(Scenes::Now() + seconds, std::move(fn));
	}
}

namespace
{
	void InitLogger()
	{
		auto path = SKSE::log::log_directory();
		if (!path) return;
		*path /= "OSEDReborn.log";

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::debug);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S:%e] [%l] %v");
	}

	// A module that throws while starting is logged and stays inert; it must not end the game.
	void Guarded(const char* a_name, void (*a_fn)())
	{
		try {
			a_fn();
		} catch (const std::exception& e) {
			logger::error("{} failed to start and is off this session: {}", a_name, e.what());
		} catch (...) {
			logger::error("{} failed to start and is off this session", a_name);
		}
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Guarded("OStim metadata", OStimData::Init);
			Guarded("Face engine", Face::Engine::OnDataLoaded);
			Guarded("Compatibility check", Compat::Detect);
			Guarded("Living Skin", Skin::OnDataLoaded);
			Guarded("Arousal", Arousal::Init);
			Guarded("Lip-sync", LipSync::OnDataLoaded);
			Guarded("Victim voice", Voice::OnDataLoaded);
			Guarded("Scene tracking", Scenes::Init);
			Guarded("Spell-started scenes", SpellCast::Init);
			Scheduler::Start();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			Face::Output::ForgetAll();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			Scenes::OnGameLoad();
			SpellCast::Clear();
			Arousal::OnGameLoad();
			Body::ClearAll();
			Skin::ClearAll();
			Voice::Clear();
			Compat::NotifyOnce();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitLogger();
	SKSE::Init(a_skse, { .trampoline = true, .trampolineSize = 64 });
	Settings::Load();
	logger::info("OSEDReborn v{} loading", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));

	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	Serialization::Install();
	Hooks::Install();
	UI::Register();
	return true;
}
