#define GLFW_INCLUDE_VULKAN
#include "core/Game.h"
#include "core/World.h"
#include "core/EntityRegistry.h"
#include "runtime/RuntimeRenderer.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <chrono>

/**
 * Declared by the game's compiled-in Game.cpp.
 */
extern "C" Game* CreateGame();

/**
 * g_engine is declared extern in Game.h and must be defined exactly once.
 * In export builds this file owns the definition (Game.cpp's definition
 * is suppressed by the #ifndef WACKY_EXPORT guard).
 */
EngineBindings g_engine;

/**
 * EngineBindings stores plain C function pointers, not std::function.
 * Capturing lambdas cannot convert to plain function pointers - only
 * captureless ones can. Runtime state is therefore in static storage
 * so the binding lambdas can reach it without capturing.
 */
namespace
{
	static float s_scaledDelta = 0.f;
	static float s_elapsed = 0.f;
	static float s_fixedDelta = 0.02f;
	static bool s_keys[512]{};
	static SpriteList s_pending{};
}

void RuntimeRun(const std::string& startWorldPath)
{
	if (!glfwInit())
	{
		throw std::runtime_error("GLFW init failed");
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(1280, 720, "Game", nullptr, nullptr);

	RuntimeRenderer renderer(window);

	std::unique_ptr<Game> game(CreateGame());

	EngineBindings bindings;
	bindings.getDelta = []() -> float
	{
		return s_scaledDelta;
	};
	bindings.getElapsed = []() -> float 
	{
		return s_elapsed;
	};
	bindings.getFixedDelta = []() -> float
	{
		return s_fixedDelta;
	};
	bindings.keyHeld = [](Key k) -> bool
	{
		const int i = static_cast<int>(k);
		return i >= 0 && i < 512 ? s_keys[i] : false;
	};
	bindings.pushSprite = [](Sprite s)
	{
		s_pending.Push(s);
		};

	g_engine = bindings;

	World world;
	world.LoadFromJson(startWorldPath, [](const char* typeName) -> Entity*
		{
			for (const auto& info : EntityRegistry::Get().GetAll())
			{
				if (info.typeName && strcmp(info.typeName, typeName) == 0 && info.factory)
				{
					return info.factory();
				}
			}
			return nullptr;
		});

	game->Init(bindings);
	world.BeginPlay(); 

	float fixedAccum = 0.f;
	auto prev = std::chrono::steady_clock::now();
	const auto start = prev;

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		auto  now = std::chrono::steady_clock::now();
		float delta = std::chrono::duration<float>(now - prev).count();
		if (delta > 0.1f)
		{
			delta = 0.1f;
		}
		prev = now;

		s_elapsed = std::chrono::duration<float>(now - start).count();
		s_scaledDelta = delta;

		/** Fixed update */
		fixedAccum += delta;
		constexpr int MAX_FIXED_TICKS = 8;
		int ticks = 0;
		while (fixedAccum >= s_fixedDelta && ticks < MAX_FIXED_TICKS)
		{
			world.FixedUpdate();
			game->OnFixedUpdate();
			fixedAccum -= s_fixedDelta;
			++ticks;
		}

		/** Variable update */
		s_pending.Clear();
		world.Update();
		game->OnUpdate();

		renderer.DrawFrame(s_pending);

		/** Key state */
		for (int k = 0; k < 512; k++)
		{
			s_keys[k] = glfwGetKey(window, k) == GLFW_PRESS;
		}
	}

	game->Shutdown();
	renderer.WaitIdle();
	glfwDestroyWindow(window);
	glfwTerminate();
}