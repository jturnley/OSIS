// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "Settings.h"

namespace Settings
{
	namespace
	{
		constexpr auto kIniPath = "Data/SKSE/Plugins/OSIS.ini";
		constexpr auto kTablePath = "Data/SKSE/Plugins/OSIS/morphs.json";

		using Ref = std::variant<bool*, int*, float*, std::string*>;

		struct Binding
		{
			const char* section;
			const char* key;
			Ref ref;
		};

		// One row per persisted setting; Load and Save walk the same table.
		const std::vector<Binding>& Bindings()
		{
			static const std::vector<Binding> rows = {
				{ "General", "bEnabled", &General::bEnabled },
				{ "General", "bIncludeNPCs", &General::bIncludeNPCs },
				{ "General", "bNPCOnlyScenes", &General::bNPCOnlyScenes },
				{ "General", "fNPCSceneRadius", &General::fNPCSceneRadius },
				{ "General", "bPulseBus", &General::bPulseBus },
				{ "General", "bAnimationHooks", &General::bAnimationHooks },
				{ "General", "bDebug", &General::bDebug },

				{ "Face", "bEnabled", &Face::bEnabled },
				{ "Face", "iMode", &Face::iMode },
				{ "Face", "fGlobalStrength", &Face::fGlobalStrength },
				{ "Face", "fDirectorGain", &Face::fDirectorGain },
				{ "Face", "bDirectorLibrary", &Face::bDirectorLibrary },
				{ "Face", "fBaseInterval", &Face::fBaseInterval },
				{ "Face", "fIntervalJitter", &Face::fIntervalJitter },
				{ "Face", "fTransition", &Face::fTransition },
				{ "Face", "iProfile", &Face::iProfile },
				{ "Face", "fStyle", &Face::fStyle },
				{ "Face", "fEyeStrength", &Face::fEyeStrength },
				{ "Face", "bTakeOverFace", &Face::bTakeOverFace },
				{ "Face", "bRoleAware", &Face::bRoleAware },
				{ "Face", "bActTypeAware", &Face::bActTypeAware },
				{ "Face", "bPaceBoost", &Face::bPaceBoost },
				{ "Face", "bCinematic", &Face::bCinematic },
				{ "Face", "bBreathing", &Face::bBreathing },
				{ "Face", "bNaturalDetail", &Face::bNaturalDetail },
				{ "Face", "bGaze", &Face::bGaze },
#if !OSIS_LITE
				{ "Face", "bGazeConsentOnly", &Face::bGazeConsentOnly },
#endif
				{ "Face", "bClimaxChoreo", &Face::bClimaxChoreo },
				{ "Face", "bVoiceArchetype", &Face::bVoiceArchetype },
				{ "Face", "bPositionalDomSub", &Face::bPositionalDomSub },
				{ "Face", "bRichEmotions", &Face::bRichEmotions },
				{ "Face", "bMouthVariety", &Face::bMouthVariety },
				{ "Face", "bDeviceAware", &Face::bDeviceAware },
				{ "Face", "bExposureAware", &Face::bExposureAware },
				{ "Face", "bYieldOralMouth", &Face::bYieldOralMouth },
				{ "Face", "bYieldMouthToPPA", &Face::bYieldMouthToPPA },
				{ "Face", "bDialogueMouthYield", &Face::bDialogueMouthYield },
				{ "Face", "bBodyDemo", &Face::bBodyDemo },
				{ "Face", "bExcitementGradient", &Face::bExcitementGradient },
				{ "Face", "bSpeedSync", &Face::bSpeedSync },
				{ "Face", "bRoleMetadata", &Face::bRoleMetadata },
#if !OSIS_LITE
				{ "Face", "bAggressorGrammar", &Face::bAggressorGrammar },
				{ "Face", "bSpellNonConsent", &Face::bSpellNonConsent },
				{ "Face", "bNCSceneLock", &Face::bNCSceneLock },
				{ "Face", "fNCAutoInterval", &Face::fNCAutoInterval },
#endif
				{ "Face", "bPhraseGrammar", &Face::bPhraseGrammar },
#if !OSIS_LITE
				{ "Face", "bConsentGuardrails", &Face::bConsentGuardrails },
				{ "Face", "bHardExclusionGate", &Face::bHardExclusionGate },
				{ "Face", "bNoDistressOverwhelm", &Face::bNoDistressOverwhelm },
				{ "Face", "bBrokenAfterClimax", &Face::bBrokenAfterClimax },
				{ "Face", "bConsentExcitement", &Face::bConsentExcitement },
				{ "Face", "fVictimExcitementMult", &Face::fVictimExcitementMult },
				{ "Face", "fAggressorExcitementMult", &Face::fAggressorExcitementMult },
#endif
				{ "Face", "bHeadflow", &Face::bHeadflow },
				{ "Face", "bGroupConductor", &Face::bGroupConductor },
				{ "Face", "bScenarioCycler", &Face::bScenarioCycler },
				{ "Face", "bOverwhelmFace", &Face::bOverwhelmFace },
				{ "Face", "bNormalState", &Face::bNormalState },
				{ "Face", "fNormalIntensity", &Face::fNormalIntensity },
				{ "Face", "fNormalGazeFrequency", &Face::fNormalGazeFrequency },
				{ "Face", "bNormalPreWarm", &Face::bNormalPreWarm },
				{ "Face", "bNormalGlancePlayer", &Face::bNormalGlancePlayer },
				{ "Face", "bWatcher", &Face::bWatcher },
				{ "Face", "bAhegaoModYield", &Face::bAhegaoModYield },
				{ "Face", "bAhegaoAutoYield", &Face::bAhegaoAutoYield },
				{ "Face", "bAnimeTongue", &Face::bAnimeTongue },
				{ "Face", "bAnimeTongueFull", &Face::bAnimeTongueFull },
				{ "Face", "bTongueLife", &Face::bTongueLife },
				{ "Face", "fAnimeStart", &Face::fAnimeStart },
				{ "Face", "fAnimeEnd", &Face::fAnimeEnd },
				{ "Face", "bSPIDPersonality", &Face::bSPIDPersonality },
				{ "Face", "bOBlushSync", &Face::bOBlushSync },
				{ "Face", "iPlayerPersonality", &Face::iPlayerPersonality },

				{ "Body", "bEnabled", &Body::bEnabled },
				{ "Body", "fStrength", &Body::fStrength },
				{ "Body", "bToe", &Body::bToe },
				{ "Body", "bHand", &Body::bHand },
				{ "Body", "bStyleGated", &Body::bStyleGated },
				{ "Body", "fToeDegrees", &Body::fToeDegrees },
				{ "Body", "fPerToe", &Body::fPerToe },
				{ "Body", "fFingerDegrees", &Body::fFingerDegrees },
				{ "Body", "bFootFlex", &Body::bFootFlex },
				{ "Body", "fFootFlexScale", &Body::fFootFlexScale },
				{ "Body", "iCurlAxis", &Body::iCurlAxis },
				{ "Body", "bGenitals", &Body::bGenitals },
				{ "Body", "fGenitalDegrees", &Body::fGenitalDegrees },
				{ "Body", "iGenitalAxis", &Body::iGenitalAxis },

				{ "Skin", "bEnabled", &Skin::bEnabled },
				{ "Skin", "fStrength", &Skin::fStrength },
				{ "Skin", "bBlush", &Skin::bBlush },
#if !OSIS_LITE
				{ "Skin", "bTears", &Skin::bTears },
				{ "Skin", "bEmoTears", &Skin::bEmoTears },
#endif
				{ "Skin", "bSaliva", &Skin::bSaliva },
				{ "Skin", "bStyleGated", &Skin::bStyleGated },
				{ "Skin", "bFemaleOnly", &Skin::bFemaleOnly },
				{ "Skin", "bMatteOverlays", &Skin::bMatteOverlays },
				{ "Skin", "iFaceFirstSlot", &Skin::iFaceFirstSlot },
				{ "Skin", "sBlushPath", &Skin::sBlushPath },
#if !OSIS_LITE
				{ "Skin", "sTearPath", &Skin::sTearPath },
#endif
				{ "Skin", "sSalivaPath", &Skin::sSalivaPath },

				{ "LipSync", "bEnabled", &LipSync::bEnabled },
				{ "LipSync", "fGain", &LipSync::fGain },
				{ "LipSync", "fAttack", &LipSync::fAttack },
				{ "LipSync", "fRelease", &LipSync::fRelease },
				{ "LipSync", "fMaxOpen", &LipSync::fMaxOpen },
				{ "LipSync", "bHoldEyes", &LipSync::bHoldEyes },
				{ "LipSync", "iTongueMode", &LipSync::iTongueMode },
				{ "LipSync", "fTongueMinOpen", &LipSync::fTongueMinOpen },
				{ "LipSync", "bYieldToDDF", &LipSync::bYieldToDDF },

#if !OSIS_LITE
				{ "Voice", "bEnabled", &Voice::bEnabled },
				{ "Voice", "bVictimNoMoans", &Voice::bVictimNoMoans },
				{ "Voice", "iVictimVoice", &Voice::iVictimVoice },
				{ "Voice", "fInterval", &Voice::fInterval },
				{ "Voice", "bMuteDialogue", &Voice::bMuteDialogue },
				{ "Voice", "bCallForHelp", &Voice::bCallForHelp },
				{ "Voice", "iResponders", &Voice::iResponders },
				{ "Voice", "fResponderRange", &Voice::fResponderRange },
				{ "Voice", "bBreakScream", &Voice::bBreakScream },
				{ "Voice", "fShockSeconds", &Voice::fShockSeconds },
#endif

				{ "Arousal", "bEnabled", &Arousal::bEnabled },
				{ "Arousal", "bAffectPlayer", &Arousal::bAffectPlayer },
				{ "Arousal", "bAffectNPCs", &Arousal::bAffectNPCs },
				{ "Arousal", "iSource", &Arousal::iSource },
				{ "Arousal", "bOStimExcitement", &Arousal::bOStimExcitement },
				{ "Arousal", "bSceneFactors", &Arousal::bSceneFactors },
				{ "Arousal", "bPersonality", &Arousal::bPersonality },
				{ "Arousal", "fClimaxHold", &Arousal::fClimaxHold },
				{ "Arousal", "iMaxNPCs", &Arousal::iMaxNPCs },
				{ "Arousal", "fIntensity", &Arousal::fIntensity },
				{ "Arousal", "fInterval", &Arousal::fInterval },
				{ "Arousal", "fRadius", &Arousal::fRadius },
				{ "Arousal", "fRiseHalfLife", &Arousal::fRiseHalfLife },
				{ "Arousal", "fFallHalfLife", &Arousal::fFallHalfLife },
				{ "Arousal", "bBlush", &Arousal::bBlush },
				{ "Arousal", "iOverlayFirstSlot", &Arousal::iOverlayFirstSlot },
				{ "Arousal", "iOverlaySlots", &Arousal::iOverlaySlots },
			};
			return rows;
		}

