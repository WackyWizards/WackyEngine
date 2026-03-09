#define GLFW_INCLUDE_VULKAN
#include "Engine.h"
#include "Game.h"
#include "Project.h"
#include "ProjectManager.h"
#include "renderer\Renderer.h"
#include <iostream>
#include <fstream>
#include <atomic>
#include <thread>
#include <mutex>
#include <windows.h>
#include <filesystem>

// Diagnostic log
std::ofstream g_log;
static void Log(const char* msg)
{
	if (!g_log.is_open())
	{
		g_log.open("wacky_log.txt", std::ios::out | std::ios::trunc);
	}
	g_log << msg << "\n";
	g_log.flush(); // flush every line so the file is valid even if we hang
}

namespace fs = std::filesystem;

// GameDLL
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

/**
 * Walk up from the engine executable until we find a directory that contains
 * core/Game.h. Works for both in-source and out-of-source builds (e.g. build/Debug/engine.exe → <root>/core/Game.h).
 * Returns an empty string if not found.
 */
static std::string FindEngineCoreDir()
{
	char exeBuf[MAX_PATH];
	GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);

	for (fs::path p = fs::path(exeBuf).parent_path(); p != p.parent_path(); p = p.parent_path())
	{
		if (fs::exists(p / "src" / "core" / "Game.h"))
		{
			return (p / "src" / "core").string();
		}
	}
	return {};
}

// Shared engine state
namespace
{
	float s_delta = 0;
	float s_elapsed = 0;
	bool s_keys[512]{};
	bool s_pressedLatch[512]{};

	SpriteList  s_pending{};

	// Request flags set by UI callbacks, consumed on the game thread
	std::atomic s_reloadRequested = false;
	std::atomic s_buildAndReloadRequested = false;
	std::atomic s_newProjectRequested = false;
	std::atomic s_loadProjectRequested = false;

	std::string s_pendingProjDir;
	std::string s_pendingProjName;
	std::string s_pendingProjFile;

	// Build state
	// cmake --build runs on a background thread so the window stays responsive.
	std::atomic s_buildInProgress = false;
	std::atomic s_buildDone = false;
	std::atomic s_buildSucceeded = false;

	// New-project configure state
	std::atomic s_configureInProgress = false;
	std::atomic s_configureDone = false;
	std::mutex s_newProjectMutex;
	Project s_newProjectResult;
	bool s_newProjectOk = false;
}

