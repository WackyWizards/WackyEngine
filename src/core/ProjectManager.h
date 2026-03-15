#pragma once

#include "Project.h"

namespace ProjectManager
{
	/**
	 * Create folder structure, write CMakeLists.txt, generate stub Game.cpp,
	 * run the initial cmake configure step, and write the .json project file.
	 *
	 * @param parentDir     Parent directory under which the project folder is created.
	 * @param name          Project (and DLL) name.
	 * @param engineCoreDir Absolute path to the engine's core/ directory (where Game.h lives).
	 *                      Passed to cmake as -DENGINE_CORE_DIR so the game can #include "Game.h".
	 * @param out           Populated on success.
	 */
	bool Create(const std::string& parentDir, const std::string& name, const std::string& engineCoreDir, Project& out);

	/** Load a .json project file written by Save(). */
	bool Load(const std::string& projFile, Project& out);

	bool Save(const Project& project);

	bool Export(const Project& project);
}