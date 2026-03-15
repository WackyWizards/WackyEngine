#pragma once

#include <string>
#include <vector>
#include <map>

enum class FieldType
{
	Float,
	Int,
	Bool,
	String,
};

using Getter = const void* (*)(const void*);
using Setter = void (*)(void*, const void*);

/**
 * Metadata for a reflected field including name, type, and memory offset.
 * Provides typed accessors for getting/setting field values on instances.
 */
struct Field
{
	std::string name;
	FieldType type;

	/** Offset in bytes from object base */
	size_t offset;

	/** Size in bytes */
	size_t size;

	Getter getter;
	Setter setter;

	/**
	 * Gets a typed value from a field on an object instance.
	 */
	template<typename T>
	const T& GetValue(const void* instance) const
	{
		return *reinterpret_cast<const T*>(
			static_cast<const char*>(instance) + offset
			);
	}

	/**
	 * Sets a typed value on a field in an object instance.
	 */
	template<typename T>
	void SetValue(void* instance, const T& value) const
	{
		*reinterpret_cast<T*>(
			static_cast<char*>(instance) + offset
			) = value;
	}

	const void* GetRaw(const void* instance) const
	{
		return static_cast<const char*>(instance) + offset;
	}

	void* GetRaw(void* instance) const
	{
		return static_cast<char*>(instance) + offset;
	}
};

/**
 * Registry of fields for a given class type.
 * Fields are automatically registered at startup via REFLECT_FIELD macros.
 */
class Reflection
{
public:
	/**
	 * Registers a field for a given class type.
	 * Called automatically by REFLECT_FIELD macro.
	 */
	static void RegisterField(const std::string& className, const Field& field);

	static const std::map<std::string, std::vector<Field>>& GetAllFields();

	/**
	 * Retrieves all registered fields for a class type.
	 */
	static const std::vector<Field>* GetFields(const std::string& className);

	/**
	 * Checks if a class type has any registered fields.
	 */
	static bool HasFields(const std::string& className);

	static void ClearAll();
};

/**
 * Macro to register a public field for reflection.
 * Usage: REFLECT_FIELD(ClassName, fieldName, FieldType::Float)
 * Must be placed at namespace scope in a .cpp file.
 */
#define REFLECT_FIELD(ClassName, FieldName, FieldEnum) \
    REFLECT_FIELD_IMPL(ClassName, FieldName, FieldEnum, __COUNTER__)

#define REFLECT_FIELD_IMPL(ClassName, FieldName, FieldEnum, Counter) \
    static bool CONCAT(RegisterField_, ClassName, _, Counter)() \
    { \
        Field f; \
        f.name = #FieldName; \
        f.type = FieldEnum; \
        f.offset = offsetof(ClassName, FieldName); \
        f.size = sizeof(((ClassName*)nullptr)->FieldName); \
        Reflection::RegisterField(#ClassName, f); \
        return true; \
    } \
    static bool CONCAT(DummyVar_, ClassName, _, Counter) = \
        CONCAT(RegisterField_, ClassName, _, Counter)();

#define CONCAT(a, b, c, d) CONCAT_IMPL(a, b, c, d)
#define CONCAT_IMPL(a, b, c, d) a ## b ## c ## d
