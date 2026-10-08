// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Overlays.h"

namespace Overlays
{
	namespace
	{
		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		// An unused overlay node carries no diffuse texture, or RaceMenu's own placeholder. Either
		// way there is nothing of anyone's to lose by writing there.
		bool LooksUnused(const std::string& a_lowerPath)
		{
			return a_lowerPath.empty() || a_lowerPath.contains("default.dds") || a_lowerPath.contains("overlays\\nothing");
		}

		// The diffuse texture a slot is currently showing. nullopt when the node is not there at
		// all: RaceMenu only builds as many as skee64.ini asks for, and the 3D may not be loaded.
		std::optional<std::string> SlotTexture(RE::Actor* a_actor, bool a_face, int a_index)
		{
			auto* root = a_actor ? a_actor->Get3D(false) : nullptr;
			if (!root) return std::nullopt;
			const RE::BSFixedString name(std::format("{} [Ovl{}]", a_face ? "Face" : "Body", a_index));
			auto* node = root->GetObjectByName(name);
			if (!node) return std::nullopt;
			auto* geom = node->AsGeometry();
			if (!geom) return std::nullopt;
			auto* shader = geom->lightingShaderProp_cast();
			if (!shader) return std::string{};
			auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(shader->material);
			if (!material || !material->textureSet) return std::string{};
			const char* path = material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
			return path ? Lower(path) : std::string{};
		}

		bool IsOurs(const std::string& a_lowerPath, const std::vector<std::string>& a_ours)
		{
			if (a_lowerPath.empty()) return false;
			for (const auto& mine : a_ours) {
				if (!mine.empty() && a_lowerPath.contains(Lower(mine))) return true;
			}
			return false;
		}
	}

	std::vector<int> Claim(RE::Actor* a, bool face, int count, int first, int total, const std::vector<std::string>& ours)
	{
		std::vector<int> slots;
		if (count <= 0 || total <= 0) return slots;
		first = std::clamp(first, 0, std::max(total - 1, 0));

		// Only upward from the configured slot. The setting exists to reserve the low slots for
		// other overlay mods, so filling them when the high ones run out would break that promise;
		// painting fewer rows and saying so in the log is the honest failure.
		bool read3D = false;
		for (int i = first; i < total; ++i) {
			const auto tex = SlotTexture(a, face, i);
			if (!tex) continue;  // no such node on this actor
			read3D = true;
			if (!LooksUnused(*tex) && !IsOurs(*tex, ours)) continue;
			slots.push_back(i);
			if (static_cast<int>(slots.size()) >= count) break;
		}
		if (!read3D) {
			// Nothing could be read - 3D not loaded yet, or a body that has no overlay nodes.
			// Fall back to the configured numbering, which is what this did before.
			slots.clear();
			for (int i = 0; i < count && first + i < total; ++i) slots.push_back(first + i);
		}
		return slots;
	}

	std::optional<bool> NodeHolds(RE::Actor* a, const std::string& a_node, const std::string& a_texture)
	{
		auto* root = a ? a->Get3D(false) : nullptr;
		if (!root) return std::nullopt;
		auto* node = root->GetObjectByName(RE::BSFixedString(a_node));
		auto* geom = node ? node->AsGeometry() : nullptr;
		if (!geom) return false;
		auto* shader = geom->lightingShaderProp_cast();
		auto* material = shader ? static_cast<RE::BSLightingShaderMaterialBase*>(shader->material) : nullptr;
		if (!material || !material->textureSet) return false;
		const char* path = material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
		return path && !a_texture.empty() && Lower(path).contains(Lower(a_texture));
	}

	std::string Report(RE::Actor* a, bool face, int total, const std::vector<std::string>& ours)
	{
		if (!a) return "no actor";
		int taken = 0, seen = 0;
		for (int i = 0; i < total; ++i) {
			const auto tex = SlotTexture(a, face, i);
			if (!tex) continue;
			++seen;
			if (!LooksUnused(*tex) && !IsOurs(*tex, ours)) ++taken;
		}
		if (seen == 0) return "slots not readable (3D not loaded)";
		return std::format("{} of {} {} slot(s) used by other mods", taken, seen, face ? "face" : "body");
	}
}
