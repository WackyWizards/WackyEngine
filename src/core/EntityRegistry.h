#pragma once

#include "Entity.h"
#include <vector>

/**
 * Per-DLL singleton that holds every Entity subclass registered via ENTITY_TYPE.
 *
 * Each DLL that includes this header gets its own independent registry instance
 * (function-local static). The engine reads it through Game::GetEntityTypes().
 */
class EntityRegistry
{
public:
	static EntityRegistry& Get()
	{
		static EntityRegistry instance;
		return instance;
	}

	void Register(const char* typeName, const char* category, Entity* (*factory)())
	{
		entries.push_back({ typeName, category, factory });
	}

	const std::vector<EntityTypeInfo>& GetAll() const
	{
		return entries;
	}

private:
	EntityRegistry() = default;
	std::vector<EntityTypeInfo> entries;
};

/**
 * Instantiated once per registered type as an inline static class member.
 * Its constructor runs at DLL load time and inserts the type into EntityRegistry.
 */
template <typename T>
struct EntityRegistrar
{
	EntityRegistrar(const char* typeName, const char* category)
	{
		EntityRegistry::Get().Register(typeName, category, []() -> Entity* { return new T(); });
	}
};

/**
 * Place this macro once inside any Entity subclass to register it with the editor.
 * Category is optional - omit it to appear under "General".
 * The macro:
 *   1. Overrides GetTypeName() to return the class name as a string literal.
 *   2. Declares an inline static EntityRegistrar that fires at DLL load time
 *      and inserts the type + category into the per-DLL EntityRegistry.
 */
#define ENTITY_TYPE(ClassName, ...)                                                     \
public:                                                                                 \
    const char* GetTypeName() const override { return #ClassName; }                     \
private:                                                                                \
    inline static EntityRegistrar<ClassName> s_registrar{ #ClassName,                   \
        [] { constexpr const char* _args[] = { __VA_ARGS__ };                           \
             return (sizeof(_args) / sizeof(_args[0])) > 0 ? _args[0] : nullptr; }()    \
    };