// SPDX-License-Identifier: GPL-3.0-or-later
// OStim Standalone Immersive Sex (OSIS) - Copyright (C) 2026 jturnley
// Free software under the GNU General Public License v3 or later; see LICENSE in this
// repository. Distributed WITHOUT ANY WARRANTY. <https://www.gnu.org/licenses/>

#include "UI.h"

#include "Arousal.h"
#include "Body.h"
#include "Compat.h"
#include "Face/Engine.h"
#include "Face/PPA.h"
#include "Face/Output.h"
#include "LipSync.h"
#include "OStimData.h"
#include "Hooks.h"
#include "Papyrus.h"
#include "Scenes.h"
#include "Scheduler.h"
#include "Settings.h"
#include "Skin.h"
#if !OSIS_LITE
#	include "SceneLock.h"
#	include "Voice.h"
#endif

namespace
{
	namespace ig = ImGuiMCP;

	// Widgets mark the page dirty; one Save button writes the files.
	bool g_dirty = false;
	char g_newName[96] = {};
	char g_newRace[64] = {};

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

	// What the test buttons act on. During a scene the crosshair is useless - OStim hides the HUD
	// and takes the camera - so fall back to the player's own scene. The partner is preferred over
	// the player, whose face you cannot see in third person anyway.
	RE::Actor* TestTarget()
	{
		if (auto* a = CrosshairActor()) return a;
		std::scoped_lock l(Scenes::Lock());
		auto* t = Scenes::PlayerThread();
		if (!t) return nullptr;
		auto* player = RE::PlayerCharacter::GetSingleton();
		RE::Actor* first = nullptr;
		for (auto& slot : t->slots) {
			auto* a = slot.Get();
			if (!a) continue;
			if (!first) first = a;
			if (a != player) return a;
		}
		return first;
	}

