#include "ProjectManager.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <windows.h>

namespace fs = std::filesystem;

/**
 * Stub Game.cpp written into every new project's src/ folder.
 * %NAME% is replaced with the actual project name.
 *
 * g_engine is guarded by WACKY_EXPORT: in DLL builds the game binary
 * owns this definition; in export builds RuntimeMain.cpp owns it and
 * the guard prevents a duplicate-symbol linker error.
 */
static const char* STUB_GAME_CPP = R"(#include "Game.h"

#ifndef WACKY_EXPORT
EngineBindings g_engine;
#endif

class %NAME% : public Game
{
public:
    void Init(const EngineBindings& b) override { g_engine = b; }
};

extern "C" __declspec(dllexport) Game* CreateGame()
{
    return new %NAME%();
}
)";

/**
 * CMakeLists.txt written into the root of every new project.
 *
 * Two targets:
 *   %NAME%        — shared library for editor hot-reload (Debug)
 *   %NAME%_Export — standalone executable for distribution (Release)
 *
 * The export target compiles only the runtime + core files needed to run
 * the game. Engine.cpp, Renderer.cpp, EngineUI.cpp, and ImGui are
 * editor-only and must not appear in the export build.
 */
static const char* STUB_CMAKE = R"(cmake_minimum_required(VERSION 3.20)
project(%NAME%)

# ENGINE_CORE_DIR and ENGINE_RUNTIME_DIR are injected at configure-time.

get_filename_component(ENGINE_ROOT "${ENGINE_CORE_DIR}/../.." ABSOLUTE)
message(STATUS "Engine root detected: ${ENGINE_ROOT}")

file(GLOB GLFW_FOLDERS LIST_DIRECTORIES true "${ENGINE_ROOT}/vendor/glfw/*")
if(GLFW_FOLDERS)
    list(GET GLFW_FOLDERS 0 GLFW_VERSION_DIR)
else()
    message(FATAL_ERROR "No GLFW folder found in ${ENGINE_ROOT}/vendor/glfw/")
endif()
message(STATUS "Using GLFW from: ${GLFW_VERSION_DIR}")

find_package(Vulkan REQUIRED)

# ---- Editor hot-reload DLL ----
add_library(%NAME% SHARED
    src/Game.cpp
    "${ENGINE_CORE_DIR}/Object.cpp"
    "${ENGINE_CORE_DIR}/Reflection.cpp"
)

target_include_directories(%NAME% PRIVATE
    "${ENGINE_CORE_DIR}"
)

set_target_properties(%NAME% PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY                "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}"
)

# ---- Standalone export executable ----
# Only runtime + core — no Engine.cpp, Renderer.cpp, EngineUI, or ImGui.
# Spinner.cpp is included so built-in engine entity types are registered
# and can be deserialised from saved worlds.
add_executable(%NAME%_Export
    src/Game.cpp
    "${ENGINE_CORE_DIR}/Object.cpp"
    "${ENGINE_CORE_DIR}/Reflection.cpp"
    "${ENGINE_CORE_DIR}/World.cpp"
    "${ENGINE_ROOT}/src/entities/Spinner.cpp"
    "${ENGINE_RUNTIME_DIR}/RuntimeMain.cpp"
    "${ENGINE_RUNTIME_DIR}/RuntimeRenderer.cpp"
    "${ENGINE_RUNTIME_DIR}/main.cpp"
)

target_include_directories(%NAME%_Export PRIVATE
    "${ENGINE_CORE_DIR}"
    "${ENGINE_RUNTIME_DIR}/.."
    "${GLFW_VERSION_DIR}/include"
)

target_compile_definitions(%NAME%_Export PRIVATE WACKY_EXPORT)

target_link_libraries(%NAME%_Export PRIVATE
    Vulkan::Vulkan
    "${GLFW_VERSION_DIR}/lib-vc2022/glfw3.lib"
)

set_target_properties(%NAME%_Export PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "%NAME%"
    RUNTIME_OUTPUT_DIRECTORY                "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/dist"
)
)";

static std::string JsonStr(const std::string& key, const std::string& value, bool last = false)
{
	std::string escaped;
	escaped.reserve(value.size());
	for (char c : value)
	{
		if (c == '\\')
		{
			escaped += "\\\\";
		}
		else if (c == '"')
		{
			escaped += "\\\"";
		}
		else
		{
			escaped += c;
		}
	}
	return "  \"" + key + "\": \"" + escaped + "\"" + (last ? "" : ",") + "\n";
}

static std::string ReadJsonValue(const std::string& json, const std::string& key)
{
	const std::string search = "\"" + key + "\"";
	size_t keyPos = json.find(search);
	if (keyPos == std::string::npos)
	{
		return {};
	}

	size_t colon = json.find(':', keyPos);
	if (colon == std::string::npos)
	{
		return {};
	}

	size_t open = json.find('"', colon);
	if (open == std::string::npos)
	{
		return {};
	}

	std::string result;
	for (size_t i = open + 1; i < json.size(); ++i)
	{
		if (json[i] == '\\' && i + 1 < json.size())
		{
			++i;
			if (json[i] == '"')
			{
				result += '"';
			}
			else if (json[i] == '\\')
			{
				result += '\\';
			}
			else
			{
				result += json[i];
			}
		}
		else if (json[i] == '"')
		{
			break;
		}
		else
		{
			result += json[i];
		}
	}

	return result;
}

