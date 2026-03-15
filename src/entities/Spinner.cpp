#include "Spinner.h"
#include <core/Game.h>
#include <cmath>

REFLECT_FIELD(SpinnerEntity, rotateSpeed)
REFLECT_FIELD(SpinnerEntity, bobSpeed)

void SpinnerEntity::OnBeginPlay()
{
	startY = transform.position.y;
	phase = 0.f;
}

void SpinnerEntity::OnFixedUpdate()
{
	const float fixedDeltaTime = Time::FixedDelta();
	transform.rotation.angle += rotateSpeed * fixedDeltaTime;
	if (transform.rotation.angle >= 360.f)
	{
		transform.rotation.angle -= 360.f;
	}

	phase += bobSpeed * fixedDeltaTime;
	transform.position.y = startY + std::sin(phase) * 0.5f;
}
