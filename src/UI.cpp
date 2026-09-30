#include "UI.h"

#include "Arousal.h"
#include "Body.h"
#include "Compat.h"
#include "Face/Engine.h"
#include "Face/Output.h"
#include "LipSync.h"
#include "OStimData.h"
#include "Hooks.h"
#include "Papyrus.h"
#include "Scenes.h"
#include "Scheduler.h"
#include "Settings.h"
#include "Skin.h"
#if !OSIS_NEXUS
#	include "SceneLock.h"
#	include "Voice.h"
#endif

namespace
{
	namespace ig = ImGuiMCP;

	// Widgets mark the page dirty; one Save button writes the files.
	bool g_dirty = false;
	char g_newName[96] = {};

	const ig::ImVec4 kGood{ 0.4f, 0.9f, 0.4f, 1.0f };
	const ig::ImVec4 kBad{ 0.9f, 0.4f, 0.4f, 1.0f };
	const ig::ImVec4 kWarn{ 1.0f, 0.75f, 0.2f, 1.0f };

	// ---- widget helpers (call with Settings::lock held)
	void Tip(const char* tip)
	{
		if (tip && *tip) ig::SetItemTooltip("%s", tip);
	}

	void Check(const char* label, bool& v, const char* tip = nullptr)
	{
		if (ig::Checkbox(label, &v)) g_dirty = true;
		Tip(tip);
	}

	void SliderF(const char* label, float& v, float lo, float hi, const char* fmt = "%.2f", const char* tip = nullptr)
	{
		if (ig::SliderFloat(label, &v, lo, hi, fmt)) g_dirty = true;
		Tip(tip);
	}

	void SliderI(const char* label, int& v, int lo, int hi, const char* tip = nullptr)
	{
		if (ig::SliderInt(label, &v, lo, hi)) g_dirty = true;
		Tip(tip);
	}

	void ComboI(const char* label, int& v, const char* const items[], int count, const char* tip = nullptr)
	{
		if (ig::Combo(label, &v, items, count)) g_dirty = true;
		Tip(tip);
	}

	void Path(const char* label, std::string& v, const char* tip = nullptr)
	{
		char buf[260];
		strncpy_s(buf, v.c_str(), _TRUNCATE);
		if (ig::InputText(label, buf, sizeof(buf))) {
			v = buf;
			g_dirty = true;
		}
		Tip(tip);
	}

	// Label/value rows go in a two-column table sized to its contents, so the values start after
	// the widest label at any font size. (A fixed SameLine offset let long labels run into the
	// values at the in-game font size.)
	bool BeginRows(const char* id)
	{
		return ig::BeginTable(id, 2, ig::ImGuiTableFlags_SizingFixedFit);
	}

	void Row(const char* name, bool ok, const char* good = "detected", const char* bad = "not found")
	{
		ig::TableNextRow();
		ig::TableNextColumn();
		ig::Text("%s", name);
		ig::TableNextColumn();
		if (ok) ig::TextColored(kGood, "%s", good);
		else ig::TextColored(kBad, "%s", bad);
	}

	// Anything that touches the game runs as a task on the main thread.
	void OnGame(std::function<void()> fn) { SKSE::GetTaskInterface()->AddTask(std::move(fn)); }

