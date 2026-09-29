#include "Settings.h"

namespace Settings
{
	namespace
	{
		constexpr auto kIniPath = "Data/SKSE/Plugins/OSEDReborn.ini";
		constexpr auto kTablePath = "Data/SKSE/Plugins/OSEDReborn/morphs.json";

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
				{ "General", "bDebug", &General::bDebug },

				{ "Face", "iMode", &Face::iMode },
				{ "Face", "fGlobalStrength", &Face::fGlobalStrength },
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
				{ "Face", "bGazeConsentOnly", &Face::bGazeConsentOnly },
				{ "Face", "bClimaxChoreo", &Face::bClimaxChoreo },
				{ "Face", "bVoiceArchetype", &Face::bVoiceArchetype },
				{ "Face", "bPositionalDomSub", &Face::bPositionalDomSub },
				{ "Face", "bRichEmotions", &Face::bRichEmotions },
				{ "Face", "bMouthVariety", &Face::bMouthVariety },
				{ "Face", "bDeviceAware", &Face::bDeviceAware },
				{ "Face", "bExposureAware", &Face::bExposureAware },
				{ "Face", "bYieldOralMouth", &Face::bYieldOralMouth },
				{ "Face", "bDialogueMouthYield", &Face::bDialogueMouthYield },
				{ "Face", "bBodyDemo", &Face::bBodyDemo },
				{ "Face", "bExcitementGradient", &Face::bExcitementGradient },
				{ "Face", "bSpeedSync", &Face::bSpeedSync },
				{ "Face", "bRoleMetadata", &Face::bRoleMetadata },
				{ "Face", "bEventBeats", &Face::bEventBeats },
				{ "Face", "bAggressorGrammar", &Face::bAggressorGrammar },
				{ "Face", "bSpellNonConsent", &Face::bSpellNonConsent },
				{ "Face", "bPhraseGrammar", &Face::bPhraseGrammar },
				{ "Face", "bConsentGuardrails", &Face::bConsentGuardrails },
				{ "Face", "bHardExclusionGate", &Face::bHardExclusionGate },
				{ "Face", "bNoDistressOverwhelm", &Face::bNoDistressOverwhelm },
				{ "Face", "bConsentExcitement", &Face::bConsentExcitement },
				{ "Face", "fVictimExcitementMult", &Face::fVictimExcitementMult },
				{ "Face", "fAggressorExcitementMult", &Face::fAggressorExcitementMult },
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
				{ "Body", "fFingerDegrees", &Body::fFingerDegrees },
				{ "Body", "iCurlAxis", &Body::iCurlAxis },

				{ "Skin", "bEnabled", &Skin::bEnabled },
				{ "Skin", "fStrength", &Skin::fStrength },
				{ "Skin", "bBlush", &Skin::bBlush },
				{ "Skin", "bTears", &Skin::bTears },
				{ "Skin", "bSaliva", &Skin::bSaliva },
				{ "Skin", "bStyleGated", &Skin::bStyleGated },
				{ "Skin", "bFemaleOnly", &Skin::bFemaleOnly },
				{ "Skin", "iFaceFirstSlot", &Skin::iFaceFirstSlot },
				{ "Skin", "sBlushPath", &Skin::sBlushPath },
				{ "Skin", "sTearPath", &Skin::sTearPath },
				{ "Skin", "sSalivaPath", &Skin::sSalivaPath },

