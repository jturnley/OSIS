#pragma once

// OStim scene metadata read straight from OStim's JSON files, replacing the per-tick
// OMetadata Papyrus queries the original scripts made. Scene files are indexed at data
// load and parsed on first use; action definitions (tags, aliases) are loaded up front.
namespace OStimData
{
	using TagList = std::vector<std::string>;  // lowercase

	struct Action
	{
		std::string type;  // canonical, lowercase
		int actor = -1;
		int target = -1;
		int performer = -1;
	};

	struct SceneActor
	{
		TagList tags;
		std::string intendedSex;
	};

	struct Scene
	{
		std::string id;
		TagList tags;
		std::vector<SceneActor> actors;
		std::vector<Action> actions;
		int maxSpeed = 0;      // highest speed index
		int defaultSpeed = 0;
		bool known = false;    // false when no scene file was found
	};

	using ScenePtr = std::shared_ptr<const Scene>;

	void Init();
	[[nodiscard]] std::size_t SceneCount();
	[[nodiscard]] std::size_t ActionCount();

	[[nodiscard]] ScenePtr GetScene(std::string_view a_id);

	[[nodiscard]] TagList SplitCSV(std::string_view a_csv);
	[[nodiscard]] bool HasAny(const TagList& a_have, const TagList& a_want);

	// OMetadata equivalents. Positions are scene actor indices.
	[[nodiscard]] bool HasAnySceneTag(const Scene& a_scene, const TagList& a_tags);
	[[nodiscard]] bool HasAnyActorTag(const Scene& a_scene, int a_pos, const TagList& a_tags);
	[[nodiscard]] bool ActionHasAnyTag(const Action& a_action, const TagList& a_tags);
	[[nodiscard]] bool HasAnyActionTagOnAny(const Scene& a_scene, const TagList& a_tags);
	[[nodiscard]] int FindAnyAction(const Scene& a_scene, const TagList& a_types);
	[[nodiscard]] int FindAnyActionForActor(const Scene& a_scene, int a_pos, const TagList& a_types);
	[[nodiscard]] int FindAnyActionForTarget(const Scene& a_scene, int a_pos, const TagList& a_types);
	[[nodiscard]] int FindAnyActionForPerformer(const Scene& a_scene, int a_pos, const TagList& a_types);
	[[nodiscard]] int FindActionTaggedForActor(const Scene& a_scene, int a_pos, const TagList& a_actionTags);
	[[nodiscard]] int FindActionTaggedForTarget(const Scene& a_scene, int a_pos, const TagList& a_actionTags);
}
