#define GLFW_INCLUDE_VULKAN
#include "Engine.h"
#include "World.h"
#include "ProjectManager.h"
#include "editor/EditorRenderer.h"
#include <iostream>
#include <fstream>
#include <mutex>
#include <windows.h>
#include <filesystem>

// Include all built-in entities so they get registered.
#include "entities/Spinner.h"

EngineBindings g_engine;

/** Diagnostic log - defined at file scope so Renderer.cpp can reach it via extern. */
std::ofstream g_log;

namespace
{
	void Log(const char* msg)
	{
		if (!g_log.is_open())
		{
			g_log.open("engine_log.txt", std::ios::out | std::ios::trunc);
		}
		g_log << msg << "\n";
		g_log.flush();
	}

	Renderer* s_renderer = nullptr;
}

namespace fs = std::filesystem;

/**
 * GameDLL - loads the game DLL by first copying both the DLL and its PDB
 * to "_live" siblings, then patching the embedded PDB path inside the copied
 * DLL so the debugger opens _live.pdb instead of the original .pdb.
 *
 * Result: the linker always writes to the now never locked .dll + .pdb
 * while the engine + debugger hold handles only on the _live copies.
 */
struct GameDLL
{
	HMODULE handle = nullptr;
	Game* game = nullptr;
	std::string liveDllPath;
	std::string livePdbPath;

