// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "LipSync.h"

#include "Compat.h"
#include "Face/Engine.h"
#include "Face/Output.h"
#include "FsUtil.h"
#include "Scenes.h"
#include "Settings.h"
#if !OSIS_LITE
#	include "Voice.h"
#endif

namespace LipSync
{
	namespace
	{
		using Envelope = Face::Output::Envelope;

		struct Key
		{
			std::uint32_t file = 0;
			std::uint32_t ext = 0;
			std::uint32_t dir = 0;
			bool operator==(const Key&) const = default;
		};

		struct KeyHash
		{
			std::size_t operator()(const Key& k) const noexcept { return (static_cast<std::size_t>(k.file) << 32) ^ (static_cast<std::size_t>(k.dir) * 31) ^ k.ext; }
		};

		Key ToKey(const RE::BSResource::ID& id)
		{
			Key k;
			k.file = id.file;
			std::memcpy(&k.ext, id.ext, 4);
			k.dir = id.dir;
			return k;
		}

		std::mutex g_lock;
		std::unordered_set<Key, KeyHash> g_wanted;                                     // moan files named by voice sets
		std::unordered_map<Key, std::shared_ptr<const Envelope>, KeyHash> g_envelopes;  // decoded
		std::unordered_set<Key, KeyHash> g_silence;     // every voice-set file, muffled too (a victim's are muted)
		std::atomic_bool g_silenceFrozen = false;       // g_silence is complete and read without a lock
		std::atomic_bool g_ready = false;
		std::string g_status = "not started";
		std::string g_lastMatch = "none yet";
		std::size_t g_descriptors = 0;

		// ------------------------------------------------------------ voice sets
		void CollectSounds(const json& j, std::vector<std::pair<std::string, RE::FormID>>& out, const char* a_key = "sound")
		{
			// A reaction set: { "sound": [ { "sound": { "mod", "formid" } } ], "soundMuffled": [...] }.
			// Muffled sounds (sucking, gagging) are not lip-synced: the mouth is busy then.
			if (j.is_object()) {
				if (auto it = j.find(a_key); it != j.end() && it->is_array()) {
					for (const auto& set : *it) {
						if (!set.is_object() || !set.contains("sound")) continue;
						const auto& f = set.at("sound");
						if (!f.is_object() || !f.contains("mod") || !f.contains("formid")) continue;
						const auto id = static_cast<RE::FormID>(std::strtoul(f.at("formid").get<std::string>().c_str(), nullptr, 16));
						out.emplace_back(f.at("mod").get<std::string>(), id);
					}
				}
			}
		}

		void ParseVoiceSets(std::vector<std::pair<std::string, RE::FormID>>& out, std::vector<std::pair<std::string, RE::FormID>>& muffled)
		{
			constexpr auto kRoot = "Data/SKSE/Plugins/OStim/voice sets";
			std::error_code ec;
			if (!std::filesystem::exists(kRoot, ec)) return;
			const auto stats = FsUtil::WalkFiles(kRoot, [&](const std::filesystem::path& path) {
				if (FsUtil::LowerExt(path) != L".json") return;
				try {
					std::ifstream f(path);
					const auto doc = json::parse(f, nullptr, true, true);
					for (const char* key : { "moan", "climax", "climaxCommentSelf", "climaxCommentOther" }) {
						if (!doc.contains(key)) continue;
						CollectSounds(doc.at(key), out);
						CollectSounds(doc.at(key), muffled, "soundMuffled");
					}
					for (const char* key : { "eventActorReactions", "eventTargetReactions", "eventPerformerReactions" }) {
						if (!doc.contains(key) || !doc.at(key).is_object()) continue;
						for (const auto& [ev, set] : doc.at(key).items()) {
							CollectSounds(set, out);
							CollectSounds(set, muffled, "soundMuffled");
						}
					}
				} catch (const std::exception& e) {
					logger::warn("voice set {}: {}", FsUtil::Printable(path), e.what());
				}
			});
			if (stats.dirErrors) logger::warn("Lip-sync: {} voice-set folder(s) could not be read", stats.dirErrors);
		}

