// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#pragma once

// All user settings. The SKSE Menu Framework pages edit these on the render thread while
// the engine reads them on the game thread, so every access holds Settings::lock.
// Persisted to Data/SKSE/Plugins/OSIS.ini (+ OSIS/morphs.json for the
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
		inline bool bAnimationHooks = true;   // per-frame face/toe writes after animation (read at startup)
		inline bool bDebug = false;
	}

	// ---------------------------------------------------------------- face (OSED core)
	namespace Face
	{
		enum Mode : int { kAssist = 0, kEnhanced = 1, kDirector = 2 };

		inline bool bEnabled = true;          // off: OSED paints no faces; the other modules keep running
		inline int iMode = kDirector;         // Assist/Enhanced layer over OStim, or Director owns the face
		inline float fGlobalStrength = 0.85f;
		// Director only: how far the pleasure phase is brought up toward OStim's own face amplitude.
		// 0 leaves the presets as authored, 1 matches OStim at the same excitement, 2 goes well beyond.
		inline float fDirectorGain = 1.0f;
		// Director only: in the pleasure and anticipation phases, play OStim's own expression pool for what the
		// actor is doing (the same files OStim reads) instead of the built-in templates.
		inline bool bDirectorLibrary = true;
		// Director: how long an actor's face stays on the climax after their own orgasm, in seconds. And the gap between two of
		// their orgasms under which the second counts as rapid: its climax face is cut shorter, its afterglow skipped or cut short,
		// and the face between orgasms carries a tension that grows with each one and fades over this window.
		inline float fClimaxSeconds = 14.0f;
		inline float fRapidOrgasmSeconds = 40.0f;
		// Director: each orgasm gets its own climax face from a pool of fifteen, picked by personality and how rapid the run is,
		// instead of the one built-in template.
		inline bool bClimaxPool = true;
		// Director: faces of its own for the build-up to an orgasm - 22 in each of five stages - picked now and then beside OStim's pool
		// for what the actor is doing, so nothing looks like it is repeating. The share is how often the pick is one of them.
		inline bool bBuildupFaces = true;
		inline float fBuildupShare = 0.7f;
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
		inline bool bSleepingEyesClosed = true;  // an actor OStim marks as asleep (the "sleeping" action) has closed eyes and a slack face
		inline bool bExposureAware = true;
		inline bool bYieldOralMouth = true;
		inline bool bYieldMouthToPPA = true;  // a blowjob's mouth is PPA's while it is playing its mouth preset
		inline bool bDialogueMouthYield = true;
		inline bool bBodyDemo = false;
		inline bool bExcitementGradient = true;
		inline bool bSpeedSync = true;
		inline bool bRoleMetadata = true;
		inline bool bAggressorGrammar = true;
		inline bool bSpellNonConsent = true;          // scenes started by the player's spell (Matchmaker...) are non-consensual
		inline bool bNCSceneLock = true;              // a thread started non-consensual plays non-consensual scenes only
		inline float fNCAutoInterval = 20.0f;         // seconds between auto-mode scene changes on such a thread (+-40%)
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

		inline bool bAhegaoModYield = false;   // force the yield on, whatever is or is not installed
		inline bool bAhegaoAutoYield = true;   // Ahegao Expressions installed: leave faces to it
		inline bool bAnimeTongue = false;
		inline bool bAnimeTongueFull = false;
		inline bool bTongueLife = false;
		inline float fAnimeStart = 85.0f;
		inline float fAnimeEnd = 70.0f;

		inline bool bSPIDPersonality = true;
		inline bool bOBlushSync = true;
		inline int iPlayerPersonality = -1;   // -1 auto, 0 balanced, 1 stoic, 2 vocal, 3 shy, 4 dominant, 5 timid, 6 submissive (full edition only), 7 wild, 8 crazed
		// Where nothing else says (no SPID keyword, voice or AI value), this share of people get one of the newer personalities
		// (timid, submissive, wild, crazed) instead of one of the first five.
		inline float fNewPersonalityShare = 0.20f;
		// Wild people build excitement faster, and submissive ones in a rough scene: a multiplier on OStim's own rate.
		inline bool bPersonalityExcitement = true;
		inline float fWildExcitementMult = 1.30f;
		inline float fStoicExcitementMult = 0.70f;  // a stoic is not into it: the opposite of wild
		inline float fSubmissiveRoughMult = 1.35f;
		// Dominant and crazed people control a partner's climax. A dominant holds the partner at the edge until the dominant's own climax (or this
		// many seconds at the edge), and builds faster themselves; a crazed one builds the partner's excitement up fast and climaxes with them,
		// whenever they do. Consensual scenes only.
		inline bool bPersonalityControl = true;
		inline float fControlMaxHold = 150.0f;
		inline float fCrazedDriveMult = 2.5f;
		inline float fDominantExcitementMult = 1.25f;
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
		inline float fPerToe = 1.0f;          // extra curl at each toe's own joints (feet weighted to XPMSSE's per-toe bones); 0 off
		inline float fFingerDegrees = 45.0f;
		// Toes working during a foot action (footjob, grinding, tickling...) rather than only at
		// climax, as a fraction of the climax angle. Off by default: it moves the same bones OStim
		// measures for a footjob's peak, so it wants looking at before it is trusted.
		inline bool bFootFlex = false;
		inline float fFootFlexScale = 0.35f;
		inline int iCurlAxis = 0;             // 0 X, 1 Y, 2 Z (bone-local)
		// Arousal bends XPMSSE's genital chain (Gen01..Gen06) on a male body, the way the softbody
		// morphs move a female one. Off by default: it needs a schlong weighted to those bones, and
		// which way they bend depends on the rig, hence the axis.
		inline bool bGenitals = false;
		inline float fGenitalDegrees = 60.0f;  // total bend across the chain at full arousal
		inline int iGenitalAxis = 0;
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
		// Overlays are drawn matte (emissive and shine zeroed) so they read as colour in the skin.
		// Some shader setups render a zeroed emissive as a solid black square; turn this off there.
		inline bool bMatteOverlays = true;
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
		// What the mouth does while a tongue is out (ours, or any ahegao mod that equips OStim's
		// "tongue" object). Closing lips over an out tongue makes it clip through.
		enum Tongue : int { kTongueStop = 0, kTongueHold = 1, kTongueIgnore = 2 };
		inline int iTongueMode = kTongueStop;
		inline float fTongueMinOpen = 0.45f;  // how far the jaw is held open while a tongue is out
		inline bool bYieldToDDF = true;       // stand down while Dynamic Dialogue Framework is installed (both drive the mouth)
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

		// Which body a table row applies to. Rows written before this existed load as Female,
		// which is what they were.
		enum Sex : int { kAnySex = 0, kFemaleBody = 1, kMaleBody = 2 };

		// Which body type a row is for. UBE is a set of races of its own (00UBE_*) with its own sliders and its own UV map, so a CBBE/3BA
		// row (its slider names, its blush textures) means nothing on it, and a UBE row means nothing on a CBBE/3BA body. Rows written
		// before this existed load as Any, except the stock blush rows, whose textures are painted on the CBBE UV: those load as Standard.
		enum BodyType : int { kAnyType = 0, kStandardType = 1, kUBEType = 2 };

		struct Morph
		{
			std::string name;
			float start = 0.0f;  // tissue level where the slider begins to move
			float full = 1.0f;   // tissue level where it reaches max
			float max = 0.0f;    // slider value at full (negative allowed)
			bool enabled = true;
			float rest = 0.0f;   // slider value while unaroused
			int sex = kFemaleBody;
			int type = kAnyType;
		};

		struct Blush
		{
			std::string name;      // label, and the Body Blushing texture name when `texture` is empty
			float start = 0.0f;
			float full = 1.0f;
			float max = 1.0f;      // overlay alpha at full
			bool enabled = true;
			std::string texture;   // optional: your own path under Data\textures, overrides `name`
			int sex = kFemaleBody;
			int type = kAnyType;
			std::int32_t tint = -1;  // 0xRRGGBB; -1 uses the per-race colour
		};

		inline bool bEnabled = true;
		inline bool bAffectPlayer = true;
		inline bool bAffectNPCs = true;
		inline bool bOutsideScenes = false;  // keep following the arousal (an arousal mod's number, for the player and the NPCs near them) when no OStim scene is running; costs resources, so off
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

		// The shape of the response: how much of each morph's, blush's and genital bend's range shows. A floor from the moment an
		// actor is aroused (apparent, not exaggerated), a logarithmic rise to a ceiling through the build-up, a lift towards the top
		// only in the last seconds before an orgasm, and the full range only at the orgasm itself. Off: the old curve, where each
		// morph reached its maximum once the level passed its own `full` and stayed there.
		inline bool bShapedResponse = true;
		inline float fResponseFloor = 0.30f;    // the share of the range shown as soon as the actor is aroused
		inline float fResponseCeiling = 0.65f;  // the most shown through the build-up, until the last seconds
		inline float fStoicRest = 0.18f;        // a stoic: the share of the range shown at rest - purely physiological, minimal
		inline float fStoicPeak = 0.70f;        // a stoic: the most that shows, in the last seconds before an orgasm and during it
		inline float fPeakWindow = 6.0f;        // seconds before the orgasm, as estimated from how fast excitement is rising, that the lift begins

		inline bool bBlush = true;
		inline int iOverlayFirstSlot = 6;
		inline int iOverlaySlots = 6;

		// A body-blush alpha multiplier per race. The overlays are one grey texture tinted per
		// race, so the same alpha reads very differently on pale and dark skin. `race` is matched
		// as a lowercase substring of the race's editor ID, first match wins, so "nord" also
		// covers modded Nord variants. A race with no row uses 1.0.
		struct RaceBlush
		{
			std::string race;
			float mult = 1.0f;
		};

		inline std::vector<Morph> morphs;
		inline std::vector<Blush> blushes;
		inline std::vector<RaceBlush> raceBlush;

		std::vector<Morph> DefaultMorphs();  // the CBBE/3BA rows and the UBE rows
		std::vector<Blush> DefaultBlushes();
		std::vector<Morph> UbeMorphs();      // just the UBE rows, for adding to a table that has none
		std::vector<Blush> UbeBlushes();
		std::vector<RaceBlush> DefaultRaceBlush();
	}

	void Load();
	bool Save();
	// Startup: write OSIS.ini and morphs.json when missing, and add to the INI any setting it lacks.
	void EnsureFiles();
	void LoadTables();
	bool SaveTables();
	void RestoreDefaults();  // everything except the morph / blush tables
	void ApplyPreset(int a_preset);  // 0 Recommended, 1 Subtle, 2 Cinematic, 3 Performance, 4 Minimal

	[[nodiscard]] inline float StyleValue() { return std::clamp(Face::fStyle, 0.0f, 2.0f); }
}
