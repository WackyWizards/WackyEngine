#pragma once

#include "core/EntityRegistry.h"

class SpinnerEntity : public Entity
{
	ENTITY_TYPE(SpinnerEntity, "Examples")

public:
	float rotateSpeed = 90.f; // degrees per second
	float bobSpeed = 2.f; // oscillation frequency

private:
	float startY = 0.f;
	float phase = 0.f;

public:
	void OnBeginPlay() override;
	void OnFixedUpdate() override;
};