		void Sanitize()
		{
			Face::iMode = std::clamp(Face::iMode, 0, 2);
			Face::fGlobalStrength = std::clamp(Face::fGlobalStrength, 0.0f, 2.0f);
			Face::fDirectorGain = std::clamp(Face::fDirectorGain, 0.0f, 2.0f);
			Face::fBaseInterval = std::clamp(Face::fBaseInterval, 1.0f, 12.0f);
			Face::fIntervalJitter = std::clamp(Face::fIntervalJitter, 0.0f, 4.0f);
			Face::fTransition = std::clamp(Face::fTransition, 0.05f, 3.0f);
			Face::iProfile = std::clamp(Face::iProfile, 0, 2);
			Face::fStyle = std::clamp(Face::fStyle, 0.0f, 2.0f);
			Face::fEyeStrength = std::clamp(Face::fEyeStrength, 0.0f, 1.5f);
			Face::iPlayerPersonality = std::clamp(Face::iPlayerPersonality, -1, 4);
			Face::fNCAutoInterval = std::clamp(Face::fNCAutoInterval, 5.0f, 120.0f);
			if (Face::fAnimeEnd > Face::fAnimeStart) Face::fAnimeEnd = Face::fAnimeStart;
			Body::iCurlAxis = std::clamp(Body::iCurlAxis, 0, 2);
			Body::fStrength = std::clamp(Body::fStrength, 0.0f, 1.0f);
			Body::fToeDegrees = std::clamp(Body::fToeDegrees, 0.0f, 90.0f);
			Body::fFingerDegrees = std::clamp(Body::fFingerDegrees, 0.0f, 120.0f);
			Body::fPerToe = std::clamp(Body::fPerToe, 0.0f, 3.0f);
			Body::fFootFlexScale = std::clamp(Body::fFootFlexScale, 0.0f, 1.0f);
			Body::fGenitalDegrees = std::clamp(Body::fGenitalDegrees, 0.0f, 120.0f);
			Body::iGenitalAxis = std::clamp(Body::iGenitalAxis, 0, 2);
			Skin::iFaceFirstSlot = std::clamp(Skin::iFaceFirstSlot, 0, 15);
			Arousal::iSource = std::clamp(Arousal::iSource, 0, 3);
			Arousal::fInterval = std::clamp(Arousal::fInterval, 0.25f, 30.0f);
			Arousal::fRiseHalfLife = std::max(0.5f, Arousal::fRiseHalfLife);
			Arousal::fFallHalfLife = std::max(0.5f, Arousal::fFallHalfLife);
			Arousal::fClimaxHold = std::clamp(Arousal::fClimaxHold, 0.0f, 60.0f);
			Arousal::iOverlayFirstSlot = std::max(0, Arousal::iOverlayFirstSlot);
			Arousal::iOverlaySlots = std::clamp(Arousal::iOverlaySlots, 0, 32);
			LipSync::fAttack = std::clamp(LipSync::fAttack, 0.005f, 0.5f);
			LipSync::fRelease = std::clamp(LipSync::fRelease, 0.02f, 1.0f);
			LipSync::iTongueMode = std::clamp(LipSync::iTongueMode, 0, 2);
			LipSync::fTongueMinOpen = std::clamp(LipSync::fTongueMinOpen, 0.0f, 1.0f);
			Voice::iVictimVoice = std::clamp(Voice::iVictimVoice, 0, 2);
			Voice::iResponders = std::clamp(Voice::iResponders, 0, 2);
			Voice::fInterval = std::clamp(Voice::fInterval, 3.0f, 60.0f);
			Voice::fResponderRange = std::clamp(Voice::fResponderRange, 256.0f, 8192.0f);
			Voice::fShockSeconds = std::clamp(Voice::fShockSeconds, 0.0f, 10.0f);
		}