	explicit GameDLL(const std::string& path)
	{
		liveDllPath = MakeLivePath(path, ".dll");
		livePdbPath = MakeLivePath(path, ".pdb");

		if (!fs::copy_file(path, liveDllPath, fs::copy_options::overwrite_existing))
		{
			throw std::runtime_error("Failed to copy game DLL to live path: " + liveDllPath);
		}

		/**
		 * If a PDB exists alongside the DLL, copy it and patch the embedded
		 * debug path in the live DLL so the debugger follows the live copy.
		 * If either step fails we carry on - debug symbols simply won't load,
		 * but the hot-reload itself still works.
		 */
		const fs::path srcPdb = fs::path(path).replace_extension(".pdb");
		if (fs::exists(srcPdb))
		{
			std::error_code ec;
			fs::copy_file(srcPdb, livePdbPath, fs::copy_options::overwrite_existing, ec);
			if (!ec)
			{
				PatchEmbeddedPdbPath(liveDllPath, fs::path(livePdbPath).filename().string());
			}
		}

		handle = LoadLibraryA(liveDllPath.c_str());
		if (!handle)
		{
			throw std::runtime_error("Failed to load game DLL: " + liveDllPath);
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

		std::error_code ec;
		if (!liveDllPath.empty())
		{
			fs::remove(liveDllPath, ec);
		}
		if (!livePdbPath.empty())
		{
			fs::remove(livePdbPath, ec);
		}
	}

	GameDLL(const GameDLL&) = delete;
	GameDLL& operator=(const GameDLL&) = delete;
	GameDLL(GameDLL&&) = delete;
	GameDLL& operator=(GameDLL&&) = delete;

private:

	/**
	 * Derives a "_live" sibling path with the given extension.
	 * e.g. C:/Projects/GG/GG.dll + ".dll" -> C:/Projects/GG/GG_live.dll
	 */
	static std::string MakeLivePath(const std::string& original, const std::string& ext)
	{
		const fs::path p(original);
		return (p.parent_path() / (p.stem().string() + "_live" + ext)).string();
	}

	/**
	 * Patches the CodeView PDB path embedded in a PE/COFF binary.
	 *
	 * The linker bakes an absolute PDB path into the DLL's debug directory
	 * (IMAGE_DEBUG_TYPE_CODEVIEW / RSDS record). When the debugger loads the
	 * DLL it opens that path, locking the original PDB. By overwriting the
	 * path with the shorter live-copy filename the debugger is redirected to
	 * the _live PDB instead, leaving the originals free for the linker.
	 *
	 * newPdbName must be strictly shorter than the current embedded path
	 * (a bare filename like "_live.pdb" is always shorter than the original
	 * absolute path). Excess bytes are zeroed.
	 * 
	 * @returns true on success; on any failure the DLL is left unmodified.
	 */
	static bool PatchEmbeddedPdbPath(const std::string& dllPath, const std::string& newPdbName)
	{
		std::fstream file(dllPath, std::ios::binary | std::ios::in | std::ios::out);
		if (!file)
		{
			return false;
		}

		IMAGE_DOS_HEADER dos{};
		file.read(reinterpret_cast<char*>(&dos), sizeof(dos));
		if (dos.e_magic != IMAGE_DOS_SIGNATURE)
		{
			return false;
		}

		file.seekg(dos.e_lfanew);
		DWORD sig = 0;
		file.read(reinterpret_cast<char*>(&sig), sizeof(sig));
		if (sig != IMAGE_NT_SIGNATURE)
		{
			return false;
		}

		IMAGE_FILE_HEADER fileHdr{};
		file.read(reinterpret_cast<char*>(&fileHdr), sizeof(fileHdr));

		/** Peek at the optional-header magic to distinguish PE32 from PE32+. */
		WORD magic = 0;
		file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
		file.seekg(-static_cast<int>(sizeof(magic)), std::ios::cur);

		DWORD debugRVA = 0;
		DWORD debugSize = 0;

		if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
		{
			IMAGE_OPTIONAL_HEADER64 opt{};
			file.read(reinterpret_cast<char*>(&opt), sizeof(opt));
			debugRVA = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress;
			debugSize = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size;
		}
		else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
		{
			IMAGE_OPTIONAL_HEADER32 opt{};
			file.read(reinterpret_cast<char*>(&opt), sizeof(opt));
			debugRVA = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress;
			debugSize = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size;
		}
		else
		{
			return false;
		}

		if (!debugRVA || !debugSize)
		{
			return false;
		}

		std::vector<IMAGE_SECTION_HEADER> sections(fileHdr.NumberOfSections);
		for (auto& s : sections)
		{
			file.read(reinterpret_cast<char*>(&s), sizeof(s));
		}

		/**
		 * Converts a relative virtual address to a raw file offset using the
		 * section table read above.
		 */
		const auto RvaToOffset = [&](const DWORD rva) -> DWORD
			{
				for (const auto& s : sections)
				{
					if (rva >= s.VirtualAddress && rva < s.VirtualAddress + s.SizeOfRawData)
					{
						return rva - s.VirtualAddress + s.PointerToRawData;
					}
				}
				return 0;
			};

		const DWORD debugOffset = RvaToOffset(debugRVA);
		if (!debugOffset)
		{
			return false;
		}

		const DWORD entryCount = debugSize / sizeof(IMAGE_DEBUG_DIRECTORY);
		for (DWORD i = 0; i < entryCount; ++i)
		{
			file.seekg(debugOffset + i * sizeof(IMAGE_DEBUG_DIRECTORY));

			IMAGE_DEBUG_DIRECTORY entry{};
			file.read(reinterpret_cast<char*>(&entry), sizeof(entry));

			if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW)
			{
				continue;
			}

			file.seekg(entry.PointerToRawData);

			/** RSDS signature marks the start of a CodeView PDB70 record. */
			DWORD cvSig = 0;
			file.read(reinterpret_cast<char*>(&cvSig), sizeof(cvSig));
			if (cvSig != 0x53445352) // 'RSDS'
			{
				continue;
			}

			/** Skip GUID (16 bytes) + Age (4 bytes) to reach the path string. */
			file.seekg(20, std::ios::cur);

			const std::streampos pathStart = file.tellg();

			std::string currentPath;
			char c = '\0';
			while (file.get(c) && c != '\0')
			{
				currentPath += c;
			}

			/** newPdbName + null terminator must fit inside the existing space. */
			if (newPdbName.size() >= currentPath.size())
			{
				return false;
			}

			file.seekp(pathStart);
			file.write(newPdbName.c_str(), static_cast<std::streamsize>(newPdbName.size() + 1));

			const std::string padding(currentPath.size() - newPdbName.size(), '\0');
			file.write(padding.c_str(), static_cast<std::streamsize>(padding.size()));

			return true;
		}

		return false;
	}
};

/**
 * Walk up from the engine executable until we find a directory that contains
 * core/Game.h. Works for both in-source and out-of-source builds.
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

/** Shared engine state */
namespace
{
	float s_delta = 0;
	float s_elapsed = 0;
	bool s_keys[512]{};
	bool s_pressedLatch[512]{};

	SpriteList s_pending{};

