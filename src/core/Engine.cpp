#define GLFW_INCLUDE_VULKAN
#include "Engine.h"
#include "Game.h"
#include "renderer\Renderer.h"
#include <iostream>
#include <mutex>
#include <atomic>
#include <windows.h>

struct GameDLL
{
	HMODULE handle = nullptr;
	Game* game = nullptr;

	explicit GameDLL(const std::string& path)
	{
		handle = LoadLibraryA(path.c_str());
		if (!handle)
		{
			throw std::runtime_error("Failed to load game DLL: " + path);
		}

		auto createFn = reinterpret_cast<CreateGameFn>(GetProcAddress(handle, "CreateGame"));
		if (!createFn)
		{
			throw std::runtime_error("Game DLL missing CreateGame export");
		}

		game = createFn();
		if (!game)
		{
			throw std::runtime_error("CreateGame() returned nullptr");
		}
	}

	~GameDLL()
	{
		delete game;
		if (handle)
		{
			FreeLibrary(handle);
		}
	}

	GameDLL(const GameDLL&) = delete;
	GameDLL& operator=(const GameDLL&) = delete;
};

namespace
{
	float s_delta = 0;
	float s_elapsed = 0;
	bool s_keys[512]{};
	bool s_pressedLatch[512]{};

	// Protect s_scene properly
	// Double-buffering: main writes to s_pending, render thread swaps it in.
	SpriteList s_pending{};
	SpriteList s_render{};
	EditorStats s_editorStats{};
	std::mutex s_sceneMutex;

	// Set by the render thread (via EngineUI callback), consumed by the main thread.
	std::atomic s_reloadRequested = false;
}

void Engine::Run(const std::string& gameDllPath)
{
	if (!glfwInit())
	{
		throw std::runtime_error("GLFW init failed");
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(800, 600, "WackyEngine", nullptr, nullptr);

	try
	{
		auto dll = std::make_unique<GameDLL>(gameDllPath);
		Renderer renderer(window, []
		{
			s_reloadRequested = true;
		});

		/*
		* Build bindings - lambdas capture engine-side state.
		* These are passed into the DLL so g_engine (which lives in the DLL)
		* gets populated. The DLL's Time/Input/Scene classes then read from
		* g_engine, which is in the same address space as the function pointers.
		*/
		EngineBindings bindings;
		bindings.getDelta = []() -> float
			{
				return s_delta;
			};
		bindings.getElapsed = []() -> float
			{
				return s_elapsed;
			};
		bindings.keyHeld = [](Key k) -> bool
			{
				const int i = static_cast<int>(k);
				return i >= 0 && i < 512 ? s_keys[i] : false;
			};
		bindings.keyPressed = [](Key k) -> bool
			{
				const int i = static_cast<int>(k);
				if (i < 0 || i >= 512)
				{
					return false;
				}
				if (s_pressedLatch[i])
				{
					s_pressedLatch[i] = false;
					return true;
				}

				return false;
			};
		bindings.pushSprite = [](Sprite s)
			{
				s_pending.Push(s); // called from main thread during Update(), no lock needed
			};

		dll->game->Init(bindings);

		std::atomic running = true;

		// Render thread
		std::thread renderThread([&]
			{
				while (running)
				{
					// Swap in the latest scene and stats under lock, then render without holding it
					SpriteList  scene;
					EditorStats stats;
					{
						std::scoped_lock lock(s_sceneMutex);
						scene = s_pending;
						stats = s_editorStats;
					}
					renderer.DrawFrame(scene, stats);
				}
				renderer.WaitIdle();
			});

		// Main thread; event pump + game update
		const auto start = std::chrono::steady_clock::now();
		auto prev = start;
		bool prevKeys[512]{};

		while (!glfwWindowShouldClose(window))
		{
			glfwPollEvents();

			// Reload requested by the UI - runs between frames so Update() is never mid-flight.
			if (s_reloadRequested.exchange(false))
			{
				dll->game->Shutdown();
				dll = std::make_unique<GameDLL>(gameDllPath);
				dll->game->Init(bindings);
			}

			auto now = std::chrono::steady_clock::now();
			s_delta = std::chrono::duration<float>(now - prev).count();
			s_elapsed = std::chrono::duration<float>(now - start).count();
			prev = now;
			if (s_delta > 0.1f)
			{
				s_delta = 0.1f;
			}

			for (int k = 0; k < 512; k++)
			{
				const bool held = glfwGetKey(window, k) == GLFW_PRESS;

				if (held && !prevKeys[k])
				{
					s_pressedLatch[k] = true;
				}

				prevKeys[k] = held;
				s_keys[k] = held;
			}

			// Build the new scene, pushSprite writes into s_pending
			s_pending.Clear();
			dll->game->Update();

			// Build editor stats, smooth FPS with a simple exponential average
			static float smoothFps = 0.f;
			const float rawFps = s_delta > 0.f ? 1.f / s_delta : 0.f;
			smoothFps = smoothFps * 0.9f + rawFps * 0.1f;

			// Publish scene + stats to render thread
			{
				std::scoped_lock lock(s_sceneMutex);
				s_render = s_pending;
				s_editorStats = {.deltaTime = s_delta, .fps = smoothFps };
			}
		}

		dll->game->Shutdown();
		running = false;
		renderThread.join();
	}
	catch (const std::exception& e)
	{
		std::cout << "\nFATAL ERROR: " << e.what() << "\n";
		std::cout << "Press Enter to exit...";
		std::cin.get();
	}

	glfwDestroyWindow(window);
	glfwTerminate();
}