	// Every actor a test should touch. With a crosshair or console pick, just that one. In a
	// scene, everybody in it: the single-target version preferred the partner, so a test fired by
	// a male player landed on the female every time and looked like it did not work on males.
	std::vector<RE::Actor*> TestTargets()
	{
		std::vector<RE::Actor*> out;
		if (auto* a = CrosshairActor()) {
			out.push_back(a);
			return out;
		}
		std::scoped_lock l(Scenes::Lock());
		auto* t = Scenes::PlayerThread();
		if (!t) return out;
		for (auto& slot : t->slots) {
			if (auto* a = slot.Get()) out.push_back(a);
		}
		return out;
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

	// Auto, then every personality by id (kPersonalityIds). Submissive is in the full edition only.
#if OSIS_LITE
	constexpr const char* kPersonalities[] = { "Auto", "Balanced", "Stoic", "Vocal", "Shy", "Dominant", "Timid", "Wild", "Crazed" };
	constexpr int kPersonalityIds[] = { -1, 0, 1, 2, 3, 4, 5, 7, 8 };
#else
	constexpr const char* kPersonalities[] = { "Auto", "Balanced", "Stoic", "Vocal", "Shy", "Dominant", "Timid", "Submissive", "Wild", "Crazed" };
	constexpr int kPersonalityIds[] = { -1, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
#endif
	constexpr int kPersonalityCount = static_cast<int>(sizeof(kPersonalities) / sizeof(kPersonalities[0]));

	int PersonalityIndex(int id)
	{
		id = Face::Engine::EditionPersonality(id);  // the lite edition shows a submissive as the timid it plays as
		for (int i = 0; i < kPersonalityCount; ++i) {
			if (kPersonalityIds[i] == id) return i;
		}
		return 0;
	}
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

	constexpr const char* kBodySexes[] = { "Any body", "Female only", "Male only" };
	constexpr const char* kTongueModes[] = { "Hold the jaw open, stop lip-sync", "Hold the jaw open, keep lip-syncing", "Ignore" };
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
			Row("Ahegao Expressions (faces and face blush yield to it)", Face::Engine::AhegaoPresent());
			Row("Overlay Distribution Framework", Compat::ODFActive(), "installed (shares overlay slots)", "not installed");
			ig::EndTable();
		}
		ig::TextDisabled("%s", Face::Engine::AhegaoStatus().c_str());

		const auto conflicts = Compat::Conflicts();
		if (!conflicts.empty()) {
			ig::SeparatorText("Old mods still active");
			for (const auto& c : conflicts) ig::TextColored(kWarn, "%s", c.c_str());
			ig::TextWrapped("Disable these in your mod manager: the original OSED mods are replaced by this one, and the OSED Reborn <x> plugins are its Papyrus fallback, not an add-on.");
		}

		ig::SeparatorText("Scenes");
		const auto threads = Scenes::Snapshot();
		if (threads.empty()) ig::TextDisabled("No OStim scene running.");
		for (const auto& t : threads) {
#if OSIS_LITE
			const char* tone = t.rough ? "  [rough]" : "";
#else
			const char* tone = t.accepted ? "  [rough, accepted by a submissive]"
			                              : (t.consent ? (t.rough ? "  [rough, consensual]" : "") : (t.spell ? "  [non-consent: spell]" : "  [non-consent]"));
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
#if !OSIS_LITE
		ig::TextWrapped("Scene lock: %s", SceneLock::Status().c_str());
#endif
		ig::Text("Watcher: %s", Face::Engine::WatcherStatus().c_str());
		ig::Text("Faces being written: %zu", Face::Output::PaintedCount());

		ig::Spacing();
		if (ig::Button("Release all faces now")) OnGame([]() {
			Face::Output::ReleaseAll(0.3f);
			Face::Engine::ClearStrayTongues();
			Face::Engine::RestorePersistedTakeovers();
		});
		ig::SetItemTooltip("Emergency reset: neutral faces, any tongue we put out taken back, and OStim's own face writer "
		                   "back on for everyone.");
		ig::SameLine();
		if (ig::Button("Probe faces (30 s)")) OnGame([]() { Face::Output::ArmProbe(30.0f); });
		ig::SetItemTooltip("Diagnostics for a face that will not move. For 30 seconds after you close this menu, OSIS.log gets a "
		                   "line per actor per second: what OSIS asked for, what the game actually rendered, and whether "
		                   "anything else changed the face in between. Use it during a scene and send the log.");
		ig::SameLine();
		if (ig::Button("Probe faces (3 min)")) OnGame([]() { Face::Output::ArmProbe(180.0f); });
		ig::SetItemTooltip("The same, for three minutes - long enough to take in a climax. Click it, close the menu, and "
		                   "play the scene through.");
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
			Check("Paint NPCs in the player's scene", bIncludeNPCs,
				"Off: only the player's own face is driven, and their partners keep whatever OStim or another mod gives them.");
			Check("Direct NPC-only scenes too", bNPCOnlyScenes, "OStim NPCs and similar mods start scenes without the player. OSED 2.0 ignored them.");
			SliderF("NPC-only scene radius", fNPCSceneRadius, 500.0f, 10000.0f, "%.0f",
				"Scenes without the player that start farther away than this are left alone: too far to watch, and every "
				"painted actor costs a little work each frame.");
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
				ModuleRow("Arousal (body morphs)", Settings::Arousal::bEnabled, Compat::kArousal,
					"Arousal body morphs and body blush. Off for another arousal-morph mod.");
#if !OSIS_LITE
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

			SliderF("Strength", fGlobalStrength, 0.0f, 2.0f, "%.2f", "How large every expression comes out. 1.00 is the authored size.");
			if (iMode == kDirector) {
				Check("Play OStim's expression pools (Director)", bDirectorLibrary,
					"Director only. In the pleasure and anticipation phases the face is built from the expression pool OStim itself has for what "
					"the actor is doing (the same files, so installed expression packs apply), not from the built-in templates. Off: the old templates.");
				SliderF("Pleasure intensity (Director)", fDirectorGain, 0.0f, 2.0f, "%.2f",
					"Director only. The expression grammar was tuned as an overlay on OStim's own face, which makes it about half as strong "
					"when it is the whole face: mood, brows and mouth during the pleasure phase come out smaller than OStim's would. 0 leaves them "
					"as authored, 1.00 brings them up to OStim's amplitude at the same excitement, 2.00 well past it. Eyelids are not changed, "
					"and climax, plateau, afterglow and distress faces are left as they are.");
				Check("Own build-up faces (Director)", bBuildupFaces,
					"Director only. Faces of its own for the build-up to an orgasm - 22 in each of five stages (anticipation, warm, rising, heightened, "
					"the edge) - picked now and then beside OStim's expression pool for what the actor is doing, so nothing looks like it is "
					"repeating. Each is a combination of eyes (including slow blinks and closed eyes), brows, mouth and mood, weighted by the "
					"actor's personality and never one of the last eight. Consensual scenes only.");
				if (bBuildupFaces) {
					SliderF("Share of picks from the own faces", fBuildupShare, 0.0f, 1.0f, "%.2f",
						"How often a build-up pick is one of the Director's own faces and not one from OStim's pool. 0 is OStim's only, 1 is the "
						"Director's only. Where the scene gives no OStim pool, the own faces are always used.");
				}
				Check("Vary the climax face (Director)", bClimaxPool,
					"Director only. Each orgasm gets its own face from a pool of fifteen (open-mouthed gasp, clenched, lip bitten, wide-eyed, rolled up, smiling, "
					"snarl and so on), picked by the actor's personality and avoiding the one before; a run of rapid orgasms leans to the overwhelmed ones. "
					"Off: the one built-in climax face. Consensual scenes only.");
				SliderF("Climax face length (s)", fClimaxSeconds, 4.0f, 30.0f, "%.0f",
					"Director only. How long an actor's face stays on the climax after their own orgasm. A partner's face is not held by it.");
				SliderF("Rapid orgasm window (s)", fRapidOrgasmSeconds, 10.0f, 120.0f, "%.0f",
					"Director only. When an actor orgasms again within this many seconds of their last one, the climax face is cut shorter (never "
					"most of the gap), the afterglow is skipped or cut short, and the face between orgasms carries a tension that grows with each "
					"one and fades over this window. Orgasms further apart than this are treated as separate.");
			}
			SliderF("Style (realistic > cinematic > anime)", fStyle, 0.0f, 2.0f, "%.2f", "Above 1.5 enables the anime climax accents and tongue options.");
			SliderF("Eye strength", fEyeStrength, 0.0f, 1.5f, "%.2f",
				"The share of an expression that goes to the eyes: lids, squint and brow. Style raises it a little on its own.");
			ComboI("Profile", iProfile, kProfiles, 3, "A size preset on top of Strength: Subtle 70%, Normal 100%, Expressive 130%.");
			SliderF("Beat interval (s)", fBaseInterval, 1.0f, 8.0f, "%.1f", "How often the grammar picks a new face. Output is smoothed every frame in between.");
			SliderF("Interval jitter (s)", fIntervalJitter, 0.0f, 3.0f, "%.1f",
				"Random variation either way on the beat interval, so the face does not change on a metronome.");
			SliderF("Transition (s)", fTransition, 0.05f, 2.0f, "%.2f",
				"How long a new face takes to blend in. Short is snappy, long is dreamy.");

			ig::SeparatorText("Mouth");
			Check("Yield mouth to oral actions", bYieldOralMouth,
				"During an oral action the mouth belongs to the act, so the grammar stops writing it and only the eyes and "
				"brow keep going.");
			Check("Yield mouth to PPA in blowjobs", bYieldMouthToPPA,
				"While PPA (Procedural Penis Animations) is playing its mouth preset on whoever is giving a blowjob, leave their mouth "
				"and where they look to it: the Director stops writing the phonemes and stops turning the head, so nothing moves the "
				"mouth away from the penis. If PPA turns out not to be driving the mouth, OSIS takes it back after a few seconds. "
				"Director mode with the expression library on; the other modes already leave oral mouths to OStim.");
			ig::TextDisabled("PPA: %s", Face::PPA::Status().c_str());
			static std::string ppaResult;  // what the last button did; the page is drawn on one thread
			static bool ppaResultOk = true;
			if (const auto warn = Face::PPA::FaceWarning(); !warn.empty()) {
				ig::TextColored(kWarn, "PPA takes the eyes and brows on a blowjob:");
				ig::TextWrapped("%s", warn.c_str());
				if (ig::Button("Keep OSIS's eyes and brows")) {
					const auto r = Face::PPA::KeepFace();
					ppaResult = r.message;
					ppaResultOk = r.ok;
				}
				if (ig::IsItemHovered())
					ig::SetTooltip("Writes a small override file for PPA (%s in ppa-override-configs): a copy of the mouth preset that plays, "
						"with OverrideModifiers and OverrideExpressions off. PPA keeps the mouth and its morphs; OSIS keeps the eyes, brows and mood. "
						"No other mod's file is changed. PPA reads it at startup or on its reload key.", Face::PPA::OsisOverrideName());
			}
			if (Face::PPA::OsisOverrideInPlace()) {
				ig::TextDisabled("OSIS's PPA override is in place (%s).", Face::PPA::OsisOverrideName());
				if (ig::Button("Restore PPA's own setting")) {
					const auto r = Face::PPA::RestoreDefault();
					ppaResult = r.message;
					ppaResultOk = r.ok;
				}
				if (ig::IsItemHovered())
					ig::SetTooltip("Deletes OSIS's override file. PPA goes back to exactly what your other mods configure; OSIS's own file is the only thing removed.");
			}
			if (!ppaResult.empty()) {
				ig::TextColored(ppaResultOk ? kGood : kWarn, ppaResultOk ? "Done:" : "Not done:");
				ig::TextWrapped("%s", ppaResult.c_str());
			}
			Check("Yield mouth to dialogue", bDialogueMouthYield,
				"While a dialogue menu is open or an actor is speaking lines, leave their mouth alone so the talking reads right.");
			Check("Breathing clock", bBreathing, "Moan/breath mouth cycle between beats. Off by default: Lip-Sync drives the mouth from the real moans.");
			Check("Mouth variety", bMouthVariety,
				"Varies the mouth shape from beat to beat above excitement 70, instead of holding one open mouth. Consensual "
				"scenes only.");

			ig::SeparatorText("Anime");
			SliderF("Anime accent starts at excitement", fAnimeStart, 50.0f, 100.0f, "%.0f",
				"Where the anime accents begin. A climax starts them whatever this says. Needs Style above 1.5.");
			SliderF("Anime accent ends below", fAnimeEnd, 40.0f, 100.0f, "%.0f",
				"Once started they hold until excitement falls below this. Keep it under the start value, or the face flickers "
				"on and off at the boundary.");
			Check("Tongue at the peak (anime style)", bAnimeTongue,
				"The tongue comes out at the peak of a consensual scene. Needs Style above 1.5, and nothing else holding the mouth.");
			Check("Full tongue mode", bAnimeTongueFull,
				"Out sooner and for longer: from excitement 90 rather than 95, held about twice as long, and able to happen "
				"again after half the wait.");
			Check("Tongue life (rare small pulses)", bTongueLife,
				"Occasional small tongue flashes between the peaks, not only at them. Needs the peak tongue above, and runs "
				"the face on a faster tick.");
			Check("Leave faces to Ahegao Expressions if it is installed", bAhegaoAutoYield,
				"On by default. Ahegao Expressions drives the whole face on its own schedule - its tongue can come out at half "
				"arousal - so sharing a face with it only produces a fight. It also paints its own face blush, which competes "
				"for the same RaceMenu face overlay slots. While it is installed this mod writes no faces and no face blush, "
				"and keeps to the body: arousal morphs, body blush, climax. Untick to drive faces anyway.");
			Check("Always yield to an ahegao mod", bAhegaoModYield,
				"The same stand-down, forced on whether or not Ahegao Expressions is detected. For any other mod that drives "
				"faces and fights with this one.");
		}
		ig::Spacing();
		// Equipping OStim's tongue by hand is exactly what an ahegao mod does, so this exercises
		// the hand-over without waiting for one to fire.
		if (ig::Button("Test: tongue out for 8s")) OnGame([]() {
			auto* a = TestTarget();
			if (!a) {
				Papyrus::Notify("OSIS: aim at an actor, select one in the console, or start a scene");
				return;
			}
			Papyrus::EquipObject(a, "tongue");
			Papyrus::Notify("OSIS: tongue out for 8 s");
			Scheduler::After(8.0f, [h = a->GetHandle()]() {
				if (auto actor = h.get()) Papyrus::UnequipObject(actor.get(), "tongue");
			});
		});
		ig::SetItemTooltip("Puts OStim's tongue on the actor the way an ahegao mod does. In a scene, watch the Status page: the "
		                   "owner columns should switch to \"Ahegao mod\" while it is out, then switch back.");
		if (ig::Button("Test face")) OnGame([]() {
			if (auto* a = TestTarget()) {
				Face::Engine::TestOnActor(a);
				const auto h = a->GetHandle();
				Scheduler::After(6.0f, [h]() {
					if (auto p = h.get()) Face::Output::Release(p.get(), 0.6f);
				});
			} else {
				Papyrus::Notify("OSIS: aim at an actor, select one in the console, or start a scene");
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
				Check("Rich emotions (tender / lust / playful / surrender / detached)", bRichEmotions,
					"Pleasure takes a colour of its own, chosen from personality, role and partner, and actors react to each "
					"other's climax. Off: pleasure is one face.");
				Check("Role aware (loving / rough tags)", bRoleAware, "The scene's loving and rough tags tint every expression in it.");
				Check("Act-type aware (oral / kissing / penetration)", bActTypeAware,
					"Oral, kissing and penetration each get their own mouth and timing. Needs scene metadata, under Head and gaze.");
				Check("Positional dominance flavor", bPositionalDomSub,
					"Who is over whom in this position shapes the face, not just the scene's tags.");
				Check("Natural detail (asymmetry, micro-tics, relationship)", bNaturalDetail,
					"Uneven sides, small tics, and a plateau the face can settle into, so repeated beats stop looking identical.");
				Check("Climax choreography", bClimaxChoreo, "A built sequence through the orgasm instead of one held face.");
				Check("Cinematic afterglow", bCinematic, "A held breath just before the climax, and a few beats of afterglow after it.");
				Check("Exposure awareness (bashful when nude)", bExposureAware,
					"A nude actor who is not yet worked up is bashful about it. Consensual scenes only.");
				Check("Device awareness (gags, blindfolds)", bDeviceAware,
					"A gag changes the mouth (closed, or held open by a ring) and a blindfold the eyes. Reads Devious Devices.");
			}
			if (ig::CollapsingHeader("Timing", ig::ImGuiTreeNodeFlags_DefaultOpen)) {
				Check("Pace boost (stage changes + speed raise intensity)", bPaceBoost,
					"Changing scene and raising the animation speed push the face harder for a while.");
				Check("Speed sync", bSpeedSync, "The animation speed shows in the face: a fast scene reads faster.");
				Check("Excitement gradient", bExcitementGradient,
					"The face follows how fast excitement is climbing, not only how high it has got.");
				Check("Phrase grammar (five-beat phrase envelope)", bPhraseGrammar,
					"Beats are grouped into a five-beat phrase that builds and releases, instead of each one standing alone.");
				Check("Scenario cycler", bScenarioCycler, "Rotates through scene-long moods so a long scene does not settle into one.");
				Check("Overwhelm face", bOverwhelmFace,
					"Past a high excitement the face can be overwhelmed: slack mouth, eyes rolling up. Consensual scenes only, "
					"and never while something else holds the mouth.");
				Check("Group conductor (3+ actors)", bGroupConductor,
					"With three or more actors their beats are spread apart, so the group does not change face in unison.");
			}
#if !OSIS_LITE
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
				Check("Consent guardrails", bConsentGuardrails,
					"Holds the grammar inside what the scene allows: no pleasured face on a victim, and the anime accents, "
					"tongue and overwhelm stay out of a forced scene.");
				Check("Hard exclusion gate (distress owns the face)", bHardExclusionGate,
					"In a non-consensual scene the victim's face is distress, full stop. Off: distress competes with the other "
					"states beat by beat.");
				Check("No overwhelm/ahegao in distress", bNoDistressOverwhelm,
					"No gaping mouth or rolled-up eyes for anyone while a scene is distressed, the aggressor included.");
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
				Check("Gaze at partner", bGaze, "Actors look at their partner rather than straight ahead.");
#if !OSIS_LITE
				Check("Only hold gaze when consensual", bGazeConsentOnly, "A victim does not hold their aggressor's eyes.");
#endif
				Check("Headflow (throat arch, aversion, afterglow drop)", bHeadflow,
					"The head moves with the scene: throat arched back, turned away, dropping in the afterglow.");
				Check("Body demo (head tips back at climax)", bBodyDemo,
					"A plain head-back tip at climax. Only used when Headflow above is off.");
				Check("Use OStim scene metadata for roles", bRoleMetadata,
					"Reads each actor's role and actions from OStim's own scene files. Off: roles are guessed from the scene "
					"tags, and act-type awareness stops working.");
			}
			if (ig::CollapsingHeader("Pre-animation (normal state)")) {
				Check("Normal state layer", bNormalState,
					"Faces before the animation starts, while OStim is still setting the scene up.");
				SliderF("Intensity", fNormalIntensity, 0.0f, 1.0f, "%.2f", "Size of those pre-animation expressions.");
				SliderF("Gaze frequency", fNormalGazeFrequency, 0.0f, 1.0f, "%.2f", "How often they look at someone while waiting.");
				Check("Pre-warm from early excitement", bNormalPreWarm,
					"Excitement already shows on the face before the animation starts. Consensual scenes only.");
				Check("NPCs glance at the player", bNormalGlancePlayer, "An NPC in the scene looks the player over while waiting.");
				Check("Watcher trial (nearby NPC reacts)", bWatcher,
					"An NPC standing near the scene, not in it, reacts to what they are watching. Experimental: off by default.");
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
			int player = PersonalityIndex(iPlayerPersonality);
			if (ig::Combo("Player personality", &player, kPersonalities, kPersonalityCount)) {
				iPlayerPersonality = kPersonalityIds[player];
				g_dirty = true;
			}
			Check("SPID personality keywords", bSPIDPersonality,
				"OSED_Personality_DISTR.ini hands NPCs Bashful/Bold/Soft/Fierce keywords; OSIS_Personality_Timid, _Wild and _Crazed (alias _Yandere)"
#if !OSIS_LITE
				" and _Submissive"
#endif
				" are read too.");
			Check("Voice set shapes personality", bVoiceArchetype,
				"An actor's voice type suggests a personality when nothing else has set one.");
			Check("OBlush-aware shyness", bOBlushSync, "While OBlush has an actor blushing, the grammar treats them as shy.");
			ig::SeparatorText("What each personality is");
			ig::TextWrapped("Stoic: tolerates sex but isn't into it, and is waiting for the partner to be done: slow to build, muted at the plateau and the climax, "
				"nothing negative in it. "
				"Shy: uncomfortable with the idea of sex, though they enjoy it. Hesitation and guilt, overridden as they plateau and climax. "
				"Timid: enjoys it a lot, but the closeness is too much: eyes shut much of the time, an occasional peek at the partner. "
				"Vocal: likes it a lot but has no control over themselves: surprise, and loud voices. "
				"Wild: enjoys every moment, the intimacy and the pleasure alike, with only positive reactions, and tries to maximise them. "
				"Dominant: their own pleasure comes first and the partner is a toy; the partner's climax is held until the dominant's own. "
				"Crazed: obsessed with the partner: long creepy eye contact, drives them up to climax as fast as it can, and climaxes with them, "
				"with a creepy look, every time they do."
#if !OSIS_LITE
				" Submissive: turned on by rough scenes, and accepts any kind of scene, forced or not, from someone with a relationship rank of 3 or 4 "
				"to everyone in it - it then plays as consensual."
#endif
			);
			SliderF("Share of the newer types", fNewPersonalityShare, 0.0f, 1.0f, "%.2f",
				"Where nothing else places someone (no SPID keyword, voice type or AI value), this share of people get a timid, "
#if !OSIS_LITE
				"submissive, "
#endif
				"wild or crazed personality instead of one of the first five. It only matters when an actor's personality is first settled.");
			Check("Personality changes how fast excitement builds", bPersonalityExcitement,
				"Wild people build faster than OStim's own rate, stoic ones slower"
#if !OSIS_LITE
				", and submissive ones in a rough scene"
#endif
				". It scales OStim's excitement rate for that actor, for the length of the scene.");
			SliderF("Wild: excitement rate", fWildExcitementMult, 0.5f, 2.0f, "%.2f", "Times OStim's rate for a wild actor.");
			SliderF("Stoic: excitement rate", fStoicExcitementMult, 0.3f, 1.0f, "%.2f", "Times OStim's rate for a stoic actor, who is not into it.");
#if !OSIS_LITE
			SliderF("Submissive: excitement rate in rough scenes", fSubmissiveRoughMult, 0.5f, 2.0f, "%.2f",
				"Times OStim's rate for a submissive actor in a rough scene, or in one they accepted.");
#endif
			ig::SeparatorText("Dominant and crazed: control of the climax");
			Check("Dominant and crazed people control a partner's climax", bPersonalityControl,
				"A dominant holds their partner's climax (OStim's stall: the partner waits at the edge) until the dominant's own, and the two climax together. "
				"A crazed one builds their partner's excitement up fast and climaxes with them every time they do. Consensual scenes only, and the "
				"player is held like anyone else when their partner is a dominant. Off leaves the climaxes to OStim.");
			SliderF("Longest a partner is held at the edge (s)", fControlMaxHold, 20.0f, 600.0f, "%.0f",
				"The dominant lets them go after this long at the edge if they have not climaxed themselves.");
			SliderF("Dominant: excitement rate", fDominantExcitementMult, 0.5f, 2.0f, "%.2f", "Times OStim's rate for a dominant: their own pleasure comes first.");
			SliderF("Crazed: partner's excitement rate", fCrazedDriveMult, 1.0f, 4.0f, "%.2f", "Times OStim's rate for whoever a crazed actor is with (not a dominant).");
		}
		ig::TextWrapped("Each actor's personality is worked out the first time a scene needs it and then kept in your save game, "
			"so it is the same in every scene and after every load - the SPID roll is not. What you set below always wins.");
		if (ig::Button("Work every personality out again")) OnGame([]() {
			const auto n = Face::Engine::ForgetPinnedPersonalities();
			Papyrus::Notify(std::format("OSIS: forgot {} automatic personalit{}; they are settled again at the next scene", n, n == 1 ? "y" : "ies"));
		});
		if (ig::IsItemHovered()) ig::SetTooltip("Use after changing the SPID file or the sources above. Personalities you set yourself are kept.");
		ig::SeparatorText("Crosshair NPC");
		ig::TextWrapped("Aim at an NPC (or select one in the console), then pick a personality. Auto works it out afresh and pins that.");
		static int pick = 0;
		ig::Combo("##npcpers", &pick, kPersonalities, kPersonalityCount);
		ig::SameLine();
		if (ig::Button("Set")) OnGame([p = pick]() {
			auto* a = TestTarget();
			if (!a) return Papyrus::Notify("OSIS: aim at an NPC, select one in the console, or start a scene");
			Face::Engine::SetNpcPersonality(a, kPersonalityIds[p]);
			std::string src;
			const int arch = Face::Engine::Archetype(a, &src);
			Papyrus::Notify(std::format("{}: {} ({})", a->GetDisplayFullName(), Face::Engine::PersonalityName(arch), src));
		});
		ig::SameLine();
		if (ig::Button("Show")) OnGame([]() {
			auto* a = TestTarget();
			if (!a) return Papyrus::Notify("OSIS: aim at an NPC, select one in the console, or start a scene");
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
			Check("Enabled", bEnabled, "The same switch as Body on the General page.");
			SliderF("Strength", fStrength, 0.0f, 1.0f, "%.2f", "How much of the angles below a full-strength climax reaches.");
			Check("Toe curl", bToe, "Curl the toes at climax.");
			SliderF("Toe degrees", fToeDegrees, 0.0f, 90.0f, "%.0f",
				"How far the whole-foot toe bone bends at full strength. Past about 60 the toes start to pass through the sole on most foot meshes, so raise it and look before you keep it.");
			SliderF("Per-toe curl", fPerToe, 0.0f, 3.0f, "%.2f",
				"Extra bend at each toe's own two joints, on top of the whole-foot toe bone. Only shows on feet weighted to "
				"XPMSSE's per-toe bones (Aerosmith TJ Feet, for example); other feet look as before. 0 = off.");
			Check("Hand grip", bHand, "Curl the fingers at climax, and while a hand is working in a handjob.");
			SliderF("Finger degrees", fFingerDegrees, 0.0f, 120.0f, "%.0f",
				"How far each of a finger's three joints bends at full strength. Fingers have more travel than toes before "
				"they pass through the palm.");
			ig::SeparatorText("During foot actions");
			Check("Toes work during foot actions", bFootFlex,
				"Normally the toes only move at climax. With this on they hold a lower flex for as long as a footjob, "
				"foot grinding, foot kissing or tickling is running, building with excitement. Off by default: it moves the "
				"same bones OStim measures to time a footjob's peak, so check that peaks still feel right before keeping it.");
			if (bFootFlex) SliderF("Flex strength", fFootFlexScale, 0.0f, 1.0f, "%.2f", "A fraction of the climax curl angle.");

			ComboI("Curl axis", iCurlAxis, kAxes, 3, "Bone-local axis. If toes/fingers bend sideways, try another axis with the test button.");

			ig::SeparatorText("Arousal response (male bodies)");
			Check("Bend the genital bones with arousal", bGenitals,
				"XPMSSE gives male skeletons a six-bone genital chain. This bends it as arousal rises, the way the softbody "
				"morphs move a female body - no mesh morphs and no assets needed, but the schlong has to be weighted to "
				"those bones. Off by default: which way they bend depends on the rig, so set the axis below first.");
			if (bGenitals) {
				SliderF("Bend degrees", fGenitalDegrees, 0.0f, 120.0f, "%.0f",
					"Total bend across the chain at full arousal, spread along it with the base taking most of it.");
				ComboI("Bend axis", iGenitalAxis, kAxes, 3,
					"Bone-local axis. Use the test button and try each one: the right axis depends on how the schlong is rigged.");
			}
			Check("Scale with style", bStyleGated, "Realistic 35%, cinematic 70%, anime 100%.");
		}
		{
			std::scoped_lock l(Settings::lock);
			if (Settings::Body::bGenitals) {
				if (ig::Button("Test genital bend (6s)")) OnGame([]() {
					const auto actors = TestTargets();
					if (actors.empty()) {
						Papyrus::Notify("OSIS: aim at an actor, select one in the console, or start a scene");
						return;
					}
					for (auto* a : actors) Body::TestGenitals(a);
				});
				ig::SetItemTooltip("Holds the chain at full bend for six seconds, so you can see which axis is right.");
			}
		}
		if (ig::Button("Test")) OnGame([]() {
			const auto actors = TestTargets();
			if (actors.empty()) {
				Papyrus::Notify("OSIS: aim at an actor, select one in the console, or start a scene");
				return;
			}
			for (auto* a : actors) Body::Test(a);
		});
		ig::SetItemTooltip("Curls the toes and fingers once, as a climax would. Acts on the actor you are aiming at or have "
		                   "selected in the console; in a scene with neither, on everyone in it.");
		SaveBar();
	}

	void __stdcall RenderSkin()
	{
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::Skin;
#if OSIS_LITE
			ig::TextWrapped("Face overlays through RaceMenu's \"Face [Ovl#]\" slots. Blush follows excitement and the body's arousal flush; "
			                "saliva is a short climax beat.");
#else
			ig::TextWrapped("Face overlays through RaceMenu's \"Face [Ovl#]\" slots. Blush follows excitement and the body's arousal flush; "
			                "saliva is a short climax beat. Tears are reserved for non-consensual scenes: the victim wells up when distress starts "
			                "and at a forced climax. Without a tear texture, tears fall back to a welling-eyes expression.");
#endif
			Check("Enabled", bEnabled, "The same switch as Living Skin on the General page.");
			SliderF("Strength", fStrength, 0.0f, 1.5f, "%.2f", "Opacity of every face overlay at full effect.");
			Check("Blush", bBlush, "The cheeks flush with excitement and the body's arousal level.");
#if !OSIS_LITE
			Check("Tears", bTears,
				"On the victim of a non-consensual scene: welling up when distress starts, and again at a forced climax.");
			Check("Emotional Tears Effect", bEmoTears, "If EmoTearsSpells.esp is installed, a crying victim also gets its streaming tears until the scene ends.");
#endif
			Check("Saliva", bSaliva, "A short saliva beat at climax.");
			Check("Scale with style", bStyleGated, "Overlay opacity by Style: realistic 45%, cinematic 75%, anime full.");
			Check("Females only", bFemaleOnly, "Skip male actors. The stock textures are authored for female faces.");
			Check("Matte overlays", bMatteOverlays,
				"Zeroes the overlay's emissive and shine so it reads as colour in the skin rather than a sheen. If overlays show up "
				"as solid black squares (reported with some Community Shaders setups), turn this off and see if they come right. "
				"Applies to the face overlays and the body blush.");
			SliderI("First face overlay slot", iFaceFirstSlot, 0, 15, "Slot 0 is often makeup; OSED uses one slot per effect from here.");
			Path("Blush texture", sBlushPath, "Relative to Data\\textures, e.g. actors\\character\\Overlays\\FMS\\Blush\\Blush Cheeks 1.dds. Empty = auto.");
#if !OSIS_LITE
			Path("Tear texture", sTearPath,
				"Relative to Data\\textures. Empty: no overlay, and tears fall back to a welling-eyes expression.");
#endif
			Path("Saliva texture", sSalivaPath, "Relative to Data\\textures. Empty: no saliva overlay.");
		}
		const int slots = Skin::FaceOverlaySlots();
		ig::TextDisabled("RaceMenu face overlay slots: %d (skee64.ini [Overlays/Face] iNumOverlays)", slots);
		{
			std::scoped_lock l(Settings::lock);
			const int needed = Settings::Skin::iFaceFirstSlot + Skin::FaceSlotsNeeded();
			if (needed > slots) {
				ig::TextColored(kWarn, "RaceMenu has %d face overlay slots; this needs %d.", slots, needed);
				ig::TextColored(kWarn, "Set [Overlays/Face] iNumOverlays=%d in skee64.ini and restart.", needed);
			} else if (Compat::ODFActive()) {
				ig::TextDisabled("Overlay Distribution Framework also uses these slots. Slots another mod already holds are "
				                 "skipped, so raising iNumOverlays gives everyone room.");
			}
		}
		const auto blush = Skin::ResolvedPath(0);
		ig::TextDisabled("Blush texture in use: %s", blush.empty() ? "none" : blush.c_str());
#if !OSIS_LITE
		ig::TextDisabled("Emotional Tears Effect: %s", Skin::EmoTearsFound() ? "installed" : "not installed");
#endif
		if (Face::Engine::OBlushPresent()) ig::TextColored(kWarn, "OBlush is installed: OSED's face blush yields to it.");
		if (ig::Button("Test blush")) OnGame([]() { if (auto* a = TestTarget()) Skin::TestBlush(a); });
#if !OSIS_LITE
		ig::SameLine();
		if (ig::Button("Test tear")) OnGame([]() { if (auto* a = TestTarget()) Skin::TestTear(a); });
#endif
		ig::SameLine();
		if (ig::Button("Test saliva")) OnGame([]() { if (auto* a = TestTarget()) Skin::TestSaliva(a); });
		SaveBar();
	}

	void __stdcall RenderLipSync()
	{
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::LipSync;
			ig::TextWrapped("Moves the mouth with the moan OStim is actually playing: the moan files named by your OStim voice sets are "
			                "decoded at startup and followed frame by frame. No bake step, no FaceFX, no second voice.");
			Check("Enabled", bEnabled, "The same switch as Lip-Sync on the General page.");
			SliderF("Mouth gain", fGain, 0.2f, 2.0f, "%.2f", "How far the mouth opens per unit of loudness in the moan.");
			SliderF("Max opening", fMaxOpen, 0.2f, 1.0f, "%.2f", "The widest the mouth goes, however loud the moan gets.");
			SliderF("Attack (s)", fAttack, 0.005f, 0.2f, "%.3f", "How quickly the mouth follows a moan getting louder.");
			SliderF("Release (s)", fRelease, 0.02f, 0.5f, "%.3f",
				"How quickly it closes again. Longer than the attack, or the mouth chatters on every syllable.");
			Check("Eyes squeeze with the moan", bHoldEyes, "The eyes tighten through the loud part of a moan, not just the mouth.");

			ig::SeparatorText("While a tongue is out");
			ig::TextWrapped("Ahegao mods stick the tongue out through OStim. A mouth that closes over it pushes the tongue through "
			                "the lips and chin, so while a tongue is out the jaw is held open and the lips kept apart, whatever is "
			                "driving the mouth. This covers any mod that uses OStim's tongue, not only this one's.");
			ComboI("Mouth behaviour", iTongueMode, kTongueModes, 3,
				"Both of the first two hold the jaw open and the lips apart for as long as the tongue is out. They differ only in "
				"whether the moan still moves the mouth above that: stopping is the calmer look, keeping it leaves some life in the "
				"face. Ignore restores the old behaviour, where the mouth closed over the tongue.");
			if (iTongueMode != kTongueIgnore) SliderF("Hold the jaw open to", fTongueMinOpen, 0.0f, 1.0f, "%.2f",
				"Raise this if the tongue still clips at its widest.");

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

#if !OSIS_LITE
	void __stdcall RenderVoice()
	{
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::Voice;
			ig::TextWrapped("The victim of a non-consensual scene. OStim's moans and climax sounds are muted on them. Instead they cry for help "
			                "as it starts, and guards or allies in range answer. Then they protest, curse or beg by personality, scream at the "
			                "climax that breaks them, and after that only breathe hard. All lines are vanilla Skyrim dialogue in the actor's own voice.");
			Check("Enabled", bEnabled, "The same switch as Victim Voice on the General page.");
			Check("Mute OStim moans on the victim", bVictimNoMoans,
				"Silences OStim's moans and climax sounds on the victim, leaving room for the lines below. Off: both play "
				"over each other.");
			ComboI("Victim voice", iVictimVoice, kVoiceModes, 3,
				"Silent: nothing at all. Breathing only: hard breathing, no words. Full: the cry for help, the personality "
				"lines and the scream as well.");
			SliderF("Seconds between lines", fInterval, 3.0f, 30.0f, "%.0f", "Varies 40% either way. Panicked victims speak more often, numb ones less.");
			Check("Mute OStim dialogue on the victim", bMuteDialogue, "OStim's spoken scene comments (OActor.Mute), until the scene ends.");

			ig::SeparatorText("Call for help");
			Check("Cry for help as it starts", bCallForHelp,
				"The victim calls out when the scene starts. This is what brings the responders below; without it nobody "
				"knows to come.");
			ComboI("Who answers", iResponders, kResponderModes, 3,
				"Guards and allies in range attack the aggressor; the victim stays in the scene. When the aggressor is the player and a "
				"guard answered, the assault also goes on the player's bounty in the victim's hold. Combat can end the OStim scene.");
			SliderF("Answer range", fResponderRange, 256.0f, 8192.0f, "%.0f",
				"How far from the scene a guard or ally can be and still hear the cry. About 2000 is a city block.");

			ig::SeparatorText("Breaking climax");
			Check("Scream", bBreakScream, "A scream at the climax that breaks the victim, before the shocked face below.");
			SliderF("Shocked face (s)", fShockSeconds, 0.0f, 10.0f, "%.1f", "Before the vacant face of a broken victim. 0 = straight to vacant.");
		}
		ig::SeparatorText("Status");
		ig::TextWrapped("%s", Voice::Status().c_str());
		if (ig::Button("Test cry for help")) OnGame([]() { Voice::TestHelp(TestTarget()); });
		ig::SameLine();
		if (ig::Button("Test line")) OnGame([]() { Voice::TestLine(TestTarget()); });
		ig::SameLine();
		if (ig::Button("Test scream")) OnGame([]() { Voice::TestScream(TestTarget()); });
		ig::SameLine();
		if (ig::Button("Test breathing")) OnGame([]() { Voice::TestBreath(TestTarget()); });
		ig::TextDisabled("Tests speak on the crosshair actor; they don't call anyone.");
		SaveBar();
	}
#endif

	void __stdcall RenderArousal()
	{
		{
			std::scoped_lock l(Settings::lock);
			using namespace Settings::Arousal;
			ig::SeparatorText("Arousal");
			Check("Enabled", bEnabled, "The same switch as Arousal on the General page.");
			Check("Affect player", bAffectPlayer, "Drive the player's own morphs and body blush.");
			Check("Affect nearby NPCs", bAffectNPCs, "Drive NPCs around the player too, within the limits below.");
			ComboI("Arousal source", iSource, kSources, 4,
				"Where the arousal number comes from. Auto takes OSL Aroused if it is installed, then SLO Aroused NG, then "
				"OStim excitement on its own. The Status page says which was found.");
			SliderF("Intensity", fIntensity, 0.0f, 2.0f, "%.2f", "Multiplier on every morph's change and the body blush.");
			SliderI("Max NPCs", iMaxNPCs, 0, 20, "At most this many NPCs are tracked at once. Each one costs morph work.");
			SliderF("NPC radius", fRadius, 500.0f, 8000.0f, "%.0f", "NPCs farther from the player than this are left alone.");
			SliderF("Update interval (s)", fInterval, 0.25f, 10.0f, "%.2f",
				"Seconds between arousal recalculations. Lower reacts sooner and costs more; the morphs are smoothed in "
				"between either way.");

			ig::SeparatorText("Scene factors");
			Check("OStim excitement is an arousal floor in scenes", bOStimExcitement,
				"During a scene the body never reads below OStim's excitement, however low the arousal mod has them. Off: "
				"an actor with no tracked arousal stays flat through the whole scene.");
			Check("Climax / edging / afterglow", bSceneFactors,
				"An orgasm pushes to full engorgement for the hold time; edging holds high; afterwards the level eases back to the reported arousal, never to zero.");
			SliderF("Climax hold (s)", fClimaxHold, 0.0f, 30.0f, "%.0f",
				"Seconds at full engorgement after an orgasm before it starts easing back.");
			Check("Personality shapes the response", bPersonality, "Vocal, dominant, crazed and wild engorge faster, stoic slower; shy and timid flush harder.");

			ig::SeparatorText("Physiological response");
			SliderF("Engorgement half-life (s)", fRiseHalfLife, 1.0f, 120.0f, "%.0f",
				"Seconds to cover half the distance to the target while the level is rising.");
			SliderF("Resolution half-life (s)", fFallHalfLife, 1.0f, 600.0f, "%.0f",
				"The same while it falls. Longer than the rise on purpose: a body settles slower than it responds.");

			ig::SeparatorText("Shape of the response");
			Check("Shape the response", bShapedResponse,
				"How much of each morph, body blush and genital bend shows. On: a floor as soon as the actor is aroused, a logarithmic rise "
				"(quick at first, then flattening) to a ceiling through the build-up, a lift towards the top only in the last seconds before an "
				"orgasm, and the full range only at the orgasm. Off: the old curve, where every morph reached its maximum early and stayed there.");
			if (bShapedResponse) {
				SliderF("Floor", fResponseFloor, 0.0f, 1.0f, "%.2f",
					"The share of each morph's range that shows as soon as it is engaged: apparent, not exaggerated. A morph still waits for its own "
					"start level before it moves at all.");
				SliderF("Ceiling until the last seconds", fResponseCeiling, 0.0f, 0.95f, "%.2f",
					"The most that shows through the build-up, however high the arousal. The rise towards it is logarithmic: quick early, flat later.");
				SliderF("Last seconds before orgasm", fPeakWindow, 1.0f, 30.0f, "%.0f",
					"The lift from the ceiling to the top begins this many seconds before the orgasm, as estimated from how fast OStim excitement is "
					"rising. Full only once the orgasm happens. With no scene there is no orgasm to wait for, so the ceiling holds.");
			}
			SliderF("Stoic: response at rest", fStoicRest, 0.0f, 0.6f, "%.2f",
				"A stoic is not excited by sex: only purely physiological reactions show, and this is the share of each range that does. "
				"A stoic blushes - the face and the body alike - only in the last seconds before the orgasm and during it.");
			SliderF("Stoic: response at the peak", fStoicPeak, 0.0f, 0.95f, "%.2f",
				"The most a stoic's body shows, in the last seconds before an orgasm and for the first four seconds of it; it then falls "
				"back to the rest level within a few seconds, and the blush goes with it.");
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
				ig::Text("arousal %3.0f  target %3.0f%%  level %3.0f%%  shown %3.0f%%  (%s)", r.arousal, r.target * 100.0f, r.level * 100.0f, r.shown * 100.0f, r.why.c_str());
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
				SliderF("Start", m.start, 0.0f, 1.0f, "%.2f", "Response level where this slider starts to move.");
				SliderF("Full", m.full, 0.0f, 1.0f, "%.2f", "Response level where it reaches Max.");
				SliderF("Rest", m.rest, -1.0f, 1.5f, "%.2f", "Where the slider sits on an unaroused body.");
				SliderF("Max", m.max, -1.0f, 1.5f, "%.2f", "Where it sits at full response. Negative is allowed: some sliders "
					"read the other way round.");
				ComboI("Body", m.sex, kBodySexes, 3, "CBBE/3BA slider names mean nothing on a male body, so the stock rows are female "
					"only. Add rows set to Male if your male body has morphs worth driving.");
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
			Check("Enable body blushing", bBlush,
				"Needs Body Blushing (iAmChe) installed for the textures, unless every row below points at your own.");
			SliderI("First slot", iOverlayFirstSlot, 0, 31, "Slots below this are left for other overlay mods.");
			SliderI("Slots to use", iOverlaySlots, 0, 16,
				"How many regions can show at once. Each takes one body overlay slot, so this is also how many slots are claimed.");
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
				SliderF("Start", b.start, 0.0f, 1.0f, "%.2f", "Response level where this region starts to show.");
				SliderF("Full", b.full, 0.0f, 1.0f, "%.2f", "Response level where it reaches Max.");
				SliderF("Max", b.max, 0.0f, 1.0f, "%.2f", "Overlay opacity at full response, before the per-race multiplier.");
				if (b.full <= b.start) b.full = std::min(1.0f, b.start + 0.01f);
				{
					char tex[200];
					strncpy_s(tex, b.texture.c_str(), _TRUNCATE);
					if (ig::InputText("Texture", tex, sizeof(tex))) {
						b.texture = tex;
						g_dirty = true;
					}
					ig::SetItemTooltip("Empty: use Body Blushing's region of this name. Otherwise a path under Data\textures, "
					                   "e.g. actors\\character\\overlays\\MyBlush\\chest.dds");
				}
				ComboI("Body", b.sex, kBodySexes, 3, "Body Blushing's textures are painted on the female UV, so the stock rows are "
					"female only. A male body needs its own textures: add rows, set this to Male and point them at your files.");
				{
					bool custom = b.tint >= 0;
					if (ig::Checkbox("Own colour", &custom)) {
						b.tint = custom ? 0xFF2030 : -1;
						g_dirty = true;
					}
					ig::SetItemTooltip("Off: the per-race colour. On: this region keeps the colour below, even on a race that would "
					                   "otherwise not blush at all.");
					if (b.tint >= 0) {
						float col[3]{ ((b.tint >> 16) & 0xFF) / 255.0f, ((b.tint >> 8) & 0xFF) / 255.0f, (b.tint & 0xFF) / 255.0f };
						if (ig::ColorEdit3("Colour", col)) {
							b.tint = (static_cast<std::int32_t>(col[0] * 255.0f + 0.5f) << 16) |
							         (static_cast<std::int32_t>(col[1] * 255.0f + 0.5f) << 8) |
							         static_cast<std::int32_t>(col[2] * 255.0f + 0.5f);
							g_dirty = true;
						}
					}
				}
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

			ig::SeparatorText("Per-race visibility");
			ig::TextWrapped("The overlays are one grey texture tinted per race, so the same opacity looks subtle on dark skin and "
			                "overpowering on pale skin. These multiply the alpha for that race. The name is matched anywhere in the "
			                "race's editor ID, so \"nord\" also covers modded Nord variants; the first match wins, and a race with no "
			                "row here stays at 1.00.");
			int removeRace = -1;
			for (size_t i = 0; i < raceBlush.size(); ++i) {
				auto& r = raceBlush[i];
				ig::PushID(static_cast<int>(i) + 2000);
				SliderF(r.race.c_str(), r.mult, 0.0f, 3.0f, "%.2fx", "Multiplies the body blush opacity for this race.");
				ig::SameLine();
				if (ig::Button("Remove")) removeRace = static_cast<int>(i);
				ig::PopID();
			}
			if (removeRace >= 0) {
				raceBlush.erase(raceBlush.begin() + removeRace);
				g_dirty = true;
			}
			ig::InputText("Race name", g_newRace, sizeof(g_newRace));
			ig::SetItemTooltip("Part of the race's editor ID, lower case: nord, redguard, darkelf, orc, highelf...");
			ig::SameLine();
			if (ig::Button("Add race") && g_newRace[0]) {
				std::string id = g_newRace;
				std::ranges::transform(id, id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				raceBlush.push_back({ id, 1.0f });
				g_newRace[0] = '\0';
				g_dirty = true;
			}
			if (ig::Button("Restore default multipliers")) {
				raceBlush = DefaultRaceBlush();
				g_dirty = true;
			}
			if (auto* a = CrosshairActor()) {
				if (auto* race = a->GetRace()) {
					if (const char* id = race->GetFormEditorID(); id && *id) {
						ig::TextDisabled("Crosshair actor's race: %s", id);
					}
				}
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
#if !OSIS_LITE
	SKSEMenuFramework::AddSectionItem("Victim Voice", RenderVoice);
#endif
	SKSEMenuFramework::AddSectionItem("Arousal", RenderArousal);
	SKSEMenuFramework::AddSectionItem("Arousal/Morphs", RenderMorphs);
	SKSEMenuFramework::AddSectionItem("Arousal/Body Blush", RenderBlush);
}