	RE::Actor* CrosshairActor()
	{
		if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
			if (auto ref = pick->GetActiveTarget().get()) {
				if (auto* a = ref->As<RE::Actor>()) return a;
			}
		}
		if (auto ref = RE::Console::GetSelectedRef()) return ref->As<RE::Actor>();
		return nullptr;
	}

	void SaveBar()
	{
		ig::Separator();
		if (g_dirty) {
			ig::TextColored(kWarn, "Unsaved changes");
			ig::SameLine();
		}
		if (ig::Button("Save")) {
			if (Settings::Save()) g_dirty = false;
		}
		ig::SameLine();
		if (ig::Button("Reload from disk")) {
			Settings::Load();
			Arousal::RequestClearAll();
			g_dirty = false;
		}
		ig::SameLine();
		ig::TextDisabled("Changes apply immediately; Save makes them persist.");
	}

	constexpr const char* kPersonalities[] = { "Auto", "Balanced", "Stoic", "Vocal", "Shy", "Dominant" };
	constexpr const char* kModes[] = { "Assist (OStim paints, OSED layers)", "Enhanced (stronger layer + headflow)", "Director (OSED owns the face)" };
	constexpr const char* kProfiles[] = { "Subtle", "Normal", "Expressive" };
	constexpr const char* kPresets[] = { "Recommended", "Subtle", "Cinematic", "Performance", "Minimal" };
	constexpr const char* kSources[] = { "Auto (OSL first)", "OSL Aroused", "SLO Aroused NG", "OStim excitement only" };
	constexpr const char* kAxes[] = { "X", "Y", "Z" };
	// A module's switch and whether it is running (or stood down for another mod). Settings::lock held.
	void ModuleRow(const char* name, bool& on, int compat, const char* tip)
	{
		ig::TableNextRow();
		ig::TableNextColumn();
		Check(name, on, tip);
		ig::TableNextColumn();
		const auto reason = compat >= 0 ? Compat::Reason(static_cast<Compat::Module>(compat)) : std::string{};
		if (!on) ig::TextDisabled("off");
		else if (!reason.empty()) ig::TextColored(kWarn, "%s", reason.c_str());
		else ig::TextColored(kGood, "on");
	}

	constexpr const char* kVoiceModes[] = { "Silent", "Breathing only", "Full (help, lines, scream)" };
	constexpr const char* kResponderModes[] = { "Nobody", "Guards", "Guards and allies" };
	constexpr const char* kDoms[] = { "Anticipation", "Pleasure", "Plateau", "Distress", "Climax", "Afterglow" };

	// ================================================================ pages
	void __stdcall RenderStatus()
	{
		ig::SeparatorText("Requirements");
		if (BeginRows("requirements")) {
			Row("OStim Standalone", Face::Engine::OStimPresent());
			Row("OStim scene files", OStimData::SceneCount() > 0, std::format("{} indexed", OStimData::SceneCount()).c_str(), "none found");
			Row("OSL Aroused", Arousal::HasOSL());
			Row("SLO Aroused NG", Arousal::HasSLO());
			Row("OBlush (face blush yields to it)", Face::Engine::OBlushPresent());
			Row("Devious Devices (gag/blindfold faces)", Face::Engine::DevicesPresent());
			ig::EndTable();
		}
		ig::TextDisabled("%s", Face::Engine::AhegaoStatus().c_str());

		const auto conflicts = Compat::Conflicts();
		if (!conflicts.empty()) {
			ig::SeparatorText("Old mods still active");
			for (const auto& c : conflicts) ig::TextColored(kWarn, "%s", c.c_str());
			ig::TextWrapped("Disable these in your mod manager: the original OSED mods and Softbody Arousal are replaced by this one, and the OSED Reborn <x> plugins are its Papyrus fallback, not an add-on.");
		}

		ig::SeparatorText("Scenes");
		const auto threads = Scenes::Snapshot();
		if (threads.empty()) ig::TextDisabled("No OStim scene running.");
		for (const auto& t : threads) {
#if OSIS_NEXUS
			const char* tone = t.rough ? "  [rough]" : "";
#else
			const char* tone = t.consent ? (t.rough ? "  [rough, consensual]" : "") : (t.spell ? "  [non-consent: spell]" : "  [non-consent]");
#endif
			ig::Text("Thread %d%s  scene %s  speed %d/%d  %.0fs%s%s%s", t.id, t.player ? " (player)" : "", t.scene.empty() ? "<starting>" : t.scene.c_str(),
				t.speed, t.maxSpeed, t.time, tone, t.orgasm ? "  [climax]" : "", t.normal ? "  [pre-animation]" : "");
			if (ig::BeginTable(std::format("actors{}", t.id).c_str(), 7, ig::ImGuiTableFlags_Borders | ig::ImGuiTableFlags_RowBg)) {
				for (const char* h : { "Actor", "Excite", "State", "Personality", "Face", "Mouth", "Head" }) ig::TableSetupColumn(h);
				ig::TableHeadersRow();
				for (const auto& s : t.slots) {
					ig::TableNextRow();
					ig::TableNextColumn();
					ig::Text("%s%s", s.name.c_str(), s.painted ? "" : " (skipped)");
					ig::TableNextColumn();
					ig::Text("%d (%d)", s.raw, s.enj);
					ig::TableNextColumn();
					ig::Text("%s", s.dom >= 0 && s.dom < 6 ? kDoms[s.dom] : "?");
					ig::TableNextColumn();
					ig::Text("%s (%s)", s.arch.c_str(), s.archSource.c_str());
					ig::TableNextColumn();
					ig::Text("%s", s.face.c_str());
					ig::TableNextColumn();
					ig::Text("%s", s.mouth.c_str());
					ig::TableNextColumn();
					ig::Text("%s", s.head.c_str());
				}
				ig::EndTable();
			}
		}

		ig::SeparatorText("Add-ons");
		ig::Text("Body: %s", Body::Status().c_str());
		ig::Text("Living Skin: %s", Skin::Status().c_str());
		ig::TextWrapped("Lip-Sync: %s", LipSync::Status().c_str());
#if !OSIS_NEXUS
		ig::TextWrapped("Scene lock: %s", SceneLock::Status().c_str());
#endif
		ig::Text("Watcher: %s", Face::Engine::WatcherStatus().c_str());
		ig::Text("Faces being written: %zu", Face::Output::PaintedCount());

		ig::Spacing();
		if (ig::Button("Release all faces now")) OnGame([]() {
			Face::Output::ReleaseAll(0.3f);
			Face::Engine::RestorePersistedTakeovers();
		});
		ig::SetItemTooltip("Emergency reset: neutral faces and OStim's own face writer back on for everyone.");
		ig::SameLine();
		if (ig::Button("Clear body + skin effects")) OnGame([]() {
			Body::ClearAll();
			Skin::ClearAll();
			Arousal::RequestClearAll();
		});
		ig::TextDisabled("Log: Documents/My Games/Skyrim Special Edition/SKSE/OSIS.log");
	}

	void __stdcall RenderGeneral()
	{
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::General;
			ig::SeparatorText("OStim Standalone Immersive Sex");
			bool was = bEnabled;
			Check("Enabled", bEnabled, "Master switch. Turning it off ends OSED's control of every scene immediately.");
			if (was && !bEnabled) OnGame([]() { Scenes::OnDisabled(); });
			Check("Paint NPCs in the player's scene", bIncludeNPCs);
			Check("Direct NPC-only scenes too", bNPCOnlyScenes, "OStim NPCs and similar mods start scenes without the player. OSED 2.0 ignored them.");
			SliderF("NPC-only scene radius", fNPCSceneRadius, 500.0f, 10000.0f, "%.0f");
			if (ig::Checkbox("Debug logging", &bDebug)) {
				spdlog::set_level(bDebug ? spdlog::level::debug : spdlog::level::info);
				g_dirty = true;
			}

			ig::SeparatorText("Modules");
			ig::TextWrapped("Every part can be switched off on its own, for when another mod already does that job. A module that is off "
			                "stops writing and hands back what it held; the others keep running.");
			if (ig::BeginTable("modules", 2, ig::ImGuiTableFlags_SizingFixedFit)) {
				ModuleRow("Faces", Settings::Face::bEnabled, Compat::kFace,
					"Expressions during scenes. Off: OStim, or another expression mod, keeps the face.");
				ModuleRow("Body (toe curl, hand grip)", Settings::Body::bEnabled, Compat::kBody,
					"Off for a mod that also poses toes or fingers at climax.");
				ModuleRow("Living Skin (face blush, saliva)", Settings::Skin::bEnabled, Compat::kSkin,
					"RaceMenu face overlays. Off for a mod that uses the same overlay slots or blushes the face.");
				ModuleRow("Lip-Sync", Settings::LipSync::bEnabled, Compat::kLipSync,
					"Moves the mouth with OStim's moans. Off for a mod that also drives the mouth. Dynamic Dialogue Framework does, "
					"so Lip-Sync stands down while DDF is installed (see the Lip-Sync page).");
				ModuleRow("Softbody Arousal", Settings::Arousal::bEnabled, Compat::kArousal,
					"Arousal body morphs and body blush. Off for another arousal-morph mod.");
#if !OSIS_NEXUS
				ModuleRow("Victim Voice", Settings::Voice::bEnabled, -1,
					"Mutes OStim's moans on a victim and speaks their lines. Off for another mod that voices the scene.");
				ModuleRow("Scene lock (non-consensual scenes only)", Settings::Face::bNCSceneLock, -1,
					"Takes over OStim's auto mode and trims the scene menu on non-consensual threads. Off for a mod that picks those scenes itself.");
				ModuleRow("Non-consent from scene tags", Settings::Face::bAggressorGrammar, -1,
					"Forced/rape/aggressive tags make a scene non-consensual. Off: every scene is consensual.");
				ModuleRow("Non-consent from the player's spells", Settings::Face::bSpellNonConsent, -1,
					"Scenes started by the player's spell (Matchmaker...) are non-consensual.");
#endif
				ModuleRow("Mod events (SLED_*, OSED_*)", bPulseBus, -1, "For third-party mods that listened to OSED's events.");
				ig::TableNextRow();
				ig::TableNextColumn();
				Check("Animation hooks (restart)", bAnimationHooks,
					"Writes faces and toe curl right after each frame's animation so nothing overwrites them. Off only if another mod's "
					"hook on the same calls crashes or fights it: faces then update 20 times a second from the main thread and toe/finger "
					"curl stops. Takes effect the next time the game starts.");
				ig::TableNextColumn();
				if (Hooks::PlayerHooked()) ig::TextColored(kGood, "installed%s", Hooks::NPCHooked() ? "" : " (player only)");
				else ig::TextDisabled("not installed");
				ig::EndTable();
			}
		}
		SaveBar();
	}

	void __stdcall RenderFaces()
	{
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::Face;
			ig::SeparatorText("Face engine");
			Check("Enabled", bEnabled, "Off: OSED paints no faces and hands back any it holds. Body, skin, lip-sync and arousal keep running.");
			ComboI("Mode", iMode, kModes, 3,
				"Director computes the whole face every frame and switches OStim's face writer off for painted actors (oral mouth overrides still work). "
				"Assist/Enhanced keep OStim's faces and layer OSED on top.");
			if (iMode != kDirector) Check("Take over OStim's face writer", bTakeOverFace, "Stops OStim overwriting OSED's eye/brow layer. Restored on scene end and load.");

			static int preset = 0;
			ig::Combo("##preset", &preset, kPresets, 5);
			ig::SameLine();
			if (ig::Button("Apply preset")) {
				Settings::ApplyPreset(preset);
				g_dirty = true;
			}

			SliderF("Strength", fGlobalStrength, 0.0f, 2.0f);
			SliderF("Style (realistic > cinematic > anime)", fStyle, 0.0f, 2.0f, "%.2f", "Above 1.5 enables the anime climax accents and tongue options.");
			SliderF("Eye strength", fEyeStrength, 0.0f, 1.5f);
			ComboI("Profile", iProfile, kProfiles, 3);
			SliderF("Beat interval (s)", fBaseInterval, 1.0f, 8.0f, "%.1f", "How often the grammar picks a new face. Output is smoothed every frame in between.");
			SliderF("Interval jitter (s)", fIntervalJitter, 0.0f, 3.0f, "%.1f");
			SliderF("Transition (s)", fTransition, 0.05f, 2.0f);

			ig::SeparatorText("Mouth");
			Check("Yield mouth to oral actions", bYieldOralMouth);
			Check("Yield mouth to dialogue", bDialogueMouthYield);
			Check("Breathing clock", bBreathing, "Moan/breath mouth cycle between beats. Off by default: Lip-Sync drives the mouth from the real moans.");
			Check("Mouth variety", bMouthVariety);

			ig::SeparatorText("Anime");
			SliderF("Anime accent starts at excitement", fAnimeStart, 50.0f, 100.0f, "%.0f");
			SliderF("Anime accent ends below", fAnimeEnd, 40.0f, 100.0f, "%.0f");
			Check("Tongue at the peak (anime style)", bAnimeTongue);
			Check("Full tongue mode", bAnimeTongueFull);
			Check("Tongue life (rare small pulses)", bTongueLife);
			Check("Yield to another ahegao mod", bAhegaoModYield);
		}
		ig::Spacing();
		if (ig::Button("Test face on crosshair actor")) OnGame([]() {
			if (auto* a = CrosshairActor()) {
				Face::Engine::TestOnActor(a);
				const auto h = a->GetHandle();
				Scheduler::After(6.0f, [h]() {
					if (auto p = h.get()) Face::Output::Release(p.get(), 0.6f);
				});
			} else {
				Papyrus::Notify("OSIS: aim at an actor first");
			}
		});
		SaveBar();
	}

	void __stdcall RenderDirector()
	{
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::Face;
			ig::TextWrapped("The Director's grammar layers. Defaults match the OSED 2.0 recommended preset.");
			if (ig::CollapsingHeader("Emotion", ig::ImGuiTreeNodeFlags_DefaultOpen)) {
				Check("Rich emotions (tender / lust / playful / surrender / detached)", bRichEmotions);
				Check("Role aware (loving / rough tags)", bRoleAware);
				Check("Act-type aware (oral / kissing / penetration)", bActTypeAware);
				Check("Positional dominance flavor", bPositionalDomSub);
				Check("Natural detail (asymmetry, micro-tics, relationship)", bNaturalDetail);
				Check("Climax choreography", bClimaxChoreo);
				Check("Cinematic afterglow", bCinematic);
				Check("Exposure awareness (bashful when nude)", bExposureAware);
				Check("Device awareness (gags, blindfolds)", bDeviceAware);
			}
			if (ig::CollapsingHeader("Timing", ig::ImGuiTreeNodeFlags_DefaultOpen)) {
				Check("Pace boost (stage changes + speed raise intensity)", bPaceBoost);
				Check("Speed sync", bSpeedSync);
				Check("Excitement gradient", bExcitementGradient);
				Check("Phrase grammar (five-beat phrase envelope)", bPhraseGrammar);
				Check("Scenario cycler", bScenarioCycler);
				Check("Overwhelm face", bOverwhelmFace);
				Check("Group conductor (3+ actors)", bGroupConductor);
			}
#if !OSIS_NEXUS
			if (ig::CollapsingHeader("Consent")) {
				Check("Aggressor grammar (forced/rape/aggressive tags mean non-consent)", bAggressorGrammar);
				Tip("Scenes tagged forced, rape or aggressive (OStim's non-consent marker) are non-consensual. The victim reacts "
				    "by personality (dominant: defiant anger, shy: fear, vocal: panic, stoic: numb endurance, balanced: sadness "
				    "turning to fear); the other actor gets an aggressive face. Rough play and BDSM tags (rough, dom, femdom, "
				    "bdsm, spank...) without a forced tag stay consensual. Off: every scene is consensual.");
				Check("Scenes started by the player's spell are non-consensual", bSpellNonConsent);
				Tip("A scene that starts right after one of the player's spells ran a script effect on an NPC in it "
				    "(OStim NPCs' Matchmaker, for example) is non-consensual, whatever its tags say. The NPCs the spell hit "
				    "are the victims; the player and anyone who joined without being cast on (a follower who asked to join) "
				    "are aggressors. A Matchmaker target tagged minutes before the final cast still counts, and the scene "
				    "stays non-consensual when a follower joins and it restarts. Attacks don't count: hostile spells and "
				    "spells on an enemy mid-fight are ignored. Asking is not forcing: an NPC who talked with the player after "
				    "the spell (ODragonSeed's NPCs come to ask, for example) is no victim, and if nobody else was cast on, "
				    "only the scene's tags decide consent.");
				Check("Non-consensual scenes stay non-consensual", bNCSceneLock,
					"A scene that starts non-consensual (a forced/rape/aggressive starting scene, the player's spell, or the "
					"starting mod's thread metadata) only plays non-consensual scenes. OStim's auto mode is taken over by one "
					"that picks among them (OStim's own library, matching the actors and furniture), the scene menu only offers "
					"options that lead to one, and anything else that lands on a consensual scene is walked back. Pressing "
					"OStim's auto-mode key still toggles auto mode.");
				SliderF("Auto mode: seconds per scene", fNCAutoInterval, 5.0f, 90.0f, "%.0f", "On those threads. Varies 40% either way.");
				Check("Consent guardrails", bConsentGuardrails);
				Check("Hard exclusion gate (distress owns the face)", bHardExclusionGate);
				Check("No overwhelm/ahegao in distress", bNoDistressOverwhelm);
				Check("Victim breaks after climaxing", bBrokenAfterClimax,
					"When a victim climaxes, their mind checks out for the rest of the scene: the face goes empty (slack mouth, "
					"lowered lids, a vacant downward stare) and stops reacting. Tears keep coming, the body keeps responding "
					"(arousal, climaxes, toe curl), and moans still move the mouth, but the eyes no longer squeeze. A victim "
					"who was defiant cries too once broken.");
				Check("Consent sets OStim excitement rates", bConsentExcitement,
					"In a non-consensual scene with an identified victim, each actor's OStim excitement rate is scaled "
					"relative to OStim's own (MCM) rate. OStim resets it when the scene ends; it is also restored if the "
					"scene turns consensual.");
				SliderF("Victim excitement rate", fVictimExcitementMult, 0.05f, 2.0f, "%.2fx", "0.5x: the victim takes about twice as long to climax.");
				SliderF("Aggressor excitement rate", fAggressorExcitementMult, 0.05f, 3.0f, "%.2fx", "1.5x: the other actor climaxes sooner.");
			}
#endif
			if (ig::CollapsingHeader("Head and gaze")) {
				Check("Gaze at partner", bGaze);
#if !OSIS_NEXUS
				Check("Only hold gaze when consensual", bGazeConsentOnly);
#endif
				Check("Headflow (throat arch, aversion, afterglow drop)", bHeadflow);
				Check("Body demo (head tips back at climax)", bBodyDemo);
				Check("Use OStim scene metadata for roles", bRoleMetadata);
			}
			if (ig::CollapsingHeader("Pre-animation (normal state)")) {
				Check("Normal state layer", bNormalState);
				SliderF("Intensity", fNormalIntensity, 0.0f, 1.0f);
				SliderF("Gaze frequency", fNormalGazeFrequency, 0.0f, 1.0f);
				Check("Pre-warm from early excitement", bNormalPreWarm);
				Check("NPCs glance at the player", bNormalGlancePlayer);
				Check("Watcher trial (nearby NPC reacts)", bWatcher);
			}
		}
		SaveBar();
	}

	void __stdcall RenderPersonality()
	{
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::Face;
			ig::SeparatorText("Personality sources");
			int player = iPlayerPersonality + 1;
			if (ig::Combo("Player personality", &player, kPersonalities, 6)) {
				iPlayerPersonality = player - 1;
				g_dirty = true;
			}
			Check("SPID personality keywords", bSPIDPersonality, "OSED_Personality_DISTR.ini hands NPCs Bashful/Bold/Soft/Fierce keywords.");
			Check("Voice set shapes personality", bVoiceArchetype);
			Check("OBlush-aware shyness", bOBlushSync);
		}
		ig::SeparatorText("Crosshair NPC");
		ig::TextWrapped("Aim at an NPC (or select one in the console), then pick a personality. Saved in your save game.");
		static int pick = 0;
		ig::Combo("##npcpers", &pick, kPersonalities, 6);
		ig::SameLine();
		if (ig::Button("Set")) OnGame([p = pick]() {
			auto* a = CrosshairActor();
			if (!a) return Papyrus::Notify("OSIS: aim at an NPC first");
			Face::Engine::SetNpcPersonality(a, p - 1);
			std::string src;
			const int arch = Face::Engine::Archetype(a, &src);
			Papyrus::Notify(std::format("{}: {} ({})", a->GetDisplayFullName(), Face::Engine::PersonalityName(arch), src));
		});
		ig::SameLine();
		if (ig::Button("Show")) OnGame([]() {
			auto* a = CrosshairActor();
			if (!a) return Papyrus::Notify("OSIS: aim at an NPC first");
			std::string src;
			const int arch = Face::Engine::Archetype(a, &src);
			Papyrus::Notify(std::format("{}: {} ({})", a->GetDisplayFullName(), Face::Engine::PersonalityName(arch), src));
		});
		SaveBar();
	}

	void __stdcall RenderBody()
	{
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::Body;
			ig::TextWrapped("Toe curl and hand grip at climax, applied to the bones after each animation update so the animation cannot overwrite it.");
			Check("Enabled", bEnabled);
			SliderF("Strength", fStrength, 0.0f, 1.0f);
			Check("Toe curl", bToe);
			SliderF("Toe degrees", fToeDegrees, 0.0f, 60.0f, "%.0f");
			SliderF("Per-toe curl", fPerToe, 0.0f, 2.0f, "%.2f",
				"Extra bend at each toe's own two joints, on top of the whole-foot toe bone. Only shows on feet weighted to "
				"XPMSSE's per-toe bones (Aerosmith TJ Feet, for example); other feet look as before. 0 = off.");
			Check("Hand grip", bHand);
			SliderF("Finger degrees", fFingerDegrees, 0.0f, 90.0f, "%.0f");
			ComboI("Curl axis", iCurlAxis, kAxes, 3, "Bone-local axis. If toes/fingers bend sideways, try another axis with the test button.");
			Check("Scale with style", bStyleGated, "Realistic 35%, cinematic 70%, anime 100%.");
		}
		if (ig::Button("Test on crosshair actor")) OnGame([]() {
			if (auto* a = CrosshairActor()) Body::Test(a);
			else Papyrus::Notify("OSIS: aim at an actor first");
		});
		SaveBar();
	}

	void __stdcall RenderSkin()
	{
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::Skin;
#if OSIS_NEXUS
			ig::TextWrapped("Face overlays through RaceMenu's \"Face [Ovl#]\" slots. Blush follows excitement and the softbody arousal flush; "
			                "saliva is a short climax beat.");
#else
			ig::TextWrapped("Face overlays through RaceMenu's \"Face [Ovl#]\" slots. Blush follows excitement and the softbody arousal flush; "
			                "saliva is a short climax beat. Tears are reserved for non-consensual scenes: the victim wells up when distress starts "
			                "and at a forced climax. Without a tear texture, tears fall back to a welling-eyes expression.");
#endif
			Check("Enabled", bEnabled);
			SliderF("Strength", fStrength, 0.0f, 1.5f);
			Check("Blush", bBlush);
#if !OSIS_NEXUS
			Check("Tears", bTears);
			Check("Emotional Tears Effect", bEmoTears, "If EmoTearsSpells.esp is installed, a crying victim also gets its streaming tears until the scene ends.");
#endif
			Check("Saliva", bSaliva);
			Check("Scale with style", bStyleGated);
			Check("Females only", bFemaleOnly);
			SliderI("First face overlay slot", iFaceFirstSlot, 0, 15, "Slot 0 is often makeup; OSED uses one slot per effect from here.");
			Path("Blush texture", sBlushPath, "Relative to Data\\textures, e.g. actors\\character\\Overlays\\FMS\\Blush\\Blush Cheeks 1.dds. Empty = auto.");
#if !OSIS_NEXUS
			Path("Tear texture", sTearPath);
#endif
			Path("Saliva texture", sSalivaPath);
		}
		const int slots = Skin::FaceOverlaySlots();
		ig::TextDisabled("RaceMenu face overlay slots: %d (skee64.ini [Overlays/Face] iNumOverlays)", slots);
		const auto blush = Skin::ResolvedPath(0);
		ig::TextDisabled("Blush texture in use: %s", blush.empty() ? "none" : blush.c_str());
