// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

#include <chrono>
#include <mutex>
#include <unordered_map>

#include <cwctype>

// Filesystem helpers for walking Data\ folders that belong to other mods.
//
// Two things in those folders must never take the game down:
//  - Names the ANSI code page can't represent (e.g. mojibake folder names from archives packed
//    on a Chinese or Japanese system). path::string() throws std::system_error on them.
//  - A single unreadable directory. recursive_directory_iterator stops the whole walk on the
//    first error, which silently truncated the scans before.
namespace FsUtil
{
	// The path in the ANSI code page (what the engine and plugins use), or nullopt if a
	// character has no mapping there. The engine can't reference such a file anyway.
	inline std::optional<std::string> Narrow(const std::filesystem::path& a_path)
	{
		try {
			return a_path.string();
		} catch (const std::system_error&) {
			return std::nullopt;
		}
	}

	// For log lines only: never throws.
	inline std::string Printable(const std::filesystem::path& a_path)
	{
		try {
			const auto u8 = a_path.u8string();
			return std::string(u8.begin(), u8.end());
		} catch (...) {
			return "<unprintable path>";
		}
	}

	// Lowercase extension, including the dot (".wav"). Works on the wide string, so it can't throw.
	inline std::wstring LowerExt(const std::filesystem::path& a_path)
	{
		auto ext = a_path.extension().wstring();
		std::ranges::transform(ext, ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		return ext;
	}

	// A texture the engine can actually load, named the way the overlay functions want it:
	// relative to Data\\textures. Goes through the resource system, so a file inside a BSA counts
	// and a loose-file test would not. Worth checking before every overlay: NiOverride takes a
	// missing path without complaint and the slot then renders as a black patch over the body,
	// which looks like a shader bug rather than a missing file.
	inline bool TextureExists(std::string_view a_relative)
	{
		if (a_relative.empty()) return false;
		std::string path("textures\\");
		path.append(a_relative);
		RE::BSResourceNiBinaryStream stream(path);
		return stream.good();
	}

	// TextureExists, remembered. Opening a resource stream is a lookup in the archives or a file open (and one more hop through the virtual
	// file system under Mod Organizer), and the overlay code asked about every texture of every actor every second. A texture that is there
	// stays there for the session; one that is not is asked about again after half a minute, in case it was installed meanwhile.
	inline bool TextureExistsCached(std::string_view a_relative)
	{
		struct Entry
		{
			bool exists;
			std::chrono::steady_clock::time_point at;
		};
		static std::mutex lock;
		static std::unordered_map<std::string, Entry> cache;
		const auto now = std::chrono::steady_clock::now();
		std::string key(a_relative);
		{
			std::scoped_lock l(lock);
			if (const auto it = cache.find(key); it != cache.end() && (it->second.exists || now - it->second.at < std::chrono::seconds(30))) return it->second.exists;
		}
		const bool exists = TextureExists(a_relative);
		std::scoped_lock l(lock);
		cache[std::move(key)] = { exists, now };
		return exists;
	}

	struct WalkStats
	{
		std::size_t files = 0;
		std::size_t dirErrors = 0;  // directories that could not be opened or fully read
	};

	// Calls a_onFile(path) for every regular file under a_root. An error in one directory skips
	// the rest of that directory only. Directory symlinks/junctions are not followed.
	template <class F>
	WalkStats WalkFiles(const std::filesystem::path& a_root, F&& a_onFile)
	{
		namespace fs = std::filesystem;
		WalkStats stats;
		std::vector<fs::path> pending{ a_root };
		while (!pending.empty()) {
			const fs::path dir = std::move(pending.back());
			pending.pop_back();
			std::error_code ec;
			fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
			if (ec) {
				++stats.dirErrors;
				continue;
			}
			for (const fs::directory_iterator end; it != end; it.increment(ec)) {
				if (ec) {
					++stats.dirErrors;
					break;
				}
				std::error_code entryEc;
				const auto& entry = *it;
				if (entry.is_symlink(entryEc)) continue;
				if (entry.is_directory(entryEc)) {
					pending.push_back(entry.path());
				} else if (entry.is_regular_file(entryEc)) {
					++stats.files;
					a_onFile(entry.path());
				}
			}
		}
		return stats;
	}
}
