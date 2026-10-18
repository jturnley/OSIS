// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMMNOSOUND
#define NOMINMAX

// The NPC personality tables read from the SkyrimNet and CHIM bios (src/BioPersonality.*, the shipped npc_personalities.txt and npc_religion.txt) are not in the build for now:
// SkyrimNet's Plugin License does not clearly allow it. 0 leaves the code out; `xmake f --bio_tables=y` builds it again.
#ifndef OSIS_BIO_TABLES
#define OSIS_BIO_TABLES 0
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <numbers>
#include <optional>
#include <random>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <format>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <SimpleIni.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace logger = SKSE::log;

// The Lite edition (xmake target OSISLite) is compiled without the non-consent features.
#ifndef OSIS_LITE
#	define OSIS_LITE 0
#endif
using namespace std::literals;