#if !OSIS_NEXUS
		ig::TextDisabled("Emotional Tears Effect: %s", Skin::EmoTearsFound() ? "installed" : "not installed");
#endif
		if (Face::Engine::OBlushPresent()) ig::TextColored(kWarn, "OBlush is installed: OSED's face blush yields to it.");
		if (ig::Button("Test blush")) OnGame([]() { if (auto* a = CrosshairActor()) Skin::TestBlush(a); });
#if !OSIS_NEXUS
		ig::SameLine();
		if (ig::Button("Test tear")) OnGame([]() { if (auto* a = CrosshairActor()) Skin::TestTear(a); });
#endif
		ig::SameLine();
		if (ig::Button("Test saliva")) OnGame([]() { if (auto* a = CrosshairActor()) Skin::TestSaliva(a); });
		SaveBar();
	}

	void __stdcall RenderLipSync()
	{
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::LipSync;
			ig::TextWrapped("Moves the mouth with the moan OStim is actually playing: the moan files named by your OStim voice sets are "
			                "decoded at startup and followed frame by frame. No bake step, no FaceFX, no second voice.");
			Check("Enabled", bEnabled);
			SliderF("Mouth gain", fGain, 0.2f, 2.0f);
			SliderF("Max opening", fMaxOpen, 0.2f, 1.0f);
			SliderF("Attack (s)", fAttack, 0.005f, 0.2f, "%.3f");
			SliderF("Release (s)", fRelease, 0.02f, 0.5f, "%.3f");
			Check("Eyes squeeze with the moan", bHoldEyes);
			Check("Stand down while Dynamic Dialogue Framework is installed", bYieldToDDF,
				"DDF plays spoken lines with their own lip movement; with both driving the mouth, both break. Untick to run Lip-Sync anyway.");
		}
		if (Compat::DDFActive()) {
			if (Compat::Disabled(Compat::kLipSync)) ig::TextColored(kWarn, "Dynamic Dialogue Framework is active: Lip-Sync is standing down.");
			else ig::TextColored(kWarn, "Dynamic Dialogue Framework is active: expect the two to fight over the mouth.");
		}
		ig::TextWrapped("%s", LipSync::Status().c_str());
		SaveBar();
	}

