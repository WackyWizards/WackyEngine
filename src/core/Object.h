#pragma once

#include <string>

/**
 * Root of the engine object hierarchy.
 * Every named, identifiable thing derives from Object.
 *
 * IDs are RFC-4122 GUIDs generated via CoCreateGuid().
 *
 * Link: Ole32.lib  (system library, no install required)
 */
class Object
{
public:
	virtual ~Object() = default;

	/** Human-readable name. */
	std::string name = "Object";

	/**
	 * RFC-4122 GUID, e.g. "3F2504E0-4F89-41D3-9A0C-0305E82C3301".
	 * Stable across serialise/deserialise round-trips because it is written to
	 * and read back from the world JSON verbatim.
	 */
	std::string id;

	virtual const char* GetTypeName() const
	{
		return "Object";
	}

protected:
	/**
	 * Stamps a fresh RFC-4122 GUID into guid.
	 * Call once from each concrete constructor.
	 */
	void GenerateId();
};