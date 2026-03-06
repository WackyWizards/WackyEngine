#include "Engine.h"

namespace
{
    /** Path to the game dll to load */
    auto gameDllPath = "SpaceShooter.dll";
}

int main()
{
    Engine::Run(gameDllPath);
    return 0;
}