#if !OSIS_NEXUS
	void __stdcall RenderVoice()
	{
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::Voice;
			ig::TextWrapped("The victim of a non-consensual scene. OStim's moans and climax sounds are muted on them. Instead they cry for help "
			                "as it starts, and guards or allies in range answer. Then they protest, curse or beg by personality, scream at the "
			                "climax that breaks them, and after that only breathe hard. All lines are vanilla Skyrim dialogue in the actor's own voice.");
			Check("Enabled", bEnabled);
			Check("Mute OStim moans on the victim", bVictimNoMoans);
			ComboI("Victim voice", iVictimVoice, kVoiceModes, 3);
			SliderF("Seconds between lines", fInterval, 3.0f, 30.0f, "%.0f", "Varies 40% either way. Panicked victims speak more often, numb ones less.");
			Check("Mute OStim dialogue on the victim", bMuteDialogue, "OStim's spoken scene comments (OActor.Mute), until the scene ends.");

			ig::SeparatorText("Call for help");
			Check("Cry for help as it starts", bCallForHelp);
			ComboI("Who answers", iResponders, kResponderModes, 3,
				"Guards and allies in range attack the aggressor; the victim stays in the scene. When the aggressor is the player and a "
				"guard answered, the assault also goes on the player's bounty in the victim's hold. Combat can end the OStim scene.");
			SliderF("Answer range", fResponderRange, 256.0f, 8192.0f, "%.0f");

			ig::SeparatorText("Breaking climax");
			Check("Scream", bBreakScream);
			SliderF("Shocked face (s)", fShockSeconds, 0.0f, 10.0f, "%.1f", "Before the vacant face of a broken victim. 0 = straight to vacant.");
		}
		ig::SeparatorText("Status");
		ig::TextWrapped("%s", Voice::Status().c_str());
		if (ig::Button("Test cry for help")) OnGame([]() { Voice::TestHelp(CrosshairActor()); });
		ig::SameLine();
		if (ig::Button("Test line")) OnGame([]() { Voice::TestLine(CrosshairActor()); });
		ig::SameLine();
		if (ig::Button("Test scream")) OnGame([]() { Voice::TestScream(CrosshairActor()); });
		ig::SameLine();
		if (ig::Button("Test breathing")) OnGame([]() { Voice::TestBreath(CrosshairActor()); });
		ig::TextDisabled("Tests speak on the crosshair actor; they don't call anyone.");
		SaveBar();
	}