		void ReadIni(const CSimpleIniA& ini)
		{
			for (const auto& b : Bindings()) {
				const char* raw = ini.GetValue(b.section, b.key, nullptr);
				if (!raw) continue;
				// Values may carry inline "; comment" tails; strip them before parsing.
				std::string v = raw;
				if (auto semi = v.find(';'); semi != std::string::npos) v.erase(semi);
				while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
				std::visit(
					[&](auto* p) {
						using T = std::remove_pointer_t<decltype(p)>;
						if constexpr (std::is_same_v<T, bool>) {
							*p = v == "1" || _stricmp(v.c_str(), "true") == 0;
						} else if constexpr (std::is_same_v<T, int>) {
							*p = std::atoi(v.c_str());
						} else if constexpr (std::is_same_v<T, float>) {
							*p = static_cast<float>(std::atof(v.c_str()));
						} else {
							*p = v;
						}
					},
					b.ref);
			}
		}
	}

	namespace Arousal
	{
		std::vector<Morph> DefaultMorphs()
		{
			// GT Softbody (CBBE) slider names. Excitement: nipple erection, areola
			// tightening, early labial/clitoral swelling. Plateau: areola puffing,
			// labia parting and protruding, slight breast volume increase.
			return {
				{ "NipBGone", 0.05f, 0.35f, 0.00f, true, 0.30f },
				{ "NippleShy_v2", 0.05f, 0.35f, 0.00f, true, 0.30f },
				{ "NipplePerkiness", 0.05f, 0.40f, 0.45f },
				{ "NippleTube_v2", 0.10f, 0.50f, 0.25f },
				{ "NippleThicc_v2", 0.15f, 0.60f, 0.30f },
				{ "NippleManga", 0.10f, 0.50f, 0.80f },
				{ "NippleSize", 0.15f, 0.60f, -0.10f },  // inverted slider: negative = larger
				{ "AreolaSize", 0.10f, 0.50f, -0.20f },
				{ "AreolaPull_v2", 0.10f, 0.50f, -0.15f },
				{ "NipplePuffy_v2", 0.55f, 0.90f, 0.60f },
				{ "Breasts", 0.40f, 1.00f, 0.06f },
				{ "LabiaNeat_v2", 0.10f, 0.50f, 0.00f, true, 0.30f },
				{ "Innieoutie", 0.10f, 0.50f, 0.00f, true, 0.30f },
				{ "Labiapuffyness", 0.10f, 0.60f, 0.15f },
				{ "ClitSwell_v2", 0.20f, 0.70f, 0.60f },
				{ "Clit", 0.20f, 0.70f, 0.60f },
				{ "LabiaMorePuffyness_v2", 0.25f, 0.80f, 0.40f },
				{ "Cutepuffyness", 0.30f, 0.90f, 0.40f },
				{ "LabiaBulgogi_v2", 0.45f, 0.95f, 0.35f },
				{ "Labiaprotrude2", 0.40f, 0.90f, 0.35f },
				{ "Labiaspread", 0.45f, 0.95f, 0.40f },
				{ "VaginaHole", 0.65f, 1.00f, 0.15f },
			};
		}