namespace ProjectManager
{
	bool Create(const std::string& parentDir, const std::string& name, const std::string& engineCoreDir, Project& out)
	{
		try
		{
			std::string cleanName = name;
			cleanName.erase(
				std::remove_if(cleanName.begin(), cleanName.end(),
					[](char c)
					{
						return c == '\r' || c == '\n' || c == '\t';
					}),
				cleanName.end());

			fs::path dir = fs::path(parentDir) / cleanName;
			fs::path buildDir = dir / "build";
			fs::create_directories(dir / "src");

			std::string stub = STUB_GAME_CPP;
			for (size_t p; (p = stub.find("%NAME%")) != std::string::npos;)
			{
				stub.replace(p, 6, cleanName);
			}
			std::ofstream(dir / "src" / "Game.cpp") << stub;

			std::string cmake = STUB_CMAKE;
			for (size_t p; (p = cmake.find("%NAME%")) != std::string::npos;)
			{
				cmake.replace(p, 6, cleanName);
			}
			std::ofstream(dir / "CMakeLists.txt") << cmake;

			auto fwd = [](fs::path p)
				{
					return p.generic_string();
				};

			const std::string coreDirFwd = fwd(fs::path(engineCoreDir));
			const std::string runtimeDirFwd = fwd(fs::path(engineCoreDir).parent_path() / "runtime");

			const std::string configureCmd =
				"cmake -S \"" + fwd(dir) + "\""
				" -B \"" + fwd(buildDir) + "\""
				" -DENGINE_CORE_DIR=\"" + coreDirFwd + "\""
				" -DENGINE_RUNTIME_DIR=\"" + runtimeDirFwd + "\"";

			if (std::system(configureCmd.c_str()) != 0)
			{
				return false;
			}

			out.name = cleanName;
			out.directory = dir.string();
			out.dllPath = (dir / (cleanName + ".dll")).string();
			out.buildDir = buildDir.string();
			out.buildCommand =
				"cmake --build \"" + fwd(buildDir) + "\""
				" --config Debug"
				" --target " + cleanName;
			out.exportCommand =
				"cmake --build \"" + fwd(buildDir) + "\""
				" --config Release"
				" --target " + cleanName + "_Export";

			const fs::path worldPath = dir / "DefaultWorld.json";
			{
				std::ofstream wf(worldPath);
				wf << "{\n"
					<< "  \"name\": \"Default World\",\n"
					<< "  \"entities\": []\n"
					<< "}\n";
			}
			out.startWorld = worldPath.string();

			return Save(out);
		}
		catch (...)
		{
			return false;
		}
	}

	bool Load(const std::string& projFile, Project& out)
	{
		std::ifstream f(projFile);
		if (!f)
		{
			return false;
		}

		std::ostringstream ss;
		ss << f.rdbuf();
		const std::string json = ss.str();

		out.name = ReadJsonValue(json, "name");
		out.directory = ReadJsonValue(json, "directory");
		out.dllPath = ReadJsonValue(json, "dll");
		out.buildDir = ReadJsonValue(json, "buildDir");
		out.buildCommand = ReadJsonValue(json, "buildCommand");
		out.exportCommand = ReadJsonValue(json, "exportCommand");
		out.startWorld = ReadJsonValue(json, "startWorld");

		return !out.name.empty();
	}

	bool Save(const Project& project)
	{
		std::ofstream f(fs::path(project.directory) / (project.name + ".json"));
		if (!f)
		{
			return false;
		}

		f << "{\n"
			<< JsonStr("name", project.name)
			<< JsonStr("directory", project.directory)
			<< JsonStr("dll", project.dllPath)
			<< JsonStr("buildDir", project.buildDir)
			<< JsonStr("buildCommand", project.buildCommand)
			<< JsonStr("exportCommand", project.exportCommand)
			<< JsonStr("startWorld", project.startWorld, true)
			<< "}\n";

		return true;
	}

	bool Export(const Project& project)
	{
		if (project.exportCommand.empty())
		{
			std::cout << "[Engine] No export command configured for this project.\n";
			return false;
		}

		std::cout << "[Engine] Export started: " << project.exportCommand << "\n";
		const int rc = std::system(project.exportCommand.c_str());

		if (rc != 0)
		{
			std::cout << "[Engine] Export FAILED.\n";
			return false;
		}

		try
		{
			const fs::path distDir = fs::path(project.directory) / "dist";
			const fs::path worldSrc = fs::path(project.startWorld);

			if (!project.startWorld.empty() && fs::exists(worldSrc))
			{
				fs::copy_file(worldSrc, distDir / worldSrc.filename(),
					fs::copy_options::overwrite_existing);
			}

			for (const auto& entry : fs::directory_iterator(project.directory))
			{
				if (entry.path().extension() == ".json" &&
					entry.path().parent_path() == fs::path(project.directory))
				{
					fs::copy_file(entry.path(), distDir / entry.path().filename(),
						fs::copy_options::overwrite_existing);
				}
			}

			/**
			 * Copy compiled shaders from the engine exe's directory into dist/.
			 * RuntimeRenderer looks for vert.spv and frag.spv relative to the
			 * working directory, so they must sit next to the exported exe.
			 */
			char exeBuf[MAX_PATH];
			GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
			const fs::path exeDir = fs::path(exeBuf).parent_path();

			for (const char* spv : { "vert.spv", "frag.spv" })
			{
				const fs::path src = exeDir / spv;
				if (fs::exists(src))
				{
					fs::copy_file(src, distDir / spv, fs::copy_options::overwrite_existing);
				}
				else
				{
					std::cout << "[Engine] Warning: shader not found: " << src.string() << "\n";
				}
			}

			std::cout << "[Engine] Exported to: " << distDir.string() << "\n";
		}
		catch (const std::exception& e)
		{
			std::cout << "[Engine] Warning: asset copy failed: " << e.what() << "\n";
		}

		return true;
	}
}