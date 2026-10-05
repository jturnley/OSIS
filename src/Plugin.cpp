// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

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
#include "UI.h"
#if !OSIS_LITE
#	include "SceneLock.h"
#	include "SpellCast.h"
#	include "Voice.h"
#endif

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
			// Without the animation hooks, faces still get written, just at 20 Hz.
			if (!RE::UI::GetSingleton()->GameIsPaused()) {
				if (!Hooks::NPCHooked()) Face::Output::UpdateNPCs(tickDt);
				if (!Hooks::PlayerHooked()) Face::Output::Update(RE::PlayerCharacter::GetSingleton(), tickDt);
			}
#if !OSIS_LITE
			SpellCast::Tick();
#endif
			Scenes::Tick();
			LipSync::Poll();
#if !OSIS_LITE
			Voice::Tick();
			SceneLock::Tick();
#endif
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
		*path /= "OSIS.log";

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
#if !OSIS_LITE
			Guarded("Victim voice", Voice::OnDataLoaded);
#endif
			Guarded("Scene tracking", Scenes::Init);
#if !OSIS_LITE
			Guarded("Spell-started scenes", SpellCast::Init);
#endif
			Scheduler::Start();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			Face::Output::ForgetAll();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			Scenes::OnGameLoad();
			Arousal::OnGameLoad();
			Body::ClearAll();
			Skin::ClearAll();
#if !OSIS_LITE
			SpellCast::Clear();
			Voice::Clear();
			SceneLock::Clear();
#endif
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
	Settings::EnsureFiles();
	logger::info("OSIS v{}{} loading", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."), OSIS_LITE ? "" : " (LoversLab edition)");

	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	Serialization::Install();
	Hooks::Install();
	UI::Register();
	return true;
}