		// Tuned so the flush reads about the same on every skin. Pale races need less alpha than
		// the tint already gives them; ashen, green and dark-brown skin need considerably more.
		std::vector<RaceBlush> DefaultRaceBlush()
		{
			return {
				{ "nord", 0.85f },
				{ "imperial", 0.85f },
				{ "breton", 0.85f },
				{ "highelf", 0.90f },
				{ "elder", 0.90f },
				{ "woodelf", 1.05f },
				{ "darkelf", 1.35f },
				{ "orc", 1.40f },
				{ "redguard", 1.60f },
			};
		}

		std::vector<Blush> DefaultBlushes()
		{
			// Sexual flush spreads from the upper chest outward. The textures' own
			// alpha is only ~20-33%, so these run near full opacity.
			return {
				{ "Blush_Chest_Upper", 0.25f, 0.70f, 1.00f },
				{ "Blush_Chest_Center", 0.30f, 0.75f, 1.00f },
				{ "Blush_Breast", 0.35f, 0.85f, 0.90f },
				{ "Blush_Coochie", 0.35f, 0.85f, 1.00f },
				{ "Blush_Thigh_Inside", 0.45f, 0.95f, 0.90f },
				{ "Blush_Shoulder", 0.55f, 1.00f, 0.80f },
			};
		}
	}

