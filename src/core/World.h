#pragma once

#include "Entity.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>

using EntityResolver = std::function<Entity* (const char* typeName)>;

/**
 * Owns a flat list of Entities and drives their full lifecycle.
 *
 * Fixed-update loop (called by Engine):
 *   Engine accumulates scaled delta each frame, then calls
 *   World::FixedUpdate(fixedStep) for each full tick that has accumulated.
 *   fixedStep defaults to 0.02 s (50 Hz) and is configurable in World Settings.
 */
class World
{
public:
	std::string name = "Untitled World";

	/**
	 * Fixed timestep in seconds. Engine reads this each frame to decide how
	 * many FixedUpdate ticks to fire. Change it in World Settings.
	 * Default: 0.02 (50 Hz).
	 */
	float fixedStep = 0.02f;

	Entity* CreateEntity(const std::string& entityName = "Entity");
	Entity* SpawnEntity(Entity* entity, const std::string& entityName = "");
	void DestroyEntity(Entity* entity);
	void Clear();

	[[nodiscard]]
	const std::vector<std::unique_ptr<Entity>>& GetEntities() const
	{
		return entities;
	}

	std::vector<std::unique_ptr<Entity>>& GetEntities()
	{
		return entities;
	}

	void BeginPlay();
	void FixedUpdate();
	void Update();
	void EndPlay();

	bool SaveToJson(const std::string& path) const;
	std::string SerializeToString()  const;
	bool LoadFromJson(const std::string& path, EntityResolver resolver = nullptr);
	bool DeserializeFromString(const std::string& json, EntityResolver resolver = nullptr);

private:
	std::vector<std::unique_ptr<Entity>> entities;
	void InitEntity(Entity* e);
};