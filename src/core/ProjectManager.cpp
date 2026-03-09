#include "ProjectManager.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace fs = std::filesystem;

// Stub Game.cpp written into every new project's src/ folder.
// %NAME% is replaced with the actual project name.
static const char* STUB_GAME_CPP = R"(#include "Game.h"

EngineBindings g_engine;

class %NAME% : public Game
{
public:
    void Init(const EngineBindings& b) override { g_engine = b; }
    void Update() override { }
    void Shutdown() override { }
};

extern "C" __declspec(dllexport) Game* CreateGame()
{
    return new %NAME%();
}
)";

// CMakeLists.txt template written into the root of every new project.
//
// ENGINE_CORE_DIR is injected by the engine at configure-time via
//   cmake -S <proj> -B <build> -DENGINE_CORE_DIR=<path>
// so the game DLL can always #include "Game.h" without hard-coding anything.
static const char* STUB_CMAKE = R"(cmake_minimum_required(VERSION 3.20)
project(%NAME%)

add_library(%NAME% SHARED src/Game.cpp)

# ENGINE_CORE_DIR is set by the engine when it runs cmake configure.
target_include_directories(%NAME% PRIVATE "${ENGINE_CORE_DIR}")

# Put the .dll directly in the project root so the engine can find it at
# the path stored in the .json file.
set_target_properties(%NAME% PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY           "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG     "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE   "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}"
)
)";

// JSON helpers
static std::string JsonStr(const std::string& key, const std::string& value, bool last = false)
{
	// Escape backslashes so Windows paths survive the round-trip.
	std::string escaped;
	escaped.reserve(value.size());
	for (char c : value)
	{
		if (c == '\\')
		{
			escaped += "\\\\";
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

	// Collect characters, handling \" and \\ escapes
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
			fs::path dir = fs::path(parentDir) / name;
			fs::path buildDir = dir / "build";
			fs::create_directories(dir / "src");

			// Game.cpp stub
			std::string stub = STUB_GAME_CPP;
			for (size_t p; (p = stub.find("%NAME%")) != std::string::npos;)
			{
				stub.replace(p, 6, name);
			}

			std::ofstream(dir / "src" / "Game.cpp") << stub;

			// CMakeLists.txt
			std::string cmake = STUB_CMAKE;
			for (size_t p; (p = cmake.find("%NAME%")) != std::string::npos;)
			{
				cmake.replace(p, 6, name);
			}

			std::ofstream(dir / "CMakeLists.txt") << cmake;

			// cmake configure (runs once to generate build system)
			// Forward-slashes keep cmake happy on Windows.
			// engineCoreDir is a raw std::string from FindEngineCoreDir() which
			// uses native backslashes — convert it through fs::path so cmake
			// doesn't misinterpret the backslashes as escape characters.
			auto fwd = [](fs::path p)
			{
				return p.generic_string();
			};

			const std::string coreDirFwd = fwd(fs::path(engineCoreDir));

			const std::string configureCmd =
				"cmake -S \"" + fwd(dir) + "\""
				" -B \"" + fwd(buildDir) + "\""
				" -DENGINE_CORE_DIR=\"" + coreDirFwd + "\"";

			if (std::system(configureCmd.c_str()) != 0)
			{
				return false;   // cmake configure failed
			}

			// Populate Project
			out.name = name;
			out.directory = dir.string();
			out.dllPath = (dir / (name + ".dll")).string();
			out.buildDir = buildDir.string();
			out.buildCommand =
				"cmake --build \"" + fwd(buildDir) + "\""
				" --config Debug"
				" --target " + name;

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
			<< JsonStr("buildCommand", project.buildCommand, true)
			<< "}\n";

		return true;
	}
}