	/** Request flags set by UI callbacks, consumed on the game thread */
	std::atomic s_reloadRequested = false;
	std::atomic s_buildAndReloadRequested = false;
	std::atomic s_newProjectRequested = false;
	std::atomic s_loadProjectRequested = false;

	std::string s_pendingProjDir;
	std::string s_pendingProjName;
	std::string s_pendingProjFile;

	/**
	 * Build state.
	 * cmake --build runs on a background thread so the window stays responsive.
	 */
	std::atomic s_buildInProgress = false;
	std::atomic s_buildDone = false;
	std::atomic s_buildSucceeded = false;

	/** New-project configure state */
	std::atomic s_configureInProgress = false;
	std::atomic s_configureDone = false;
	std::mutex s_newProjectMutex;
	Project s_newProjectResult;
	bool s_newProjectOk = false;

	/** Play-mode state (consumed on game thread, set by Renderer passthrough each frame) */
	auto s_enginePlayState = PlayState::Editing;
	std::string s_worldSnapshot;

	/** Scaled delta exposed to DLL; 0 when paused/editing. */
	float s_scaledDelta = 0.f;
	/** Fixed step size exposed via getFixedDelta(); updated from world.fixedStep each frame. */
	float s_fixedDelta = 0.02f;
	/** Fixed-update accumulator - carries leftover time across frames. */
	float s_fixedAccum = 0.f;
	/** Step-frame: advance exactly one fixed tick next frame even while Paused. */
	bool s_stepFrame = false;
	/** How many fixed ticks fired last frame */
	int s_fixedTicksLastFrame = 0;

	/**
	 * World snapshot taken just before a Build & Reload unloads the DLL.
	 * Restored after a successful build using the new DLL's factories so that
	 * game-type entities are reconstructed with valid vtable pointers.
	 */
	std::string s_buildSnapshot;
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
	Log("Window created");

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

		World currentWorld;
		std::vector<EntityTypeInfo> currentEntityTypes;

		/**
		 * Builds an EntityResolver from the currently loaded entity types.
		 * Passed to World::LoadFromJson so derived types are reconstructed correctly.
		 */
		const auto MakeResolver = [&currentEntityTypes]() -> EntityResolver
			{
				return [&currentEntityTypes](const char* typeName) -> Entity*
					{
						for (const auto& info : EntityRegistry::Get().GetAll())
						{
							if (info.typeName && strcmp(info.typeName, typeName) == 0 && info.factory)
							{
								return info.factory();
							}
						}

						for (const auto& info : currentEntityTypes)
						{
							if (info.typeName && strcmp(info.typeName, typeName) == 0 && info.factory)
							{
								return info.factory();
							}
						}
						return nullptr;
					};
			};

		const std::string engineCoreDir = FindEngineCoreDir();
		if (engineCoreDir.empty())
		{
			std::cout << "[Engine] Warning: could not locate core/Game.h\n";
		}

		Log("Constructing Renderer...");
		Renderer renderer(window,
			[] { s_reloadRequested = true; },
			[] { s_buildAndReloadRequested = true; },
			[&currentProject] {
				std::cout << "[Engine] Exporting\n";
				ProjectManager::Export(currentProject);
			},
			[](const std::string& dir, const std::string& name)
			{
				s_pendingProjDir = dir;
				s_pendingProjName = name;
				s_newProjectRequested = true;
			},
			[](const std::string& path)
			{
				s_pendingProjFile = path;
				s_loadProjectRequested = true;
			},
			[&currentEntityTypes]() { return currentEntityTypes; }
		);
		s_renderer = &renderer;

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
		bindings.loadTexture = [](const char* path) -> Texture* {
			try {
				return new Texture(s_renderer->LoadTexture(path));
			}
			catch (const std::exception& e) {
				std::cout << "[Engine] LoadTexture failed: " << e.what() << "\n";
				return nullptr;
			}
			};

		bindings.bindTexture = [](const Texture* tex)
			{
				s_renderer->BindTexture(*tex);
			};

