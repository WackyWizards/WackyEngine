#pragma once

#include "core/EntityRegistry.h"
#include "renderer/Texture.h"

class SpinnerEntity : public Entity
{
	ENTITY_TYPE(SpinnerEntity, "Examples")

public:
	/** Degrees per second */
	float rotateSpeed = 90.f;

	/** Oscillation frequency */
	float bobSpeed = 2.f;

	/** Path to the PNG sprite to display. */
	std::string spritePath = "assets/spinner.png";

private:
	float startY = 0.f;
	float phase = 0.f;
	Texture* texture = nullptr;

public:
	void OnBeginPlay() override;
	void OnUpdate() override;
	void OnFixedUpdate() override;
	void OnEndPlay() override;
};