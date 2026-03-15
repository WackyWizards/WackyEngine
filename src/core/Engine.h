#pragma once

#include <string>

struct GameDLL;

class Engine
{
public:
	static void Run(const std::string& gameDllPath);

private:
	/** Refreshes entities stored in reflection. */
	static void RefreshEntities(const GameDLL& dll);
};