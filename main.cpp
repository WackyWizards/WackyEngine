#include "Engine.h"

/* Path to the game dll to load */
static const char* gameDllPath = "SpaceShooter.dll";

int main()
{
    Engine engine;
    engine.Run(gameDllPath);
    return 0;
}