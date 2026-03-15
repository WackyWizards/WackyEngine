#pragma once

#include "Object.h"

class World;

struct Position
{
	float x = 0.f;
	float y = 0.f;
};

struct Rotation
{
	float angle = 0.f;
};

struct Scale
{
	float x = 1.f;
	float y = 1.f;
};

struct Transform
{
	Position position{};
	Rotation rotation{};
	Scale scale{};
};

/**
 * Base class for all world objects.
 *
 * Lifecycle order per play session:
 *   OnSpawn()      – right after the entity enters the world (editor or play)
 *   OnBeginPlay()  – play mode starts (snapshot already taken)
 *   FixedUpdate()  – fixed-rate physics tick; use Time::FixedDelta() for the step
 *   Update()       – per-frame variable tick; use Time::Delta() for elapsed seconds
 *   OnEndPlay()    – play mode stops (world about to be restored from snapshot)
 *   OnDestroy()    – entity being removed from world
 *
 * Access the owning world at any time via GetWorld().
 * Register subclasses with the editor using the ENTITY_TYPE macro.
 */
class Entity : public Object
{
public:
	Entity()
	{
		GenerateId();
		name = "Entity";
	}

	virtual ~Entity() = default;

	virtual const char* GetTypeName() const override
	{
		return "Entity";
	}

	bool active = true;
	Transform transform;

	/** Called once when the entity enters the world. */
	virtual void OnSpawn() {}

	/** Called once when Play mode begins. */
	virtual void OnBeginPlay() {}

	virtual void OnFixedUpdate() {}

	virtual void OnUpdate() {}

	/** Called once when Play mode stops (world is about to be restored). */
	virtual void OnEndPlay() {}

	/** Called once just before the entity is removed from the world. */
	virtual void OnDestroy() {}

	/** Valid after OnSpawn; never nullptr while the entity is alive. */
	World* GetWorld() const;

protected:
	World* world = nullptr;
	friend class World;
};

struct EntityTypeInfo
{
	const char* typeName = nullptr;
	const char* category = nullptr;
	Entity* (*factory)() = nullptr;
};