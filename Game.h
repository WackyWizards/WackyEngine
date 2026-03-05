#pragma once

/*Button keys*/
enum class Key : int
{
    Space = 32,
    Num0 = 48,
    Num1 = 49,
    Num2 = 50,
    Num3 = 51,
    Num4 = 52,
    Num5 = 53,
    Num6 = 54,
    Num7 = 55,
    Num8 = 56,
    Num9 = 57,
    A = 65,
    B = 66,
    C = 67,
    D = 68,
    E = 69,
    F = 70,
    G = 71,
    H = 72,
    I = 73,
    J = 74,
    K = 75,
    L = 76,
    M = 77,
    N = 78,
    O = 79,
    P = 80,
    Q = 81,
    R = 82,
    S = 83,
    T = 84,
    U = 85,
    V = 86,
    W = 87,
    X = 88,
    Y = 89,
    Z = 90,
    Escape = 256,
    Enter = 257,
    Tab = 258,
    Backspace = 259,
    Right = 262,
    Left = 263,
    Down = 264,
    Up = 265,
    F1 = 290,
    F2 = 291,
    F3 = 292,
    F4 = 293,
    F5 = 294,
    F6 = 295,
    F7 = 296,
    F8 = 297,
    F9 = 298,
    F10 = 299,
    F11 = 300,
    F12 = 301,
    Shift = 340,
    Ctrl = 341,
    Alt = 342,
};

struct Sprite
{
    /*NDC centre position*/
    float x = 0, y = 0;
    /*Half-extents*/
    float halfW = 0, halfH = 0;
    /*Color*/
    float r = 0, g = 0, b = 0;
};

static constexpr int MAX_SPRITES = 4;

struct SpriteList
{
    Sprite sprites[MAX_SPRITES];
    unsigned count = 0;

    void Clear()
    {
        count = 0;
    }

    bool Push(const Sprite& s)
    {
        if (count >= MAX_SPRITES)
        {
            return false;
        }

        sprites[count++] = s;
        return true;
    }
};

static_assert(sizeof(SpriteList) <= 128, "SpriteList exceeds push-constant budget");

/*
* Engine API
* static inline members are per-DLL, the engine exe and game DLL each get their own copy.
* Binding in the engine would never be seen by the DLL.
* 
* Solution: the engine passes a plain struct of function pointers to Game::Init(). The API classes store them as plain (non-static) members on a singleton that lives inside the game DLL, so there's only one copy.
*/
struct EngineBindings
{
    float (*getdelta)() = nullptr;
    float (*getElapsed)() = nullptr;
    bool (*keyHeld)(Key) = nullptr;
    bool (*keyPressed)(Key) = nullptr;
    void (*pushSprite)(Sprite) = nullptr;
};

// These live in the game DLL, set once in Game::Init, then used by the API classes.
// Defined as non-inline so there is exactly one instance (in the DLL).
extern EngineBindings g_engine;

struct Time
{
    static float Delta()
    {
        return g_engine.getdelta();
    }

    static float Elapsed()
    {
        return g_engine.getElapsed();
    }
};

struct Input
{
    static bool KeyHeld(Key key)
    {
        return g_engine.keyHeld(key);
    }

    static bool KeyPressed(Key key)
    {
        return g_engine.keyPressed(key);
    }
};

struct Scene
{
    static void Push(Sprite s)
    {
        g_engine.pushSprite(s);
    }
};

class Game
{
public:
    virtual ~Game() = default;
    /*Store bindings into g_engine and initialize the game, if needed.*/
    virtual void Init(const EngineBindings& bindings) = 0;
    virtual void Update() = 0;
    virtual void Shutdown() = 0;
};

// DLL entry point
using CreateGameFn = Game * (*)();