void Engine::Run(const std::string& gameDllPath)
{
	Log("glfwInit");
	if (!glfwInit())
	{
		throw std::runtime_error("GLFW init failed");
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	Log("glfwCreateWindow");
	GLFWwindow* window = glfwCreateWindow(800, 600, "WackyEngine", nullptr, nullptr);
	Log("window created");

	try
	{
		std::unique_ptr<GameDLL> dll;
		if (!gameDllPath.empty())
		{
			dll = std::make_unique<GameDLL>(gameDllPath);
		}

		Project currentProject;
		if (!gameDllPath.empty())
		{
			currentProject.dllPath = gameDllPath;
		}

		const std::string engineCoreDir = FindEngineCoreDir();
		if (engineCoreDir.empty())
		{
			std::cout << "[Engine] Warning: could not locate core/Game.h\n";
		}

		// Renderer
		Log("constructing Renderer...");
		Renderer renderer(window,
			// Reload DLL (no build)
			[] { s_reloadRequested = true; },
			// Build & Reload  ← NEW callback
			[] { s_buildAndReloadRequested = true; },
			// New Project
			[](const std::string& dir, const std::string& name)
			{
				s_pendingProjDir = dir;
				s_pendingProjName = name;
				s_newProjectRequested = true;
			},
			// Load Project
			[](const std::string& path)
			{
				s_pendingProjFile = path;
				s_loadProjectRequested = true;
			}
		);

		// EngineBindings passed into the game DLL
		EngineBindings bindings;
		bindings.getDelta = []() -> float { return s_delta; };
		bindings.getElapsed = []() -> float { return s_elapsed; };
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
				s_pending.Push(s);
			};

		if (dll)
		{
			dll->game->Init(bindings);
		}

		Log("Renderer constructed OK — entering main loop");

		const auto start = std::chrono::steady_clock::now();
		auto prev = start;
		bool prevKeys[512]{};
		int frameCount = 0;

		while (!glfwWindowShouldClose(window))
		{
			glfwPollEvents();

			// Hot-Reload (no build), just swap the DLL on disk
			if (s_reloadRequested.exchange(false) && dll)
			{
				dll->game->Shutdown();
				dll = std::make_unique<GameDLL>(currentProject.dllPath);
				dll->game->Init(bindings);
			}

			// Build & Reload – cmake on a background thread; window stays live.
			if (s_buildAndReloadRequested.exchange(false) && !currentProject.buildCommand.empty() && !s_buildInProgress)
			{
				if (dll)
				{
					dll->game->Shutdown();
					dll.reset();
				}

				s_buildInProgress = true;
				std::cout << "[Engine] Build started: " << currentProject.buildCommand << "\n";

				std::thread([cmd = currentProject.buildCommand]()
					{
						const int rc = std::system(cmd.c_str());
						s_buildSucceeded = (rc == 0);
						s_buildInProgress = false;
						s_buildDone = true;
					}).detach();
			}

			if (s_buildDone.exchange(false))
			{
				if (s_buildSucceeded)
				{
					std::cout << "[Engine] Build succeeded — reloading DLL.\n";
					try
					{
						dll = std::make_unique<GameDLL>(currentProject.dllPath);
						dll->game->Init(bindings);
					}
					catch (const std::exception& e)
					{
						std::cout << "[Engine] DLL load failed: " << e.what() << "\n";
					}
				}
				else
				{
					std::cout << "[Engine] Build FAILED. Fix errors and try again.\n";
				}
			}

			// New Project - cmake configure on a background thread.
			if (s_newProjectRequested.exchange(false) && !s_configureInProgress)
			{
				s_configureInProgress = true;
				std::cout << "[Engine] Configuring project: " << s_pendingProjName << "\n";

				std::thread([dir = s_pendingProjDir,
					name = s_pendingProjName,
					core = engineCoreDir]()
					{
						Project p;
						const bool ok = ProjectManager::Create(dir, name, core, p);
						{
							std::scoped_lock lk(s_newProjectMutex);
							s_newProjectResult = p;
							s_newProjectOk = ok;
						}
						s_configureInProgress = false;
						s_configureDone = true;
					}).detach();
			}

			if (s_configureDone.exchange(false))
			{
				std::scoped_lock lk(s_newProjectMutex);
				if (s_newProjectOk)
				{
					currentProject = s_newProjectResult;
					std::cout << "[Engine] Project created : " << currentProject.directory << "\n";
					std::cout << "[Engine] Build command   : " << currentProject.buildCommand << "\n";
				}
				else
				{
					std::cout << "[Engine] Project creation failed.\n";
				}
			}

			// Load Project
			if (s_loadProjectRequested.exchange(false))
			{
				Project p;
				if (ProjectManager::Load(s_pendingProjFile, p))
				{
					if (dll)
					{
						dll->game->Shutdown();
					}

					dll = std::make_unique<GameDLL>(p.dllPath);
					dll->game->Init(bindings);
					currentProject = p;
				}
			}

			// Timing
			auto now = std::chrono::steady_clock::now();
			s_delta = std::chrono::duration<float>(now - prev).count();
			s_elapsed = std::chrono::duration<float>(now - start).count();
			prev = now;
			if (s_delta > 0.1f)
			{
				s_delta = 0.1f;
			}

			// Key state
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

			// Game update
			s_pending.Clear();
			if (dll)
			{
				dll->game->Update();
			}

			// Smooth FPS
			static float smoothFps = 0.f;
			const float rawFps = s_delta > 0.f ? 1.f / s_delta : 0.f;
			smoothFps = smoothFps * 0.9f + rawFps * 0.1f;

			// Log first few frames so we can see if we ever enter DrawFrame
			if (frameCount < 3)
			{
				char buf[64];
				snprintf(buf, sizeof(buf), "DrawFrame %d", frameCount);
				Log(buf);
			}
			++frameCount;

			const EditorStats stats{ .deltaTime = s_delta, .fps = smoothFps };
			renderer.DrawFrame(s_pending, stats);

			if (frameCount <= 3)
			{
				Log("DrawFrame returned");
			}
		}

		if (dll)
		{
			dll->game->Shutdown();
		}
		renderer.WaitIdle();
	}
	catch (const std::exception& e)
	{
		const std::string msg = std::string("FATAL ERROR:\n") + e.what();
		std::cout << msg << "\n";
		MessageBoxA(nullptr, msg.c_str(), "Fatal Error", MB_OK | MB_ICONERROR);
	}

	glfwDestroyWindow(window);
	glfwTerminate();
}