#pragma once

#include <string>

struct Project
{
	std::string name;

	/** Absolute path to project folder */
	std::string directory;

	/** Absolute path to built .dll */
	std::string dllPath;

	/** Absolute path to the cmake build directory (where cmake -B points) */
	std::string buildDir;

	/** Absolute path to the starting world for this project */
	std::string startWorld;

	/** Full command used to rebuild the game DLL. */
	std::string buildCommand;

	/** Full command used to export the game. */
	std::string exportCommand;
};