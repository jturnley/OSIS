// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Serialization.h"

#include "Experience.h"
#include "Face/Engine.h"
#include "Skin.h"
#include "Virginity.h"

namespace Serialization
{
	namespace
	{
		constexpr std::uint32_t kID = 'OSIS';
		constexpr std::uint32_t kPersonality = 'PERS';
		constexpr std::uint32_t kTakenOver = 'TKOV';
		constexpr std::uint32_t kEmoTears = 'EMOT';
		constexpr std::uint32_t kExperience = 'EXPR';
		constexpr std::uint32_t kVirginity = 'VIRG';
		constexpr std::uint32_t kVersion = 1;
		constexpr std::uint32_t kExperienceVersion = 2;  // 2: who they are obsessed with, and the sessions they have had with each partner (1 held a plain crazed counter)

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
			const auto virginity = Virginity::Snapshot();
			if (a_intfc->OpenRecord(kVirginity, kVersion)) {
				const auto n = static_cast<std::uint32_t>(virginity.size());
				a_intfc->WriteRecordData(n);
				for (const auto& [id, r] : virginity) {
					a_intfc->WriteRecordData(id);
					a_intfc->WriteRecordData(r.state);
					a_intfc->WriteRecordData(r.original);
					a_intfc->WriteRecordData(r.pendingCrazed);
					a_intfc->WriteRecordData(r.broken);
					a_intfc->WriteRecordData(r.brokenRate);
				}
			}
			const auto experience = Experience::Snapshot();
			if (a_intfc->OpenRecord(kExperience, kExperienceVersion)) {
				const auto n = static_cast<std::uint32_t>(experience.size());
				a_intfc->WriteRecordData(n);
				for (const auto& [id, r] : experience) {
					a_intfc->WriteRecordData(id);
					a_intfc->WriteRecordData(r.pers);
					a_intfc->WriteRecordData(r.qualified);
					a_intfc->WriteRecordData(r.sessions);
					a_intfc->WriteRecordData(r.orgasms);
					a_intfc->WriteRecordData(r.obsessedWith);
					const auto partners = static_cast<std::uint32_t>(r.partners.size());
					a_intfc->WriteRecordData(partners);
					for (const auto& [partner, count] : r.partners) {
						a_intfc->WriteRecordData(partner);
						a_intfc->WriteRecordData(count);
					}
				}
			}
		}

		void Load(SKSE::SerializationInterface* a_intfc)
		{
			std::uint32_t type, version, length;
			std::unordered_map<RE::FormID, int> pers;
			std::vector<RE::FormID> taken;
			std::vector<RE::FormID> emo;
			std::unordered_map<RE::FormID, Experience::Record> experience;
			std::unordered_map<RE::FormID, Virginity::Record> virginity;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (version != (type == kExperience ? kExperienceVersion : kVersion)) continue;
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
					} else if (type == kVirginity) {
						Virginity::Record r;
						if (!a_intfc->ReadRecordData(r.state) || !a_intfc->ReadRecordData(r.original) || !a_intfc->ReadRecordData(r.pendingCrazed) || !a_intfc->ReadRecordData(r.broken) ||
							!a_intfc->ReadRecordData(r.brokenRate))
							break;
						RE::FormID resolved = 0;
						if (a_intfc->ResolveFormID(id, resolved)) virginity[resolved] = r;
					} else if (type == kExperience) {
						Experience::Record r;
						std::uint32_t partners = 0;
						if (!a_intfc->ReadRecordData(r.pers) || !a_intfc->ReadRecordData(r.qualified) || !a_intfc->ReadRecordData(r.sessions) || !a_intfc->ReadRecordData(r.orgasms) ||
							!a_intfc->ReadRecordData(r.obsessedWith) || !a_intfc->ReadRecordData(partners) || partners > 64)
							break;
						bool ok = true;
						for (std::uint32_t k = 0; k < partners && ok; ++k) {
							RE::FormID partner = 0;
							std::uint32_t count = 0;
							if (!a_intfc->ReadRecordData(partner) || !a_intfc->ReadRecordData(count)) {
								ok = false;
								break;
							}
							RE::FormID resolvedPartner = 0;
							if (a_intfc->ResolveFormID(partner, resolvedPartner)) r.partners.emplace_back(resolvedPartner, count);
						}
						if (!ok) break;
						if (r.obsessedWith != 0) {
							RE::FormID resolvedObsession = 0;
							r.obsessedWith = a_intfc->ResolveFormID(r.obsessedWith, resolvedObsession) ? resolvedObsession : 0;
						}
						RE::FormID resolved = 0;
						if (a_intfc->ResolveFormID(id, resolved)) experience[resolved] = std::move(r);
					}
				}
			}
			logger::info("Cosave: {} personalit(y/ies) kept (pinned or set), {} OStim face takeover(s) to restore", pers.size(), taken.size());
			Face::Engine::SetNpcPersonalities(std::move(pers));
			Face::Engine::SetTakenOverIDs(std::move(taken));
			Skin::SetEmoTearIDs(std::move(emo));
			Experience::Restore(std::move(experience));
			Virginity::Restore(std::move(virginity));
		}

		void Revert(SKSE::SerializationInterface*)
		{
			Face::Engine::SetNpcPersonalities({});
			Face::Engine::SetTakenOverIDs({});
			Skin::SetEmoTearIDs({});
			Experience::Clear();
			Virginity::Clear();
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
