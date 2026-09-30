#pragma once

// All user settings. The SKSE Menu Framework pages edit these on the render thread while
// the engine reads them on the game thread, so every access holds Settings::lock.
// Persisted to Data/SKSE/Plugins/OSEDReborn.ini (+ OSEDReborn/morphs.json for the
// softbody morph and body-blush tables).
namespace Settings
{
	inline std::recursive_mutex lock;

	// ---------------------------------------------------------------- general
	namespace General
	{
		inline bool bEnabled = true;
		inline bool bIncludeNPCs = true;      // paint NPCs in the player's scene
		inline bool bNPCOnlyScenes = true;    // also direct scenes without the player (OStim NPCs etc.)
		inline float fNPCSceneRadius = 4000.0f;  // NPC-only scenes farther than this are ignored
		inline bool bPulseBus = true;         // emit SLED_* / OSED_* mod events for other mods
		inline bool bDebug = false;
	}

	// ---------------------------------------------------------------- face (OSED core)
	namespace Face
	{
		enum Mode : int { kAssist = 0, kEnhanced = 1, kDirector = 2 };

		inline int iMode = kDirector;         // Assist/Enhanced layer over OStim, or Director owns the face
		inline float fGlobalStrength = 0.85f;
		inline float fBaseInterval = 3.0f;
		inline float fIntervalJitter = 1.0f;
		inline float fTransition = 0.5f;
		inline int iProfile = 1;              // 0 subtle, 1 normal, 2 expressive
		inline float fStyle = 0.0f;           // 0 realistic, 1 cinematic, 2 anime
		inline float fEyeStrength = 0.45f;
		inline bool bTakeOverFace = false;    // Assist/Enhanced: switch OStim's face writer off for painted actors (Director always does)

		inline bool bRoleAware = true;
		inline bool bActTypeAware = true;
		inline bool bPaceBoost = true;
		inline bool bCinematic = true;
		inline bool bBreathing = false;
		inline bool bNaturalDetail = true;
		inline bool bGaze = true;
		inline bool bGazeConsentOnly = false;
		inline bool bClimaxChoreo = true;
		inline bool bVoiceArchetype = true;
		inline bool bPositionalDomSub = true;
		inline bool bRichEmotions = true;
		inline bool bMouthVariety = true;
		inline bool bDeviceAware = true;
		inline bool bExposureAware = true;
		inline bool bYieldOralMouth = true;
		inline bool bDialogueMouthYield = true;
		inline bool bBodyDemo = false;
		inline bool bExcitementGradient = true;
		inline bool bSpeedSync = true;
		inline bool bRoleMetadata = true;
		inline bool bEventBeats = true;
		inline bool bAggressorGrammar = true;
		inline bool bSpellNonConsent = true;          // scenes started by the player's spell (Matchmaker...) are non-consensual
		inline bool bPhraseGrammar = true;
		inline bool bConsentGuardrails = true;
		inline bool bHardExclusionGate = true;
		inline bool bNoDistressOverwhelm = true;
		inline bool bBrokenAfterClimax = true;        // a victim who climaxes goes vacant for the rest of the scene
		inline bool bConsentExcitement = true;        // non-consent: scale OStim excitement rates by role
		inline float fVictimExcitementMult = 0.5f;    // relative to OStim's own (MCM) rate
		inline float fAggressorExcitementMult = 1.5f;
		inline bool bHeadflow = true;
		inline bool bGroupConductor = true;
		inline bool bScenarioCycler = true;
		inline bool bOverwhelmFace = true;

		inline bool bNormalState = true;
		inline float fNormalIntensity = 0.35f;
		inline float fNormalGazeFrequency = 0.55f;
		inline bool bNormalPreWarm = true;
		inline bool bNormalGlancePlayer = true;
		inline bool bWatcher = false;

		inline bool bAhegaoModYield = false;
		inline bool bAnimeTongue = false;
		inline bool bAnimeTongueFull = false;
		inline bool bTongueLife = false;
		inline float fAnimeStart = 85.0f;
		inline float fAnimeEnd = 70.0f;

		inline bool bSPIDPersonality = true;
		inline bool bOBlushSync = true;
		inline int iPlayerPersonality = -1;   // -1 auto, 0 balanced, 1 stoic, 2 vocal, 3 shy, 4 dominant
	}