	void Load()
	{
		{
			std::scoped_lock l(lock);
			CSimpleIniA ini;
			ini.SetUnicode();
			if (ini.LoadFile(kIniPath) < 0) {
				logger::warn("Could not load {} - using defaults.", kIniPath);
			} else {
				ReadIni(ini);
			}
			Sanitize();
		}
		LoadTables();
		spdlog::set_level(General::bDebug ? spdlog::level::debug : spdlog::level::info);
	}

	namespace
	{
		// The comment above a section in a file the plugin writes itself. Every line starts with the
		// comment character, which is how SimpleIni takes a comment. The first section of a brand
		// new file also says what the file is.
		std::string SectionNote(const std::string& section, bool fileIsNew)
		{
			static const std::unordered_map<std::string, std::string> notes = {
				{ "General",
					"; Master switches. Everything here is also on the SKSE Menu Framework pages (default key F1).\n"
					"; bAnimationHooks is read at startup: leave it on unless another mod's hook on the same calls conflicts." },
				{ "Face", "; Face engine. iMode: 0 Assist, 1 Enhanced, 2 Director (OSED owns the face). fStyle: 0 realistic, 1 cinematic, 2 anime." },
				{ "Body", "; Toe curl / hand grip at climax. iCurlAxis: 0 X, 1 Y, 2 Z (bone-local)." },
				{ "Skin",
					"; Face overlays (\"Face [Ovl#]\"). Texture paths are relative to Data\textures; an empty blush path auto-uses Female Makeup Suite's cheek blush if installed."
#if !OSIS_LITE
					" Tears only appear in non-consensual scenes, on the victim."
#endif
				},
				{ "LipSync",
					"; Mouth follows the moan OStim plays (decoded from your voice-set .wav files at startup).\n"
					"; bYieldToDDF: stand down while Dynamic Dialogue Framework is installed (both drive the mouth)." },
				{ "Voice",
					"; The victim of a non-consensual scene: OStim moans muted, a cry for help that guards/allies answer, personality lines, a scream at the breaking climax, then hard breathing. Vanilla Skyrim.esm lines only.\n"
					"; iVictimVoice: 0 silent, 1 breathing only, 2 full. iResponders: 0 nobody, 1 guards, 2 guards and allies." },
				{ "Arousal",
					"; Softbody Arousal. iSource: 0 auto (OSL first), 1 OSL Aroused, 2 SLO Aroused NG, 3 OStim excitement only.\n"
					"; Body blush uses \"Body [Ovl#]\" slots iOverlayFirstSlot .. +iOverlaySlots-1; set skee64.ini [Overlays/Body] iNumOverlays to at least 12." },
			};
			std::string note;
			if (fileIsNew && section == "General") {
				note = "; OStim Standalone Immersive Sex - edit in game via SKSE Menu Framework (OSIS section).\n"
				       "; The plugin creates this file, adds any setting a newer version introduces, and rewrites it when you press Save,\n"
				       "; so updating the mod never replaces your settings.\n";
			}
			if (const auto it = notes.find(section); it != notes.end()) note += it->second;
			return note;
		}

