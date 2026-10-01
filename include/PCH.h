// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMMNOSOUND
#define NOMINMAX

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

// The Nexus edition (xmake target OSISNexus) is compiled without the non-consent features.
#ifndef OSIS_NEXUS
#	define OSIS_NEXUS 0
#endif
using namespace std::literals;
