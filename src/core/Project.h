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

    /**
     * Full command used to rebuild the game DLL.
     * e.g. cmake --build "C:/Projects/MyGame/build" --config Debug --target MyGame
     * Populated by ProjectManager::Create and persisted in the .json file.
     */
    std::string buildCommand;
};