		bool SaveIni()
		{
			CSimpleIniA ini;
			ini.SetUnicode();
			const bool existed = ini.LoadFile(kIniPath) >= 0;  // keeps the comments, and any key we do not know
			{
				std::scoped_lock l(lock);
				Sanitize();
				for (const auto& b : Bindings()) {
					// A section new to this file gets its comment. One that is already there keeps whatever
					// it has, the user's own edits included.
					if (!ini.GetSection(b.section)) {
						const auto note = SectionNote(b.section, !existed);
						ini.SetValue(b.section, nullptr, nullptr, note.empty() ? nullptr : note.c_str());
					}
					std::visit(
						[&](auto* p) {
							using T = std::remove_pointer_t<decltype(p)>;
							if constexpr (std::is_same_v<T, bool>) {
								ini.SetBoolValue(b.section, b.key, *p);
							} else if constexpr (std::is_same_v<T, int>) {
								ini.SetLongValue(b.section, b.key, *p);
							} else if constexpr (std::is_same_v<T, float>) {
								ini.SetValue(b.section, b.key, std::format("{:.3f}", *p).c_str());
							} else {
								ini.SetValue(b.section, b.key, p->c_str());
							}
						},
						b.ref);
				}
			}
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(kIniPath).parent_path(), ec);
			if (ini.SaveFile(kIniPath) < 0) {
				logger::error("Failed to write {}", kIniPath);
				return false;
			}
			return true;
		}
	}

	bool Save()
	{
		return SaveIni() && SaveTables();
	}

	// The release ships neither OSIS.ini nor morphs.json: installing a mod folder over an old one
	// replaces both, and with them whatever the user had chosen. The plugin makes them itself. A
	// missing INI is written with the defaults; one from an older version has the settings this
	// version introduced added to it, its values and comments left alone. The tables are only
	// written when absent, since an existing one is the user's to edit.
	void EnsureFiles()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		const bool existed = ini.LoadFile(kIniPath) >= 0;
		std::size_t missing = 0;
		for (const auto& b : Bindings()) {
			if (!ini.GetValue(b.section, b.key)) ++missing;
		}
		if (missing && SaveIni()) {
			if (existed) logger::info("{}: added {} setting(s) introduced by this version.", kIniPath, missing);
			else logger::info("{}: created with the default settings.", kIniPath);
		}
		std::error_code ec;
		if (!std::filesystem::exists(kTablePath, ec) && SaveTables()) {
			logger::info("{}: created with the default morph and blush tables.", kTablePath);
		}
	}

	void LoadTables()
	{
		using namespace Arousal;
		std::vector<Morph> loaded;
		std::vector<Blush> loadedBlush;
		std::vector<RaceBlush> loadedRace;
		bool haveBlush = false;
		bool haveRace = false;
		try {
			std::ifstream f(kTablePath);
			if (f) {
				const auto doc = json::parse(f);
				if (doc.contains("blush")) {
					haveBlush = true;
					for (const auto& j : doc.at("blush")) {
						Blush b;
						b.name = j.at("name").get<std::string>();
						b.start = j.value("start", 0.0f);
						b.full = j.value("full", 1.0f);
						b.texture = j.value("texture", std::string{});
						b.sex = std::clamp(j.value("sex", static_cast<int>(kFemaleBody)), 0, 2);
						b.tint = j.value("tint", -1);
						b.max = std::clamp(j.value("max", 1.0f), 0.0f, 1.0f);
						b.enabled = j.value("enabled", true);
						if (b.full <= b.start) b.full = b.start + 0.01f;
						loadedBlush.push_back(std::move(b));
					}
				}
				if (doc.contains("raceBlush")) {
					haveRace = true;
					for (const auto& j : doc.at("raceBlush")) {
						RaceBlush r;
						r.race = j.at("race").get<std::string>();
						std::ranges::transform(r.race, r.race.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						r.mult = std::clamp(j.value("mult", 1.0f), 0.0f, 3.0f);
						if (!r.race.empty()) loadedRace.push_back(std::move(r));
					}
				}
				for (const auto& j : doc.at("morphs")) {
					Morph m;
					m.name = j.at("name").get<std::string>();
					m.start = j.value("start", 0.0f);
					m.full = j.value("full", 1.0f);
					m.max = j.value("max", 0.0f);
					m.enabled = j.value("enabled", true);
					m.sex = std::clamp(j.value("sex", static_cast<int>(kFemaleBody)), 0, 2);
					m.rest = j.value("rest", 0.0f);
					if (m.full <= m.start) m.full = m.start + 0.01f;
					loaded.push_back(std::move(m));
				}
			}
		} catch (const std::exception& e) {
			logger::error("{}: {} - using built-in morph table.", kTablePath, e.what());
			loaded.clear();
			loadedBlush.clear();
			loadedRace.clear();
			haveBlush = false;
			haveRace = false;
		}
		if (loaded.empty()) loaded = DefaultMorphs();
		if (!haveBlush) loadedBlush = DefaultBlushes();
		if (!haveRace) loadedRace = DefaultRaceBlush();

		std::scoped_lock l(lock);
		morphs = std::move(loaded);
		blushes = std::move(loadedBlush);
		raceBlush = std::move(loadedRace);
		logger::info("{} softbody morphs, {} body-blush regions, {} per-race blush multiplier(s) configured.",
			morphs.size(), blushes.size(), raceBlush.size());
	}

	bool SaveTables()
	{
		json arr = json::array();
		json blushArr = json::array();
		json raceArr = json::array();
		{
			std::scoped_lock l(lock);
			for (const auto& m : Arousal::morphs) {
				arr.push_back({ { "name", m.name }, { "sex", m.sex }, { "start", m.start }, { "full", m.full }, { "max", m.max }, { "rest", m.rest },
					{ "enabled", m.enabled } });
			}
			for (const auto& b : Arousal::blushes) {
				blushArr.push_back({ { "name", b.name }, { "texture", b.texture }, { "sex", b.sex }, { "tint", b.tint }, { "start", b.start },
					{ "full", b.full }, { "max", b.max }, { "enabled", b.enabled } });
			}
			for (const auto& r : Arousal::raceBlush) {
				raceArr.push_back({ { "race", r.race }, { "mult", r.mult } });
			}
		}
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(kTablePath).parent_path(), ec);
		std::ofstream f(kTablePath);
		if (!f) {
			logger::error("Failed to write {}", kTablePath);
			return false;
		}
		f << json{ { "morphs", arr }, { "blush", blushArr }, { "raceBlush", raceArr } }.dump(4);
		return true;
	}

	void RestoreDefaults()
	{
		std::scoped_lock l(lock);
		General::bEnabled = true;
		General::bIncludeNPCs = true;
		General::bNPCOnlyScenes = true;
		General::fNPCSceneRadius = 4000.0f;
		General::bPulseBus = true;
		General::bDebug = false;
		ApplyPreset(0);
	}

	// Ported from OSExpressionFaces.ApplyPreset. Presets only touch the face engine.
	void ApplyPreset(int p)
	{
		std::scoped_lock l(lock);
		using namespace Face;
		const bool full = p != 4;
		fIntervalJitter = 1.0f;
		bDeviceAware = full;
		bPositionalDomSub = full;
		bRichEmotions = full;
		bMouthVariety = full;
		bExposureAware = full;
		bYieldOralMouth = true;
		bYieldMouthToPPA = true;
		bDialogueMouthYield = true;
		bBodyDemo = false;
		bExcitementGradient = true;
		bSpeedSync = true;
		bRoleMetadata = true;
		bAggressorGrammar = true;
		bSpellNonConsent = true;
		bNCSceneLock = true;
		bPhraseGrammar = full;
		bConsentGuardrails = true;
		bHardExclusionGate = true;
		bNoDistressOverwhelm = true;
		bBrokenAfterClimax = true;
		bConsentExcitement = true;
		bHeadflow = full;
		bGroupConductor = full;
		bScenarioCycler = full;
		bOverwhelmFace = full;
		bNormalState = true;
		fNormalIntensity = 0.35f;
		fNormalGazeFrequency = 0.55f;
		bNormalPreWarm = true;
		bNormalGlancePlayer = true;
		bWatcher = false;
		bAhegaoModYield = false;
		bAhegaoAutoYield = true;
		fStyle = 0.0f;
		fEyeStrength = 0.45f;
		iMode = kDirector;
		fDirectorGain = 1.0f;
		bDirectorLibrary = true;
		bTakeOverFace = false;
		bAnimeTongue = false;
		bAnimeTongueFull = false;
		bTongueLife = false;
		bSPIDPersonality = true;
		bOBlushSync = true;
		fAnimeStart = 85.0f;
		fAnimeEnd = 70.0f;

		bRoleAware = full;
		bActTypeAware = full;
		bPaceBoost = full;
		bCinematic = full;
		bBreathing = false;
		bNaturalDetail = p != 3 && p != 4;
		bGaze = p != 3 && p != 4;
		bClimaxChoreo = full;

		switch (p) {
		case 1:  // Subtle
			fGlobalStrength = 0.65f;
			fBaseInterval = 3.5f;
			fTransition = 0.60f;
			iProfile = 0;
			fEyeStrength = 0.35f;
			break;
		case 2:  // Cinematic
			fGlobalStrength = 1.00f;
			fBaseInterval = 2.5f;
			fTransition = 0.45f;
			iProfile = 2;
			fStyle = 1.0f;
			fEyeStrength = 0.50f;
			break;
		case 3:  // Performance
			fGlobalStrength = 0.85f;
			fBaseInterval = 4.5f;
			fTransition = 0.60f;
			iProfile = 1;
			fEyeStrength = 0.40f;
			break;
		case 4:  // Minimal
			fGlobalStrength = 0.80f;
			fBaseInterval = 3.0f;
			fTransition = 0.50f;
			iProfile = 1;
			fEyeStrength = 0.35f;
			break;
		default:  // Recommended
			fGlobalStrength = 0.85f;
			fBaseInterval = 3.0f;
			fTransition = 0.50f;
			iProfile = 1;
			break;
		}
	}
}
