#include "Reflection.h"
#include <mutex>

static std::mutex& GetMutex()
{
	static std::mutex mtx;
	return mtx;
}

static std::map<std::string, std::vector<Field>>& FieldMap()
{
	static std::map<std::string, std::vector<Field>> instance;
	return instance;
}

void Reflection::RegisterField(const std::string& className, const Field& field)
{
	std::lock_guard lock(GetMutex());
	FieldMap()[className].push_back(field);
}

const std::map<std::string, std::vector<Field>>& Reflection::GetAllFields()
{
	return FieldMap();
}

const std::vector<Field>* Reflection::GetFields(const std::string& className)
{
	std::lock_guard lock(GetMutex());
	const auto& map = FieldMap();
	const auto it = map.find(className);
	if (it != map.end())
	{
		return &it->second;
	}
	return nullptr;
}

bool Reflection::HasFields(const std::string& className)
{
	std::lock_guard lock(GetMutex());
	return FieldMap().contains(className);
}

void Reflection::ClearAll()
{
	std::lock_guard lock(GetMutex());
	FieldMap().clear();
}