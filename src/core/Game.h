#pragma once

#include "Entity.h"
#include "EntityRegistry.h"
#include "Reflection.h"
#include "renderer/Texture.h"
#include <vector>

/** Button keys */
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
	/** NDC centre position */
	float x = 0, y = 0;
	/** Half-extents */
	float halfW = 0, halfH = 0;
	/** Color tint (multiplied with the texture sample, white = no tint) */
	float r = 1, g = 1, b = 1;
	/** UV rect into the texture atlas (normalized 0-1) */
	float uvX = 0, uvY = 0, uvW = 1, uvH = 1;
	/** 1 = sample bound texture, 0 = use solid color */
	float textured = 0;
};

// Hard limit is the device's maxStorageBufferRange
// (guaranteed >= 128 MB on any Vulkan 1.0+ implementation).
static constexpr int MAX_SPRITES = 1024;

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

/**
 * Engine API
 * static inline members are per-DLL, the engine exe and game DLL each get their own copy.
 * Binding in the engine would never be seen by the DLL.
 *
 * Solution: the engine passes a plain struct of function pointers to Game::Init(). The API
 * classes store them as plain (non-static) members on a singleton that lives inside the
 * game DLL, so there's only one copy.
 */
struct EngineBindings
{
	/** Timescale-scaled frame delta (0 when paused) */
	float (*getDelta)() = nullptr;
	/** Real seconds since Play started */
	float (*getElapsed)() = nullptr;
	/** Fixed step size in seconds (e.g. 0.02) */
	float (*getFixedDelta)() = nullptr;
	bool (*keyHeld)(Key) = nullptr;
	bool (*keyPressed)(Key) = nullptr;
	void (*pushSprite)(Sprite) = nullptr;

	/**
	 * Loads a PNG from disk and uploads it to the GPU.
	 * The returned pointer is owned by the engine and remains valid until
	 * destroyTexture is called with it.
	 */
	Texture* (*loadTexture)(const char* path) = nullptr;

	/**
	 * Updates the pipeline's combined-image-sampler to point at tex.
	 * Call once after loading, and again whenever you switch textures.
	 * All textured sprites pushed this frame will use the last bound texture.
	 */
	void (*bindTexture)(const Texture* tex) = nullptr;

	/**
	 * Releases the GPU resources for tex and frees the pointer.
	 * Do not use tex after this call.
	 */
	void (*destroyTexture)(Texture* tex) = nullptr;
};

// These live in the game DLL, set once in Game::Init, then used by the API classes.
// Defined as non-inline so there is exactly one instance (in the DLL).
extern EngineBindings g_engine;

struct Time
{
	/** Timescale-scaled seconds since last frame. Zero while Paused/Editing. */
	static float Delta()
	{
		return g_engine.getDelta();
	}

	/**
	 * The fixed timestep size in seconds (e.g. 0.02 for 50 Hz).
	 * Use this inside FixedUpdate() if you need the step size explicitly.
	 * Already scaled by timescale - the same value passed to FixedUpdate(fdt).
	 */
	static float FixedDelta()
	{
		return g_engine.getFixedDelta();
	}

	/** Real seconds since Play mode started (unaffected by timescale). */
	static float Elapsed()
	{
		return g_engine.getElapsed();
	}
};

struct Input
{
	static bool KeyHeld(const Key key)
	{
		return g_engine.keyHeld(key);
	}

	static bool KeyPressed(const Key key)
	{
		return g_engine.keyPressed(key);
	}
};

struct Scene
{
	static void Push(const Sprite& s)
	{
		g_engine.pushSprite(s);
	}

	/**
	 * Pushes a textured quad using the currently bound texture.
	 * uvX/uvY/uvW/uvH address a sub-rect of the texture in normalized 0-1 space,
	 * which lets you pick individual frames out of a sprite sheet.
	 * Defaults cover the entire texture (one frame).
	 */
	static void PushTextured(const float x, const float y, const float halfW, const float halfH, const float uvX = 0.f, const float uvY = 0.f, const float uvW = 1.f, const float uvH = 1.f)
	{
		Sprite s;
		s.x = x;
		s.y = y;
		s.halfW = halfW;
		s.halfH = halfH;
		s.uvX = uvX;
		s.uvY = uvY;
		s.uvW = uvW;
		s.uvH = uvH;
		s.textured = 1.f;
		g_engine.pushSprite(s);
	}
};

class Game
{
public:
	virtual ~Game() = default;

	virtual void Init(const EngineBindings& bindings) = 0;

	/**
	 * Called at a fixed rate (default 50 Hz) while Playing.
	 * Override for physics, movement integration, and deterministic logic.
	 * The engine calls this zero or more times per rendered frame to keep up.
	 */
	virtual void OnFixedUpdate() {}

	/**
	 * Called once per rendered frame while Playing.
	 */
	virtual void OnUpdate() {}

	virtual void Shutdown() {}

	/** This is virtual so we force dispatch through the vtable for custom registration, you aren't meant to override this in your game. */
	[[nodiscard]]
	virtual std::vector<EntityTypeInfo> GetEntityTypes() const;

	/** This is virtual so we force dispatch through the vtable for custom registration, you aren't meant to override this in your game. */
	[[nodiscard]]
	virtual std::vector<std::pair<std::string, Field>> GetReflectedFields() const;
};

using CreateGameFn = Game * (*)();