	// ---------------------------------------------------------------- body (toe / hand)
	namespace Body
	{
		inline bool bEnabled = true;
		inline float fStrength = 0.75f;
		inline bool bToe = true;
		inline bool bHand = true;
		inline bool bStyleGated = true;
		inline float fToeDegrees = 35.0f;
		inline float fFingerDegrees = 45.0f;
		inline int iCurlAxis = 0;             // 0 X, 1 Y, 2 Z (bone-local)
	}

	// ---------------------------------------------------------------- living skin (face overlays)
	namespace Skin
	{
		inline bool bEnabled = true;
		inline float fStrength = 1.0f;
		inline bool bBlush = true;
		inline bool bTears = true;            // non-consensual scenes only, on the victim
		inline bool bEmoTears = true;         // also Emotional Tears Effect (EmoTearsSpells.esp) if installed
		inline bool bSaliva = true;
		inline bool bStyleGated = true;
		inline bool bFemaleOnly = false;
		inline int iFaceFirstSlot = 1;        // first "Face [Ovl#]" slot used (0 is often makeup)
		inline std::string sBlushPath = "";
		inline std::string sTearPath = "";
		inline std::string sSalivaPath = "";
	}

	// ---------------------------------------------------------------- lip-sync
	namespace LipSync
	{
		inline bool bEnabled = true;
		inline float fGain = 1.0f;            // mouth opening per unit loudness
		inline float fAttack = 0.04f;         // envelope attack (s)
		inline float fRelease = 0.12f;        // envelope release (s)
		inline float fMaxOpen = 0.85f;
		inline bool bHoldEyes = true;         // squint with the moan while it plays
	}

	// ---------------------------------------------------------------- victim voice
	namespace Voice
	{
		inline bool bEnabled = true;
		inline bool bVictimNoMoans = true;    // mute OStim's moans and climax sounds on the victim
		inline int iVictimVoice = 2;          // 0 silent, 1 breathing only, 2 full (help, lines, scream)
		inline float fInterval = 9.0f;        // seconds between lines (+-40%)
		inline bool bMuteDialogue = true;     // OActor.Mute on the victim: no OStim scene comments
		inline bool bCallForHelp = true;
		inline int iResponders = 2;           // 0 off, 1 guards, 2 guards and allies
		inline float fResponderRange = 2048.0f;
		inline bool bBreakScream = true;
		inline float fShockSeconds = 3.0f;    // shocked face after the breaking climax (0 off)
	}

	// ---------------------------------------------------------------- softbody arousal
	namespace Arousal
	{
		enum Source : int { kAuto = 0, kOSL = 1, kSLO = 2, kOStimOnly = 3 };

		struct Morph
		{
			std::string name;
			float start = 0.0f;  // tissue level where the slider begins to move
			float full = 1.0f;   // tissue level where it reaches max
			float max = 0.0f;    // slider value at full (negative allowed)
			bool enabled = true;
			float rest = 0.0f;   // slider value while unaroused
		};

		struct Blush
		{
			std::string name;    // Body Blushing texture name, no folder or .dds
			float start = 0.0f;
			float full = 1.0f;
			float max = 1.0f;    // overlay alpha at full
			bool enabled = true;
		};

		inline bool bEnabled = true;
		inline bool bAffectPlayer = true;
		inline bool bAffectNPCs = true;
		inline int iSource = kAuto;
		inline bool bOStimExcitement = true;  // in a scene, OStim excitement is an arousal floor
		inline bool bSceneFactors = true;     // climax spike, plateau hold and afterglow from the face engine
		inline bool bPersonality = true;      // archetype changes response speed and flush strength
		inline float fClimaxHold = 8.0f;      // seconds of full engorgement after an orgasm
		inline int iMaxNPCs = 6;
		inline float fIntensity = 1.0f;
		inline float fInterval = 1.0f;
		inline float fRadius = 3000.0f;
		inline float fRiseHalfLife = 10.0f;
		inline float fFallHalfLife = 45.0f;

		inline bool bBlush = true;
		inline int iOverlayFirstSlot = 6;
		inline int iOverlaySlots = 6;

		inline std::vector<Morph> morphs;
		inline std::vector<Blush> blushes;

		std::vector<Morph> DefaultMorphs();
		std::vector<Blush> DefaultBlushes();
	}

	void Load();
	bool Save();
	void LoadTables();
	bool SaveTables();
	void RestoreDefaults();  // everything except the morph / blush tables
	void ApplyPreset(int a_preset);  // 0 Recommended, 1 Subtle, 2 Cinematic, 3 Performance, 4 Minimal

	[[nodiscard]] inline float StyleValue() { return std::clamp(Face::fStyle, 0.0f, 2.0f); }
}
