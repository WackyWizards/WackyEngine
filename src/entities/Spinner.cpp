#include "Spinner.h"
#include "core/Game.h"
#include <cmath>
#include <iostream>

REFLECT_FIELD(SpinnerEntity, rotateSpeed)
REFLECT_FIELD(SpinnerEntity, bobSpeed)
REFLECT_FIELD(SpinnerEntity, spritePath)

void SpinnerEntity::OnBeginPlay()
{
	startY = transform.position.y;
	phase = 0.f;

	texture = g_engine.loadTexture(spritePath.c_str());

	if (!texture)
	{
		std::cout << "[SpinnerEntity] Failed to load sprite: " << spritePath << "\n";
		return;
	}

	g_engine.bindTexture(texture);
}

void SpinnerEntity::OnUpdate()
{
	if (!texture)
	{
		Sprite fallback;
		fallback.x = 0.f;
		fallback.y = startY + std::sin(phase) * 0.2f;
		fallback.halfW = 0.1f;
		fallback.halfH = 0.1f;
		fallback.r = 1.f;
		fallback.g = 0.f;
		fallback.b = 1.f;
		Scene::Push(fallback);
		return;
	}

	const float bobY = startY + std::sin(phase) * 0.2f;
	Scene::PushTextured(0.f, bobY, 0.1f, 0.1f);
}

void SpinnerEntity::OnFixedUpdate()
{
	phase += bobSpeed * Time::FixedDelta();
}

void SpinnerEntity::OnEndPlay()
{
	if (texture)
	{
		g_engine.destroyTexture(texture);
		texture = nullptr;
	}
}