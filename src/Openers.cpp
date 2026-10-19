// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Openers.h"

#include "Debug.h"
#include "Face/Engine.h"

namespace Openers
{
	namespace
	{
		constexpr auto kRoot = "Data/Sound/OSIS/Openers";  // relative to the game folder (the virtual file system of a mod manager shows the mods' files there)
		constexpr auto kSoundRoot = "OSIS/Openers";        // the same, relative to Data/Sound, which is where SpeakSound looks

		std::mutex g_lock;
		bool g_scanned = false;
		std::unordered_map<std::string, std::string> g_voices;                     // lower-case voice type -> the folder's own name
		std::unordered_map<std::string, std::vector<std::string>> g_pools;         // "<voice type>/<personality>" (lower case) -> file names
		std::unordered_map<RE::FormID, std::string> g_last;                        // the line each actor said last, so a repeat is avoided
		std::unordered_set<std::string> g_reportedMissing;
		std::atomic<std::uint32_t> g_said{ 0 };
		std::string g_lastSaid = "none yet";

		std::string Lower(std::string_view a_text)
		{
			std::string s(a_text);
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		std::mt19937& Rng()
		{
			static std::mt19937 rng{ std::random_device{}() };
			return rng;
		}

		// Look at what the pack holds, once. The caller holds g_lock.
		void Scan()
		{
			if (g_scanned) return;
			g_scanned = true;
			std::error_code ec;
			std::size_t clips = 0;
			for (const auto& voice : std::filesystem::directory_iterator(kRoot, ec)) {
				if (ec || !voice.is_directory(ec)) continue;
				const std::string voiceName = voice.path().filename().string();
				g_voices[Lower(voiceName)] = voiceName;
				for (const auto& person : std::filesystem::directory_iterator(voice.path(), ec)) {
					if (ec || !person.is_directory(ec)) continue;
					auto& pool = g_pools[Lower(voiceName) + "/" + Lower(person.path().filename().string())];
					for (const auto& file : std::filesystem::directory_iterator(person.path(), ec)) {
						// .fuz is what the pack ships (the audio with its lip data); a bare .wav plays too, with no mouth movement from the engine.
						const std::string ext = Lower(file.path().extension().string());
						if (ec || !file.is_regular_file(ec) || (ext != ".fuz" && ext != ".wav")) continue;
						pool.push_back(file.path().filename().string());
						++clips;
					}
					std::ranges::sort(pool);
				}
			}
			logger::info("Virgin openers: {} clip(s) for {} voice type(s) found in {}{}", clips, g_voices.size(), kRoot, clips == 0 ? " (the OSIS Virgin Openers voice pack is not installed)" : "");
		}

		// The voice types to try, in order: the actor's own, her race's generic voice, then even-toned. (The NPC record's field, as OStim reads it: not TESBoundObject::GetObjectVoiceType.)
		std::vector<std::string> Chain(RE::Actor* a)
		{
			std::vector<std::string> out;
			const auto add = [&](std::string v) {
				if (!v.empty() && std::ranges::find(out, v) == out.end()) out.push_back(std::move(v));
			};
			RE::BGSVoiceType* vt = nullptr;
			if (auto* base = a->GetActorBase()) vt = base->voiceType;
			if (!vt) {
				if (auto* race = a->GetRace()) vt = race->defaultVoiceTypes[RE::SEX::kFemale];
			}
			if (const char* id = vt ? vt->GetFormEditorID() : nullptr; id && *id) add(Lower(id));
			if (auto* race = a->GetRace()) {
				const char* rid = race->GetFormEditorID();
				const std::string r = rid ? Lower(rid) : std::string{};
				if (r.contains("orc")) add("femaleorc");
				else if (r.contains("khajiit")) add("femalekhajiit");
				else if (r.contains("argonian")) add("femaleargonian");
				else if (r.contains("highelf") || r.contains("woodelf")) add("femaleelfhaughty");
				else if (r.contains("darkelf")) {
					add("femaledarkelf");
					add("femaleelfhaughty");
				}
			}
			add("femaleeventoned");
			return out;
		}

		// The console's SpeakSound, as OStim's dialogue does: the voice plays from the actor.
		void Speak(RE::Actor* a, const std::string& a_path)
		{
			const auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
			const auto script = factory ? factory->Create() : nullptr;
			if (!script) return;
			a->StopCurrentDialogue();
			script->SetCommand(std::format("SpeakSound \"{}\"", a_path));
			script->CompileAndRun(a);
			delete script;
		}
	}

	std::string Say(RE::Actor* a_actor)
	{
		if (!a_actor || a_actor->IsDead() || !a_actor->Get3D()) return "nobody there to speak";
		const std::string person = Lower(Face::Engine::PersonalityName(Face::Engine::Archetype(a_actor)));
		const auto chain = Chain(a_actor);
		std::string path;
		std::string voiceFolder;
		{
			std::scoped_lock l(g_lock);
			Scan();
			for (const auto& v : chain) {
				const auto pool = g_pools.find(v + "/" + person);
				const auto folder = g_voices.find(v);
				if (pool == g_pools.end() || pool->second.empty() || folder == g_voices.end()) continue;
				const auto& files = pool->second;
				const auto last = g_last.find(a_actor->GetFormID());
				std::vector<const std::string*> choices;
				for (const auto& f : files) {
					if (files.size() < 2 || last == g_last.end() || last->second != f) choices.push_back(&f);
				}
				const std::string& pick = *choices[std::uniform_int_distribution<std::size_t>(0, choices.size() - 1)(Rng())];
				g_last[a_actor->GetFormID()] = pick;
				voiceFolder = folder->second;
				path = std::format("{}/{}/{}/{}", kSoundRoot, voiceFolder, person, pick);
				break;
			}
			if (path.empty()) {
				const std::string why = std::format("no clip for voice type {} ({} tried), personality {}; {} voice type folder(s) in {}", chain.empty() ? "?" : chain.front(), chain.size(), person, g_voices.size(), kRoot);
				if (g_reportedMissing.insert(chain.empty() ? person : chain.front() + "/" + person).second) Debug::Log(Debug::Cat::Pers, "{}: virgin opener: {}", Debug::Label(a_actor), why);
				return why + (g_voices.empty() ? " (the OSIS Virgin Openers voice pack is not installed or not enabled)" : "");
			}
		}
		Speak(a_actor, path);
		++g_said;
		{
			std::scoped_lock l(g_lock);
			g_lastSaid = std::format("{} says {}", a_actor->GetDisplayFullName(), path);
		}
		Debug::Log(Debug::Cat::Pers, "{}: virgin opener ({}, {}): {}", Debug::Label(a_actor), voiceFolder, person, path);
		return std::format("{} ({}, {}): {}", a_actor->GetDisplayFullName(), voiceFolder, person, path);
	}

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		Scan();
		std::size_t clips = 0;
		for (const auto& [k, v] : g_pools) clips += v.size();
		return std::format("Virgin openers: {} clip(s) for {} voice type(s); {} said this session; last: {}", clips, g_voices.size(), g_said.load(), g_lastSaid);
	}
}