		bindings.destroyTexture = [](Texture* tex)
			{
				s_renderer->DestroyTexture(*tex);
				delete tex;
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
		bindings.pushSprite = [](Sprite s) { s_pending.Push(s); };

		g_engine = bindings;

		if (dll)
		{
			dll->game->Init(bindings);
			currentEntityTypes = dll->game->GetEntityTypes();
			RefreshEntities(*dll);
		}

		Log("Renderer constructed OK - entering main loop");

		const auto start = std::chrono::steady_clock::now();
		auto prev = start;
		bool prevKeys[512]{};

		while (!glfwWindowShouldClose(window))
		{
			glfwPollEvents();

			/** Hot-Reload (no build) - snapshot, swap DLL, restore world */
			if (s_reloadRequested.exchange(false) && dll)
			{
				const std::string snapshot = currentWorld.SerializeToString();

				dll->game->Shutdown();
				dll = std::make_unique<GameDLL>(currentProject.dllPath);
				dll->game->Init(bindings);
				currentEntityTypes = dll->game->GetEntityTypes();
				RefreshEntities(*dll);

				currentWorld.DeserializeFromString(snapshot, MakeResolver());
			}

			/**
			 * Build & Reload - cmake on a background thread so the window stays live.
			 *
			 * The world is snapshotted and cleared BEFORE the DLL is unloaded.
			 * Every game-type entity (TestEntity etc.) holds a vtable pointer into
			 * the DLL. If the world is not cleared first, the hierarchy panel will
			 * call GetTypeName() on those entities the very next frame, dereference
			 * a dangling vtable, and crash with a read access violation.
			 * The snapshot is restored after a successful build using the new
			 * DLL's factories so game-type entities get fresh, valid vtables.
			 */
			if (s_buildAndReloadRequested.exchange(false) && !currentProject.buildCommand.empty() && !s_buildInProgress)
			{
				s_buildSnapshot = currentWorld.SerializeToString();
				currentWorld.Clear();
				renderer.NotifyWorldRestored();

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
					std::cout << "[Engine] Build succeeded - reloading DLL.\n";
					try
					{
						dll = std::make_unique<GameDLL>(currentProject.dllPath);
						dll->game->Init(bindings);
						currentEntityTypes = dll->game->GetEntityTypes();
						RefreshEntities(*dll);

						/**
						 * Restore the world using the newly loaded DLL's factories
						 * so game-type entities are reconstructed with valid vtables.
						 */
						if (!s_buildSnapshot.empty())
						{
							currentWorld.DeserializeFromString(s_buildSnapshot, MakeResolver());
							s_buildSnapshot.clear();
						}
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

			/** New Project - cmake configure on a background thread */
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

					if (!currentProject.startWorld.empty())
					{
						currentWorld.LoadFromJson(currentProject.startWorld, MakeResolver());
						std::cout << "[Engine] Loaded start world: " << currentProject.startWorld << "\n";
					}
				}
				else
				{
					std::cout << "[Engine] Project creation failed.\n";
				}
			}

			/** Load Project */
			if (s_loadProjectRequested.exchange(false))
			{
				Project p;
				if (ProjectManager::Load(s_pendingProjFile, p))
				{
					if (dll)
					{
						dll->game->Shutdown();
					}
					dll.reset();
					currentEntityTypes.clear();

					if (!p.dllPath.empty() && fs::exists(p.dllPath))
					{
						try
						{
							dll = std::make_unique<GameDLL>(p.dllPath);
							dll->game->Init(bindings);
							currentEntityTypes = dll->game->GetEntityTypes();
							RefreshEntities(*dll);
							std::cout << "[Engine] Loaded DLL: " << p.dllPath << "\n";
						}
						catch (const std::exception& ex)
						{
							std::cout << "[Engine] DLL load failed: " << ex.what()
								<< "\n         (Build the project first if this is new)\n";
						}
					}
					else if (!p.dllPath.empty())
					{
						std::cout << "[Engine] DLL not found: " << p.dllPath
							<< "\n         Build the project (Build & Reload) to create it.\n";
					}

					currentProject = p;

					if (!currentProject.startWorld.empty() && fs::exists(currentProject.startWorld))
					{
						currentWorld.LoadFromJson(currentProject.startWorld, MakeResolver());
						std::cout << "[Engine] Loaded start world: " << currentProject.startWorld << "\n";
					}
					else if (!currentProject.startWorld.empty())
					{
						std::cout << "[Engine] Start world not found: " << currentProject.startWorld << "\n";
					}
				}
				else
				{
					std::cout << "[Engine] Failed to load project file: " << s_pendingProjFile << "\n";
				}
			}

			/** Timing */
			auto now = std::chrono::steady_clock::now();
			s_delta = std::chrono::duration<float>(now - prev).count();
			s_elapsed = std::chrono::duration<float>(now - start).count();
			prev = now;
			if (s_delta > 0.1f)
			{
				s_delta = 0.1f;
			}

			/** Play-mode state machine */
			{
				const PlayState newState = renderer.GetPlayState();
				const float timescale = renderer.GetTimescale();

				currentWorld.fixedStep = renderer.GetFixedStep();
				s_fixedDelta = currentWorld.fixedStep * timescale;

				if (newState != s_enginePlayState)
				{
					if (newState == PlayState::Playing && s_enginePlayState == PlayState::Editing)
					{
						s_worldSnapshot = currentWorld.SerializeToString();
						s_fixedAccum = 0.f;
						if (dll)
						{
							dll->game->Shutdown();
							dll->game->Init(bindings);
						}
						currentWorld.BeginPlay();
						std::cout << "[Engine] Entered play mode.\n";
					}

					if (newState == PlayState::Editing && (s_enginePlayState == PlayState::Playing || s_enginePlayState == PlayState::Paused))
					{
						currentWorld.EndPlay();
						if (!s_worldSnapshot.empty())
						{
							currentWorld.DeserializeFromString(s_worldSnapshot, MakeResolver());
							s_worldSnapshot.clear();
						}
						s_fixedAccum = 0.f;
						renderer.NotifyWorldRestored();
						if (dll)
						{
							dll->game->Shutdown();
						}
						std::cout << "[Engine] Stopped - world restored.\n";
					}

					s_enginePlayState = newState;
				}

				s_scaledDelta = s_enginePlayState == PlayState::Playing ? s_delta * timescale : 0.f;
				s_stepFrame = s_enginePlayState == PlayState::Paused && renderer.ConsumeStepFrame();
			}

			/** Key state */
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

			/** Fixed update loop */
			s_fixedTicksLastFrame = 0;
			if (s_enginePlayState == PlayState::Playing || s_stepFrame)
			{
				const float step = currentWorld.fixedStep;
				if (step > 0.f)
				{
					if (s_stepFrame)
					{
						currentWorld.FixedUpdate();
						if (dll)
						{
							dll->game->OnFixedUpdate();
						}
						s_fixedTicksLastFrame = 1;
					}
					else
					{
						s_fixedAccum += s_scaledDelta;

						/**
						 * Safety cap: at most 8 ticks per frame so a spiral-of-death
						 * can't freeze the window if the game hangs for a frame.
						 */
						constexpr int MAX_FIXED_TICKS = 8;
						int ticks = 0;
						while (s_fixedAccum >= step && ticks < MAX_FIXED_TICKS)
						{
							currentWorld.FixedUpdate();
							if (dll)
							{
								dll->game->OnFixedUpdate();
							}
							s_fixedAccum -= step;
							++ticks;
						}
						s_fixedTicksLastFrame = ticks;
					}
				}
			}

			/** Variable update (Playing only) */
			s_pending.Clear();
			if (s_enginePlayState == PlayState::Playing)
			{
				currentWorld.Update();
				if (dll)
				{
					dll->game->OnUpdate();
				}
			}

			/**
			 * Pending world load from the "Open World" dialog - handled here so
			 * we can inject the resolver with the currently loaded entity types.
			 */
			if (renderer.HasPendingWorldLoad())
			{
				currentWorld.LoadFromJson(renderer.GetPendingWorldLoadPath(), MakeResolver());
				renderer.ClearPendingWorldLoad();
			}

			/** Smooth FPS */
			static float smoothFps = 0.f;
			const float rawFps = s_delta > 0.f ? 1.f / s_delta : 0.f;
			smoothFps = smoothFps * 0.9f + rawFps * 0.1f;

			const EditorStats stats{
				.deltaTime = s_delta,
				.fps = smoothFps,
				.timescale = renderer.GetTimescale(),
				.fixedStep = currentWorld.fixedStep,
				.fixedTicks = s_fixedTicksLastFrame
			};
			renderer.DrawFrame(s_pending, stats, currentWorld);
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

void Engine::RefreshEntities(const GameDLL& dll)
{
	Reflection::ClearAll();
	for (auto& [className, field] : dll.game->GetReflectedFields())
	{
		Reflection::RegisterField(className, field);
	}
}