				{ "LipSync", "bEnabled", &LipSync::bEnabled },
				{ "LipSync", "fGain", &LipSync::fGain },
				{ "LipSync", "fAttack", &LipSync::fAttack },
				{ "LipSync", "fRelease", &LipSync::fRelease },
				{ "LipSync", "fMaxOpen", &LipSync::fMaxOpen },
				{ "LipSync", "bHoldEyes", &LipSync::bHoldEyes },

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
			Face::fBaseInterval = std::clamp(Face::fBaseInterval, 1.0f, 12.0f);
			Face::fIntervalJitter = std::clamp(Face::fIntervalJitter, 0.0f, 4.0f);
			Face::fTransition = std::clamp(Face::fTransition, 0.05f, 3.0f);
			Face::iProfile = std::clamp(Face::iProfile, 0, 2);
			Face::fStyle = std::clamp(Face::fStyle, 0.0f, 2.0f);
			Face::fEyeStrength = std::clamp(Face::fEyeStrength, 0.0f, 1.5f);
			Face::iPlayerPersonality = std::clamp(Face::iPlayerPersonality, -1, 4);
			if (Face::fAnimeEnd > Face::fAnimeStart) Face::fAnimeEnd = Face::fAnimeStart;
			Body::iCurlAxis = std::clamp(Body::iCurlAxis, 0, 2);
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

	bool Save()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);  // keep comments
		{
			std::scoped_lock l(lock);
			Sanitize();
			for (const auto& b : Bindings()) {
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
		return SaveTables();
	}

	void LoadTables()
	{
		using namespace Arousal;
		std::vector<Morph> loaded;
		std::vector<Blush> loadedBlush;
		bool haveBlush = false;
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
						b.max = std::clamp(j.value("max", 1.0f), 0.0f, 1.0f);
						b.enabled = j.value("enabled", true);
						if (b.full <= b.start) b.full = b.start + 0.01f;
						loadedBlush.push_back(std::move(b));
					}
				}
				for (const auto& j : doc.at("morphs")) {
					Morph m;
					m.name = j.at("name").get<std::string>();
					m.start = j.value("start", 0.0f);
					m.full = j.value("full", 1.0f);
					m.max = j.value("max", 0.0f);
					m.enabled = j.value("enabled", true);
					m.rest = j.value("rest", 0.0f);
					if (m.full <= m.start) m.full = m.start + 0.01f;
					loaded.push_back(std::move(m));
				}
			}
		} catch (const std::exception& e) {
			logger::error("{}: {} - using built-in morph table.", kTablePath, e.what());
			loaded.clear();
			loadedBlush.clear();
			haveBlush = false;
		}
		if (loaded.empty()) loaded = DefaultMorphs();
		if (!haveBlush) loadedBlush = DefaultBlushes();

		std::scoped_lock l(lock);
		morphs = std::move(loaded);
		blushes = std::move(loadedBlush);
		logger::info("{} softbody morphs, {} body-blush regions configured.", morphs.size(), blushes.size());
	}

	bool SaveTables()
	{
		json arr = json::array();
		json blushArr = json::array();
		{
			std::scoped_lock l(lock);
			for (const auto& m : Arousal::morphs) {
				arr.push_back({ { "name", m.name }, { "start", m.start }, { "full", m.full }, { "max", m.max }, { "rest", m.rest }, { "enabled", m.enabled } });
			}
			for (const auto& b : Arousal::blushes) {
				blushArr.push_back({ { "name", b.name }, { "start", b.start }, { "full", b.full }, { "max", b.max }, { "enabled", b.enabled } });
			}
		}
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(kTablePath).parent_path(), ec);
		std::ofstream f(kTablePath);
		if (!f) {
			logger::error("Failed to write {}", kTablePath);
			return false;
		}
		f << json{ { "morphs", arr }, { "blush", blushArr } }.dump(4);
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
		bDialogueMouthYield = true;
		bBodyDemo = false;
		bExcitementGradient = true;
		bSpeedSync = true;
		bRoleMetadata = true;
		bEventBeats = true;
		bAggressorGrammar = true;
		bSpellNonConsent = true;
		bPhraseGrammar = full;
		bConsentGuardrails = true;
		bHardExclusionGate = true;
		bNoDistressOverwhelm = true;
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
		fStyle = 0.0f;
		fEyeStrength = 0.45f;
		iMode = kDirector;
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
