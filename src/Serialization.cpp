// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Serialization.h"

#include "Face/Engine.h"
#include "Skin.h"

namespace Serialization
{
	namespace
	{
		constexpr std::uint32_t kID = 'OSIS';
		constexpr std::uint32_t kPersonality = 'PERS';
		constexpr std::uint32_t kTakenOver = 'TKOV';
		constexpr std::uint32_t kEmoTears = 'EMOT';
		constexpr std::uint32_t kVersion = 1;

		void Save(SKSE::SerializationInterface* a_intfc)
		{
			const auto pers = Face::Engine::NpcPersonalities();
			if (a_intfc->OpenRecord(kPersonality, kVersion)) {
				const auto n = static_cast<std::uint32_t>(pers.size());
				a_intfc->WriteRecordData(n);
				for (const auto& [id, arch] : pers) {
					a_intfc->WriteRecordData(id);
					a_intfc->WriteRecordData(static_cast<std::int32_t>(arch));
				}
			}
			const auto ids = Face::Engine::TakenOverIDs();
			if (a_intfc->OpenRecord(kTakenOver, kVersion)) {
				const auto n = static_cast<std::uint32_t>(ids.size());
				a_intfc->WriteRecordData(n);
				for (auto id : ids) a_intfc->WriteRecordData(id);
			}
			const auto emo = Skin::EmoTearIDs();
			if (a_intfc->OpenRecord(kEmoTears, kVersion)) {
				const auto n = static_cast<std::uint32_t>(emo.size());
				a_intfc->WriteRecordData(n);
				for (auto id : emo) a_intfc->WriteRecordData(id);
			}
		}

		void Load(SKSE::SerializationInterface* a_intfc)
		{
			std::uint32_t type, version, length;
			std::unordered_map<RE::FormID, int> pers;
			std::vector<RE::FormID> taken;
			std::vector<RE::FormID> emo;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (version != kVersion) continue;
				std::uint32_t n = 0;
				if (!a_intfc->ReadRecordData(n)) continue;
				for (std::uint32_t i = 0; i < n; ++i) {
					RE::FormID id = 0;
					if (!a_intfc->ReadRecordData(id)) break;
					if (type == kPersonality) {
						std::int32_t arch = -1;
						if (!a_intfc->ReadRecordData(arch)) break;
						RE::FormID resolved = 0;
						if (a_intfc->ResolveFormID(id, resolved)) pers[resolved] = arch;
					} else if (type == kTakenOver) {
						RE::FormID resolved = 0;
						if (a_intfc->ResolveFormID(id, resolved)) taken.push_back(resolved);
					} else if (type == kEmoTears) {
						RE::FormID resolved = 0;
						if (a_intfc->ResolveFormID(id, resolved)) emo.push_back(resolved);
					}
				}
			}
			logger::info("Cosave: {} personalit(y/ies) kept (pinned or set), {} OStim face takeover(s) to restore", pers.size(), taken.size());
			Face::Engine::SetNpcPersonalities(std::move(pers));
			Face::Engine::SetTakenOverIDs(std::move(taken));
			Skin::SetEmoTearIDs(std::move(emo));
		}

		void Revert(SKSE::SerializationInterface*)
		{
			Face::Engine::SetNpcPersonalities({});
			Face::Engine::SetTakenOverIDs({});
			Skin::SetEmoTearIDs({});
		}
	}

	void Install()
	{
		auto* s = SKSE::GetSerializationInterface();
		s->SetUniqueID(kID);
		s->SetSaveCallback(Save);
		s->SetLoadCallback(Load);
		s->SetRevertCallback(Revert);
	}
}