#endif

	void __stdcall RenderArousal()
	{
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::Arousal;
			ig::SeparatorText("Softbody Arousal");
			Check("Enabled", bEnabled);
			Check("Affect player", bAffectPlayer);
			Check("Affect nearby NPCs", bAffectNPCs);
			ComboI("Arousal source", iSource, kSources, 4);
			SliderF("Intensity", fIntensity, 0.0f, 2.0f, "%.2f", "Multiplier on every morph's change and the body blush.");
			SliderI("Max NPCs", iMaxNPCs, 0, 20);
			SliderF("NPC radius", fRadius, 500.0f, 8000.0f, "%.0f");
			SliderF("Update interval (s)", fInterval, 0.25f, 10.0f);

			ig::SeparatorText("Scene factors");
			Check("OStim excitement is an arousal floor in scenes", bOStimExcitement);
			Check("Climax / edging / afterglow", bSceneFactors,
				"An orgasm pushes to full engorgement for the hold time; edging holds high; afterwards the level eases back to the reported arousal, never to zero.");
			SliderF("Climax hold (s)", fClimaxHold, 0.0f, 30.0f, "%.0f");
			Check("Personality shapes the response", bPersonality, "Vocal/dominant engorge faster, stoic slower; shy flushes harder.");

			ig::SeparatorText("Physiological response");
			SliderF("Engorgement half-life (s)", fRiseHalfLife, 1.0f, 120.0f, "%.0f");
			SliderF("Resolution half-life (s)", fFallHalfLife, 1.0f, 600.0f, "%.0f");
		}
		ig::SeparatorText("Affected actors");
		const auto rows = Arousal::Snapshot();
		if (rows.empty()) ig::TextDisabled("None right now.");
		if (!rows.empty() && BeginRows("arousal actors")) {
			for (const auto& r : rows) {
				ig::TableNextRow();
				ig::TableNextColumn();
				ig::Text("%s", r.name.c_str());
				ig::TableNextColumn();
				ig::Text("arousal %3.0f  target %3.0f%%  response %3.0f%%  (%s)", r.arousal, r.target * 100.0f, r.level * 100.0f, r.why.c_str());
			}
			ig::EndTable();
		}
		if (ig::Button("Clear all morphs now")) Arousal::RequestClearAll();
		SaveBar();
	}

	void __stdcall RenderMorphs()
	{
		ig::TextWrapped("Each row is a GT Softbody BodySlide slider. It holds at Rest while unaroused, starts moving when the response reaches Start, "
		                "reaches Max at Full, and eases in between. The body must be built with \"Build Morphs\" checked.");
		{
			std::scoped_lock l(Settings::lock);
			auto& morphs = Settings::Arousal::morphs;
			int removeAt = -1;
			for (size_t i = 0; i < morphs.size(); ++i) {
				auto& m = morphs[i];
				ig::PushID(static_cast<int>(i));
				if (ig::Checkbox("##on", &m.enabled)) g_dirty = true;
				ig::SameLine();
				ig::SeparatorText(m.name.c_str());
				SliderF("Start", m.start, 0.0f, 1.0f);
				SliderF("Full", m.full, 0.0f, 1.0f);
				SliderF("Rest", m.rest, -1.0f, 1.5f);
				SliderF("Max", m.max, -1.0f, 1.5f);
				if (m.full <= m.start) m.full = std::min(1.0f, m.start + 0.01f);
				if (ig::Button("Remove")) removeAt = static_cast<int>(i);
				ig::PopID();
			}
			if (removeAt >= 0) {
				morphs.erase(morphs.begin() + removeAt);
				g_dirty = true;
			}
			ig::SeparatorText("Add slider");
			ig::InputText("Slider name", g_newName, sizeof(g_newName));
			ig::SameLine();
			if (ig::Button("Add") && g_newName[0]) {
				morphs.push_back({ g_newName, 0.3f, 0.9f, 0.3f });
				g_newName[0] = '\0';
				g_dirty = true;
			}
			if (ig::Button("Restore default table")) {
				morphs = Settings::Arousal::DefaultMorphs();
				g_dirty = true;
			}
		}
		SaveBar();
	}

	void __stdcall RenderBlush()
	{
		ig::TextWrapped("Fades Body Blushing (iAmChe) overlays in with the arousal level. Each region takes one RaceMenu \"Body [Ovl#]\" slot, "
		                "starting at First slot. Colour is chosen per race; beast races and vampires are skipped.");
		const int have = Arousal::BodyOverlaySlots();
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::Arousal;
			Check("Enable body blushing", bBlush);
			SliderI("First slot", iOverlayFirstSlot, 0, 31, "Slots below this are left for other overlay mods.");
			SliderI("Slots to use", iOverlaySlots, 0, 16);
			const int needed = iOverlayFirstSlot + iOverlaySlots;
			if (needed > have) {
				ig::TextColored(kWarn, "RaceMenu has %d body overlay slots; this needs %d.", have, needed);
				ig::TextColored(kWarn, "Set [Overlays/Body] iNumOverlays=%d in skee64.ini and restart.", needed);
			}
			int removeAt = -1;
			for (size_t i = 0; i < blushes.size(); ++i) {
				auto& b = blushes[i];
				ig::PushID(static_cast<int>(i) + 1000);
				if (ig::Checkbox("##on", &b.enabled)) g_dirty = true;
				ig::SameLine();
				ig::SeparatorText(b.name.c_str());
				SliderF("Start", b.start, 0.0f, 1.0f);
				SliderF("Full", b.full, 0.0f, 1.0f);
				SliderF("Max", b.max, 0.0f, 1.0f);
				if (b.full <= b.start) b.full = std::min(1.0f, b.start + 0.01f);
				if (ig::Button("Remove")) removeAt = static_cast<int>(i);
				ig::PopID();
			}
			if (removeAt >= 0) {
				blushes.erase(blushes.begin() + removeAt);
				g_dirty = true;
			}
			ig::SeparatorText("Add region");
			ig::InputText("Texture name", g_newName, sizeof(g_newName));
			ig::SetItemTooltip("e.g. Blush_Belly_Upper, Blush_Flanks, Blush_Ass, Blush_Back_Upper");
			ig::SameLine();
			if (ig::Button("Add") && g_newName[0]) {
				blushes.push_back({ g_newName, 0.4f, 0.9f, 0.7f });
				g_newName[0] = '\0';
				g_dirty = true;
			}
			if (ig::Button("Restore default regions")) {
				blushes = DefaultBlushes();
				g_dirty = true;
			}
		}
		SaveBar();
	}
}