		// ------------------------------------------------------------ wav decoding
		std::shared_ptr<const Envelope> Decode(const std::filesystem::path& path)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f) return nullptr;
			std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			if (data.size() < 44 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "WAVE", 4) != 0) return nullptr;

			std::uint16_t format = 0, channels = 0, bits = 0;
			std::uint32_t rate = 0;
			const char* samples = nullptr;
			std::size_t sampleBytes = 0;
			std::size_t pos = 12;
			while (pos + 8 <= data.size()) {
				std::uint32_t size;
				std::memcpy(&size, data.data() + pos + 4, 4);
				const char* body = data.data() + pos + 8;
				if (std::memcmp(data.data() + pos, "fmt ", 4) == 0 && size >= 16) {
					std::memcpy(&format, body, 2);
					std::memcpy(&channels, body + 2, 2);
					std::memcpy(&rate, body + 4, 4);
					std::memcpy(&bits, body + 14, 2);
				} else if (std::memcmp(data.data() + pos, "data", 4) == 0) {
					samples = body;
					sampleBytes = std::min<std::size_t>(size, data.size() - (pos + 8));
				}
				pos += 8 + size + (size & 1);
			}
			const bool pcm = format == 1 && (bits == 16 || bits == 24 || bits == 8);
			const bool flt = format == 3 && bits == 32;
			if (!samples || !channels || !rate || (!pcm && !flt)) return nullptr;

			const std::size_t frameBytes = (bits / 8) * channels;
			const std::size_t count = sampleBytes / frameBytes;
			auto read = [&](std::size_t i, int ch) -> float {
				const char* p = samples + i * frameBytes + ch * (bits / 8);
				if (flt) {
					float v;
					std::memcpy(&v, p, 4);
					return v;
				}
				if (bits == 16) {
					std::int16_t v;
					std::memcpy(&v, p, 2);
					return v / 32768.0f;
				}
				if (bits == 24) {
					const std::int32_t v = (static_cast<std::uint8_t>(p[0]) | (static_cast<std::uint8_t>(p[1]) << 8) | (static_cast<std::int8_t>(p[2]) << 16));
					return v / 8388608.0f;
				}
				return (static_cast<std::uint8_t>(p[0]) - 128) / 128.0f;
			};

			auto env = std::make_shared<Envelope>();
			env->frameSeconds = 0.01f;
			const std::size_t hop = std::max<std::size_t>(1, rate / 100);
			std::vector<float> rms;
			std::vector<float> zcr;
			for (std::size_t start = 0; start < count; start += hop) {
				const std::size_t end = std::min(count, start + hop);
				double sum = 0.0;
				int crossings = 0;
				float prev = 0.0f;
				for (std::size_t i = start; i < end; ++i) {
					float v = 0.0f;
					for (int c = 0; c < channels; ++c) v += read(i, c);
					v /= channels;
					sum += static_cast<double>(v) * v;
					if (i > start && ((v >= 0.0f) != (prev >= 0.0f))) ++crossings;
					prev = v;
				}
				const std::size_t n = end - start;
				rms.push_back(static_cast<float>(std::sqrt(sum / std::max<std::size_t>(1, n))));
				zcr.push_back(static_cast<float>(crossings) / static_cast<float>(std::max<std::size_t>(1, n)));
			}
			if (rms.empty()) return nullptr;

			// Normalize to the file's own loud parts so quiet and loud voice packs both move.
			auto sorted = rms;
			std::ranges::sort(sorted);
			const float ref = std::max(0.02f, sorted[static_cast<std::size_t>(0.95f * static_cast<float>(sorted.size() - 1))]);
			env->loud.reserve(rms.size());
			env->bright.reserve(rms.size());
			for (std::size_t i = 0; i < rms.size(); ++i) {
				const float r = std::clamp(rms[i] / ref, 0.0f, 1.0f);
				env->loud.push_back(r < 0.06f ? 0.0f : std::pow(r, 0.6f));  // gate breath noise, lift the middle
				env->bright.push_back(std::clamp((zcr[i] - 0.03f) / 0.25f, 0.0f, 1.0f));
			}
			return env;
		}

		struct Match
		{
			Key key;
			std::filesystem::path path;
		};

		// ------------------------------------------------------------ descriptor file lists
		// The engine keeps a descriptor's files only as hashed IDs (soundFiles), in ANAM order. The
		// ANAM strings themselves are read from the plugin that last edited the record and paired
		// with those IDs by position. That gives each ID its real path without guessing the
		// engine's path format (plugins store "data\sound\...", "sound\..." or "fx\..."), and
		// without walking all of Data\Sound (which took ~11 s at load).
		struct PluginSounds
		{
			bool ok = false;
			std::vector<std::string> masters;                                                // lowercase
			std::map<std::pair<std::string, std::uint32_t>, std::vector<std::string>> anam;  // (owner, local id) -> paths
			std::size_t compressed = 0;
		};

		std::string LowerStr(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		// Subrecords of one record body; XXXX carries the size of the next oversized subrecord.
		template <class F>
		void ForEachSubrecord(const char* a_data, std::size_t a_size, F&& a_fn)
		{
			std::size_t i = 0;
			std::uint32_t big = 0;
			while (i + 6 <= a_size) {
				const std::string_view sig(a_data + i, 4);
				std::uint16_t size16;
				std::memcpy(&size16, a_data + i + 4, 2);
				i += 6;
				const std::size_t size = big ? big : size16;
				big = 0;
				if (sig == "XXXX" && size16 == 4 && i + 4 <= a_size) {
					std::memcpy(&big, a_data + i, 4);
					i += 4;
					continue;
				}
				if (i + size > a_size) break;
				a_fn(sig, a_data + i, size);
				i += size;
			}
		}

		std::string ZString(const char* a_data, std::size_t a_size)
		{
			return std::string(a_data, strnlen(a_data, a_size));
		}

		PluginSounds ReadPluginSounds(const std::string& a_plugin)
		{
			PluginSounds out;
			std::ifstream f(std::filesystem::path("Data") / a_plugin, std::ios::binary);
			if (!f) return out;
			const std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			const std::string self = LowerStr(a_plugin);
			auto u32 = [&](std::size_t a_at) {
				std::uint32_t v;
				std::memcpy(&v, buf.data() + a_at, 4);
				return v;
			};
			if (buf.size() < 24 || std::memcmp(buf.data(), "TES4", 4) != 0) return out;
			const std::size_t tes4Size = u32(4);
			if (24 + tes4Size > buf.size()) return out;
			ForEachSubrecord(buf.data() + 24, tes4Size, [&](std::string_view a_sig, const char* a_d, std::size_t a_n) {
				if (a_sig == "MAST") out.masters.push_back(LowerStr(ZString(a_d, a_n)));
			});
			std::size_t pos = 24 + tes4Size;
			while (pos + 24 <= buf.size()) {
				if (std::memcmp(buf.data() + pos, "GRUP", 4) != 0) break;
				const std::size_t groupSize = u32(pos + 4);
				if (groupSize < 24 || pos + groupSize > buf.size()) break;
				if (std::memcmp(buf.data() + pos + 8, "SNDR", 4) == 0) {
					std::size_t r = pos + 24;
					while (r + 24 <= pos + groupSize) {
						const std::size_t dataSize = u32(r + 4);
						const std::uint32_t flags = u32(r + 8);
						const std::uint32_t fileFormID = u32(r + 12);
						if (r + 24 + dataSize > pos + groupSize) break;
						if (std::memcmp(buf.data() + r, "SNDR", 4) == 0) {
							if (flags & 0x40000) {
								++out.compressed;  // no zlib in this DLL; descriptors are practically never compressed
							} else {
								const std::size_t masterIdx = fileFormID >> 24;
								const std::string owner = masterIdx < out.masters.size() ? out.masters[masterIdx] : self;
								auto& paths = out.anam[{ owner, fileFormID & 0xFFFFFF }];
								ForEachSubrecord(buf.data() + r + 24, dataSize, [&](std::string_view a_sig, const char* a_d, std::size_t a_n) {
									if (a_sig == "ANAM") paths.push_back(ZString(a_d, a_n));
								});
							}
						}
						r += 24 + dataSize;
					}
				}
				pos += groupSize;
			}
			out.ok = true;
			return out;
		}

		// ANAM paths are relative to the game folder ("data\sound\..."), to Data ("sound\..."),
		// or to Data\Sound ("fx\..."). The game's working directory is the game folder.
		std::filesystem::path ResolveSoundPath(const std::string& a_anam)
		{
			const std::string lower = LowerStr(a_anam);
			if (lower.starts_with("data\\") || lower.starts_with("data/")) return std::filesystem::path(a_anam);
			if (lower.starts_with("sound\\") || lower.starts_with("sound/")) return std::filesystem::path("Data") / a_anam;
			return std::filesystem::path("Data") / "Sound" / a_anam;
		}

		struct Descriptor
		{
			const RE::BGSStandardSoundDef* def;
			std::string winningFile;  // the plugin whose record the engine uses
			std::string owner;        // lowercase name of the plugin that defines the form
			std::uint32_t localID;
		};

		// Main thread (reads form data); each plugin file is read once.
		std::vector<Match> PairSoundFiles(const std::vector<Descriptor>& a_descs, std::size_t& a_paired)
		{
			std::vector<Match> out;
			std::unordered_map<std::string, PluginSounds> plugins;
			std::size_t mismatched = 0, missing = 0, compressed = 0, unreadable = 0;
			for (const auto& d : a_descs) {
				auto [it, fresh] = plugins.try_emplace(d.winningFile);
				if (fresh) {
					it->second = ReadPluginSounds(d.winningFile);
					compressed += it->second.compressed;
					if (!it->second.ok) ++unreadable;
				}
				const auto rec = it->second.anam.find({ d.owner, d.localID });
				if (rec == it->second.anam.end() || rec->second.size() != d.def->soundFiles.size()) {
					++mismatched;
					continue;
				}
				for (std::size_t i = 0; i < rec->second.size(); ++i) {
					auto path = ResolveSoundPath(rec->second[i]);
					std::error_code ec;
					if (!std::filesystem::is_regular_file(path, ec)) {
						++missing;  // packed in a BSA, or shipped as .xwm/.fuz: not decodable here
						continue;
					}
					out.push_back({ ToKey(d.def->soundFiles[i]), std::move(path) });
					++a_paired;
				}
			}
			if (mismatched || missing || compressed || unreadable) {
				logger::warn("Lip-sync: {} descriptor(s) without a matching file list, {} file(s) not loose, {} compressed record(s), {} unreadable plugin(s)",
					mismatched, missing, compressed, unreadable);
			}
			return out;
		}

		// Runs on its own thread: an exception escaping it would terminate the game.
		void DecodeAll(std::vector<Match> files, std::size_t paired, std::chrono::steady_clock::time_point t0)
		{
			std::size_t decoded = 0;
			for (auto& m : files) {
				try {
					if (auto env = Decode(m.path)) {
						std::scoped_lock l(g_lock);
						g_envelopes[m.key] = std::move(env);
						++decoded;
					}
				} catch (const std::exception& e) {
					logger::warn("Lip-sync: could not decode {}: {}", FsUtil::Printable(m.path), e.what());
				}
			}
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
			{
				std::scoped_lock l(g_lock);
				g_status = std::format("{} moan files decoded ({} voice-set sounds, {} found on disk, {} ms)", decoded, g_wanted.size(), paired, ms);
			}
			g_ready = true;
			logger::info("Lip-sync: {} of {} voice-set sound files decoded ({} found on disk) in {} ms", decoded, g_wanted.size(), paired, ms);
		}

		// ------------------------------------------------------------ audio polling
		struct Playing
		{
			std::uint32_t soundID;
			RE::NiAVObject* node;
			Key key;
			std::uint32_t positionMS;
		};

		// The audio manager's maps are owned by the audio thread; read them defensively.
		// No C++ objects with destructors live in this function so SEH can guard it.
		int CollectPlaying(RE::BSAudioManager* am, Playing* out, int max) noexcept
		{
			int n = 0;
			__try {
				for (auto& [soundID, nodePtr] : am->movingSounds) {
					if (n >= max) break;
					RE::NiAVObject* node = nodePtr.get();
					if (!node) continue;
					auto it = am->activeSounds.find(soundID);
					if (it == am->activeSounds.end() || !it->second) continue;
					RE::BSGameSound* snd = it->second;
					if (!snd->IsPlaying()) continue;
					out[n].soundID = soundID;
					out[n].node = node;
					out[n].key.file = snd->resourceID.file;
					std::memcpy(&out[n].key.ext, snd->resourceID.ext, 4);
					out[n].key.dir = snd->resourceID.dir;
					out[n].positionMS = snd->GetCurrentPlaybackPosition();
					++n;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return n;
			}
			return n;
		}

		// The actor a playing sound follows. OStim attaches moans to the actor's 3D root
		// (SetObjectToFollow(actor->Get3D())); other mods may use a child node such as the head.
		// Walk up to the first node the engine tagged with its owning reference. (The old code
		// climbed to the top of the whole scene graph and compared that with the actor's root,
		// which never matched, so no clip was ever lip-synced.)
		RE::Actor* OwnerOf(RE::NiAVObject* node)
		{
			for (int guard = 0; node && guard < 64; ++guard, node = node->parent) {
				if (auto* ref = node->GetUserData()) return ref->As<RE::Actor>();
			}
			return nullptr;
		}

		// Diagnostics: one summary line per 10 s while moan clips are playing, and the first
		// match per actor, so a log shows which stage fails if lip-sync stays silent.
		struct PollStats
		{
			std::size_t clips = 0, matched = 0, notInScene = 0, noOwner = 0, mouthBusy = 0, tongueOut = 0;
			float windowStart = 0.0f;
		};
		PollStats g_stats;
		std::unordered_set<RE::FormID> g_reported;
	}

	void OnDataLoaded()
	{
		std::vector<std::pair<std::string, RE::FormID>> refs;
		std::vector<std::pair<std::string, RE::FormID>> muffled;
		ParseVoiceSets(refs, muffled);
		auto* dh = RE::TESDataHandler::GetSingleton();
#if !OSIS_LITE
		// Everything a victim must not be heard making: the lip-synced sounds and the muffled ones.
		for (const auto* list : { &refs, &muffled }) {
			for (const auto& [mod, local] : *list) {
				auto* form = dh ? dh->LookupForm<RE::BGSSoundDescriptorForm>(local, mod) : nullptr;
				auto* def = form && form->soundDescriptor ? skyrim_cast<RE::BGSStandardSoundDef*>(form->soundDescriptor) : nullptr;
				if (!def) continue;
				for (const auto& id : def->soundFiles) g_silence.insert(ToKey(id));
			}
		}
		g_silenceFrozen = true;
		logger::info("Victim voice: {} OStim voice-set sound files can be muted on a victim", g_silence.size());
#endif
		std::unordered_set<RE::FormID> seen;
		std::vector<Descriptor> descs;
		for (const auto& [mod, local] : refs) {
			auto* form = dh ? dh->LookupForm<RE::BGSSoundDescriptorForm>(local, mod) : nullptr;
			if (!form || !seen.insert(form->GetFormID()).second) continue;
			auto* def = form->soundDescriptor ? skyrim_cast<RE::BGSStandardSoundDef*>(form->soundDescriptor) : nullptr;
			const auto* definer = form->GetFile(0);
			const auto* winner = form->GetFile(-1);
			if (!def || !definer || !winner) continue;
			++g_descriptors;
			descs.push_back({ def, std::string(winner->GetFilename()), LowerStr(std::string(definer->GetFilename())), form->GetLocalFormID() & 0xFFFFFF });
			std::scoped_lock l(g_lock);
			for (const auto& id : def->soundFiles) g_wanted.insert(ToKey(id));
		}
		logger::info("Lip-sync: {} sound descriptors / {} files named by OStim voice sets", g_descriptors, g_wanted.size());
		if (g_wanted.empty()) {
			g_status = "no voice-set sounds found";
			return;
		}
		g_status = "decoding moan files...";
		const auto t0 = std::chrono::steady_clock::now();
		std::size_t paired = 0;
		auto files = PairSoundFiles(descs, paired);
		logger::info("Lip-sync: {} moan files located from plugin records in {} ms", paired,
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
		std::thread(DecodeAll, std::move(files), paired, t0).detach();
	}

	void Poll()
	{
		Face::Output::TrackParams params;
		int tongueMode = Settings::LipSync::kTongueStop;
		float tongueMinOpen = 0.45f;
		{
			std::scoped_lock l(Settings::lock);
			if (!Settings::LipSync::bEnabled || !Settings::General::bEnabled) return;
			params.gain = Settings::LipSync::fGain;
			params.maxOpen = Settings::LipSync::fMaxOpen;
			params.attack = Settings::LipSync::fAttack;
			params.release = Settings::LipSync::fRelease;
			params.holdEyes = Settings::LipSync::bHoldEyes;
			tongueMode = Settings::LipSync::iTongueMode;
			tongueMinOpen = Settings::LipSync::fTongueMinOpen;
		}
		if (!g_ready || Compat::Disabled(Compat::kLipSync)) return;
		auto* am = RE::BSAudioManager::GetSingleton();
		if (!am) return;

		std::array<Playing, 64> playing{};
		const int n = CollectPlaying(am, playing.data(), static_cast<int>(playing.size()));
		if (n == 0) return;

		const float now = Scenes::Now();
		std::scoped_lock sl(Scenes::Lock());
		for (int i = 0; i < n; ++i) {
			const auto& p = playing[i];
			std::shared_ptr<const Envelope> env;
			{
				std::scoped_lock l(g_lock);
				auto it = g_envelopes.find(p.key);
				if (it == g_envelopes.end()) continue;
				env = it->second;
			}
			++g_stats.clips;
			// Which scene actor is this sound following?
			RE::Actor* owner = OwnerOf(p.node);
			if (!owner) {
				++g_stats.noOwner;
				continue;
			}
			if (!Scenes::InAnyScene(owner)) {
				++g_stats.notInScene;
				continue;
			}
#if !OSIS_LITE
			// A victim's moans are muted: no mouth to move.
			if (Voice::IsSilenced(owner)) continue;
#endif
			// Oral / dialogue: the mouth belongs to the animation or the voice line.
			auto* t = Scenes::ThreadOf(owner);
			auto* s = t ? t->Find(owner) : nullptr;
			if (s && (s->exprOverride || (s->externalMouthUntil > now))) {
				++g_stats.mouthBusy;
				continue;
			}
			// An ahegao mod owns this face; leave its mouth alone entirely.
			if (s && s->tongueOut && !s->tongueOn) {
				++g_stats.tongueOut;
				continue;
			}
			// Our own tongue is out: closing lips over it make it clip through the mouth.
			if (s && s->tongueOn && tongueMode != Settings::LipSync::kTongueIgnore) {
				if (tongueMode == Settings::LipSync::kTongueStop) {
					Face::Output::ClearMouthTrack(owner);
					++g_stats.tongueOut;
					continue;
				}
				params.minOpen = tongueMinOpen;  // Hold open: keep the lips clear of it
			}
			const float start = now - static_cast<float>(p.positionMS) / 1000.0f;
			auto track = params;
			if (s && s->broken) track.holdEyes = false;  // a broken victim's eyes don't squeeze
			Face::Output::SetMouthTrack(owner, env, start, track);
			++g_stats.matched;
			if (g_reported.insert(owner->GetFormID()).second) {
				logger::info("Lip-sync: first clip on {:08X} {} ({:.1f} s)", owner->GetFormID(), owner->GetDisplayFullName(), env->Duration());
			}
			std::scoped_lock l(g_lock);
			g_lastMatch = std::format("{} ({:.1f} s clip)", owner->GetDisplayFullName(), env->Duration());
		}
		if (g_stats.clips && now - g_stats.windowStart >= 10.0f) {
			logger::info("Lip-sync: {} moan-clip polls in the last 10 s: {} lip-synced, {} mouth busy (oral/dialogue/override), {} tongue out, {} not in a scene, {} with no owning actor",
				g_stats.clips, g_stats.matched, g_stats.mouthBusy, g_stats.tongueOut, g_stats.notInScene, g_stats.noOwner);
			g_stats = PollStats{};
			g_stats.windowStart = now;
		} else if (!g_stats.clips) {
			g_stats.windowStart = now;
		}
	}

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		return std::format("{}; last lip-synced: {}", g_status, g_lastMatch);
	}

	std::size_t EnvelopeCount()
	{
		std::scoped_lock l(g_lock);
		return g_envelopes.size();
	}

	bool IsMoanResource(const RE::BSResource::ID& a_id)
	{
		if (!g_silenceFrozen.load(std::memory_order_acquire)) return false;
		return g_silence.contains(ToKey(a_id));
	}

	RE::Actor* ActorOf(RE::NiAVObject* a_node) { return OwnerOf(a_node); }
}