void UI::Register()
{
	if (!SKSEMenuFramework::IsInstalled()) {
		logger::info("SKSE Menu Framework not installed - configure via OSIS.ini instead.");
		return;
	}
	SKSEMenuFramework::SetSection("OSIS");
	SKSEMenuFramework::AddSectionItem("Status", RenderStatus);
	SKSEMenuFramework::AddSectionItem("General", RenderGeneral);
	SKSEMenuFramework::AddSectionItem("Faces", RenderFaces);
	SKSEMenuFramework::AddSectionItem("Director", RenderDirector);
	SKSEMenuFramework::AddSectionItem("Personality", RenderPersonality);
	SKSEMenuFramework::AddSectionItem("Body", RenderBody);
	SKSEMenuFramework::AddSectionItem("Living Skin", RenderSkin);
	SKSEMenuFramework::AddSectionItem("Lip-Sync", RenderLipSync);
#if !OSIS_NEXUS
	SKSEMenuFramework::AddSectionItem("Victim Voice", RenderVoice);
#endif
	SKSEMenuFramework::AddSectionItem("Arousal", RenderArousal);
	SKSEMenuFramework::AddSectionItem("Arousal/Morphs", RenderMorphs);
	SKSEMenuFramework::AddSectionItem("Arousal/Body Blush", RenderBlush);
}
