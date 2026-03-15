#include "World.h"
#include "Reflection.h"
#include <fstream>
#include <sstream>

static std::string JFloat(float v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%.4f", v);
	return buf;
}

static std::string JBool(bool v)
{
	return v ? "true" : "false";
}

static std::string ReadJsonValue(const std::string& json, const std::string& key, size_t begin = 0, size_t end = std::string::npos)
{
	if (end == std::string::npos)
	{
		end = json.size();
	}

	const std::string search = "\"" + key + "\"";
	const size_t keyPos = json.find(search, begin);
	if (keyPos == std::string::npos || keyPos >= end)
	{
		return {};
	}

	const size_t colon = json.find(':', keyPos);
	if (colon == std::string::npos || colon >= end)
	{
		return {};
	}

	size_t val = colon + 1;
	while (val < end && (json[val] == ' ' || json[val] == '\t' || json[val] == '\n' || json[val] == '\r'))
	{
		++val;
	}

	if (val >= end)
	{
		return {};
	}

	if (json[val] == '"')
	{
		std::string result;

		for (size_t i = val + 1; i < end; ++i)
		{
			if (json[i] == '\\' && i + 1 < end)
			{
				++i;
				if (json[i] == '"')
				{
					result += '"';
				}
				else if (json[i] == '\\')
				{
					result += '\\';
				}
				else
				{
					result += json[i];
				}
			}
			else if (json[i] == '"')
			{
				break;
			}
			else
			{
				result += json[i];
			}
		}

		return result;
	}

	size_t endVal = val;
	while (endVal < end && json[endVal] != ',' && json[endVal] != '}' && json[endVal] != '\n')
	{
		++endVal;
	}

	return json.substr(val, endVal - val);
}

static float RFloat(const std::string& j, const std::string& k, size_t b, size_t e)
{
	const std::string s = ReadJsonValue(j, k, b, e);
	return s.empty() ? 0.f : std::stof(s);
}

static bool RBool(const std::string& j, const std::string& k, size_t b, size_t e)
{
	return ReadJsonValue(j, k, b, e) == "true";
}

void World::InitEntity(Entity* e)
{
	e->world = this; // set back-pointer before any virtual call
	e->OnSpawn();
}

Entity* World::CreateEntity(const std::string& entityName)
{
	auto e = std::make_unique<Entity>();
	e->name = entityName;
	Entity* raw = e.get();
	entities.push_back(std::move(e));
	InitEntity(raw);
	return raw;
}

Entity* World::SpawnEntity(Entity* entity, const std::string& entityName)
{
	if (!entity)
	{
		return nullptr;
	}

	if (!entityName.empty())
	{
		entity->name = entityName;
	}

	entities.emplace_back(entity);
	InitEntity(entity);
	return entity;
}

void World::DestroyEntity(Entity* entity)
{
	const auto it = std::ranges::find_if(entities,
		[entity](const std::unique_ptr<Entity>& p)
		{
			return p.get() == entity;
		});

	if (it != entities.end())
	{
		(*it)->OnDestroy();
		entities.erase(it);
	}
}

void World::Clear()
{
	for (auto& entity : entities)
	{
		entity->OnDestroy();
	}

	entities.clear();
}

void World::BeginPlay()
{
	// Iterate by index: OnBeginPlay() is allowed to spawn new entities.
	for (const auto& entity : entities)
	{
		if (!entity->active)
		{
			continue;
		}

		entity->OnBeginPlay();
	}
}

void World::FixedUpdate()
{
	for (size_t i = 0; i < entities.size(); ++i)
	{
		if (!entities[i]->active)
		{
			continue;
		}

		entities[i]->OnFixedUpdate();
	}
}

void World::Update()
{
	// Iterate by index: Update() is allowed to spawn/destroy entities.
	for (size_t i = 0; i < entities.size(); ++i)
	{
		if (!entities[i]->active)
		{
			continue;
		}

		entities[i]->OnUpdate();
	}
}

void World::EndPlay()
{
	for (size_t i = 0; i < entities.size(); ++i)
	{
		if (!entities[i]->active)
		{
			continue;
		}

		entities[i]->OnEndPlay();
	}
}

std::string World::SerializeToString() const
{
	std::ostringstream f;
	f << "{\n";
	f << "  \"name\": \"" << name << "\",\n";
	f << "  \"entities\": [\n";

	for (size_t i = 0; i < entities.size(); ++i)
	{
		const Entity& e = *entities[i];
		const bool   last = (i + 1 == entities.size());

		f << "    {\n";
		f << "      \"type\": \"" << e.GetTypeName() << "\",\n";
		f << "      \"id\": \"" << e.id << "\",\n";
		f << "      \"name\": \"" << e.name << "\",\n";
		f << "      \"active\": " << JBool(e.active) << ",\n";
		f << "      \"transform\": {\n";
		f << "        \"x\": " << JFloat(e.transform.position.x) << ",\n";
		f << "        \"y\": " << JFloat(e.transform.position.y) << ",\n";
		f << "        \"rotation\": " << JFloat(e.transform.rotation.angle) << ",\n";
		f << "        \"scaleX\": " << JFloat(e.transform.scale.x) << ",\n";
		f << "        \"scaleY\": " << JFloat(e.transform.scale.y) << "\n";
		f << "      }";

		/**
		 * Serialize all reflected fields for this entity type.
		 */
		const std::vector<Field>* fields = Reflection::GetFields(e.GetTypeName());
		if (fields && !fields->empty())
		{
			f << ",\n";
			f << "      \"fields\": {\n";
			for (size_t j = 0; j < fields->size(); ++j)
			{
				const Field& field = (*fields)[j];
				const bool lastField = j + 1 == fields->size();

				f << "        \"" << field.name << "\": ";
				switch (field.type)
				{
				case FieldType::Float:
					f << JFloat(field.GetValue<float>(&e));
					break;
				case FieldType::Int:
					f << field.GetValue<int>(&e);
					break;
				case FieldType::Bool:
					f << JBool(field.GetValue<bool>(&e));
					break;
				case FieldType::String:
				{
					const std::string& str = field.GetValue<std::string>(&e);
					f << "\"" << str << "\"";
					break;
				}
				}
				f << (lastField ? "" : ",") << "\n";
			}
			f << "      }\n";
		}
		else
		{
			f << "\n";
		}

		f << "    }" << (last ? "" : ",") << "\n";
	}

	f << "  ]\n}\n";
	return f.str();
}

bool World::SaveToJson(const std::string& path) const
{
	std::ofstream f(path);

	if (!f)
	{
		return false;
	}

	f << SerializeToString();
	return true;
}

/** Shared parse logic used by both LoadFromJson and DeserializeFromString. */
static bool ParseEntities(const std::string& json, std::string& outName, std::vector<std::unique_ptr<Entity>>& entities, const EntityResolver& resolver)
{
	outName = ReadJsonValue(json, "name");
	if (outName.empty())
	{
		outName = "Untitled World";
	}

	entities.clear();

	const size_t arrOpen = json.find('[');
	if (arrOpen == std::string::npos)
	{
		return true;
	}

	size_t pos = arrOpen;
	while (true)
	{
		const size_t objOpen = json.find('{', pos + 1);
		if (objOpen == std::string::npos)
		{
			break;
		}

		// Match the closing brace, respecting nesting depth.
		int depth = 1;
		size_t objClose = objOpen + 1;
		while (objClose < json.size() && depth > 0)
		{
			if (json[objClose] == '{')
			{
				++depth;
			}
			else if (json[objClose] == '}')
			{
				--depth;
			}
			++objClose;
		}

		if (depth != 0)
		{
			break;
		}
		--objClose;

		const std::string typeName = ReadJsonValue(json, "type", objOpen, objClose);

		std::unique_ptr<Entity> e;
		if (resolver && !typeName.empty() && typeName != "Entity")
		{
			if (Entity* resolved = resolver(typeName.c_str()))
			{
				e.reset(resolved);
			}
		}
		if (!e)
		{
			e = std::make_unique<Entity>();
		}

		e->id = ReadJsonValue(json, "id", objOpen, objClose);
		e->name = ReadJsonValue(json, "name", objOpen, objClose);
		e->active = RBool(json, "active", objOpen, objClose);
		if (e->name.empty())
		{
			e->name = "Entity";
		}

		const size_t tKey = json.find("\"transform\"", objOpen);
		if (tKey != std::string::npos && tKey < objClose)
		{
			const size_t tBrace = json.find('{', tKey);
			const size_t tEnd = json.find('}', tBrace);
			if (tBrace != std::string::npos && tEnd != std::string::npos)
			{
				e->transform.position.x = RFloat(json, "x", tBrace, tEnd);
				e->transform.position.y = RFloat(json, "y", tBrace, tEnd);
				e->transform.rotation.angle = RFloat(json, "rotation", tBrace, tEnd);
				e->transform.scale.x = RFloat(json, "scaleX", tBrace, tEnd);
				e->transform.scale.y = RFloat(json, "scaleY", tBrace, tEnd);
			}
		}

		/**
		 * Deserialize all reflected fields for this entity type.
		 */
		const std::vector<Field>* fields = Reflection::GetFields(typeName);
		if (fields && !fields->empty())
		{
			const size_t fKey = json.find("\"fields\"", objOpen);
			if (fKey != std::string::npos && fKey < objClose)
			{
				const size_t fBrace = json.find('{', fKey);
				const size_t fEnd = json.find('}', fBrace);
				if (fBrace != std::string::npos && fEnd != std::string::npos)
				{
					for (const Field& field : *fields)
					{
						switch (field.type)
						{
						case FieldType::Float:
							field.SetValue<float>(e.get(), RFloat(json, field.name, fBrace, fEnd));
							break;
						case FieldType::Int:
						{
							const std::string val = ReadJsonValue(json, field.name, fBrace, fEnd);
							field.SetValue<int>(e.get(), val.empty() ? 0 : std::stoi(val));
							break;
						}
						case FieldType::Bool:
							field.SetValue<bool>(e.get(), ReadJsonValue(json, field.name, fBrace, fEnd) == "true");
							break;
						case FieldType::String:
							field.SetValue<std::string>(e.get(), ReadJsonValue(json, field.name, fBrace, fEnd));
							break;
						}
					}
				}
			}
		}

		entities.push_back(std::move(e));
		pos = objClose + 1;
	}
	return true;
}

bool World::LoadFromJson(const std::string& path, EntityResolver resolver)
{
	std::ifstream f(path);
	if (!f)
	{
		return false;
	}

	std::ostringstream ss;
	ss << f.rdbuf();

	std::vector<std::unique_ptr<Entity>> loaded;
	if (!ParseEntities(ss.str(), name, loaded, resolver))
	{
		return false;
	}

	// Set world pointers and call OnSpawn (this is a real load, not a snapshot restore).
	entities.clear();
	for (auto& entity : loaded)
	{
		Entity* rawEntity = entity.get();
		entities.push_back(std::move(entity));
		rawEntity->world = this;
		rawEntity->OnSpawn();
	}
	return true;
}

bool World::DeserializeFromString(const std::string& json, EntityResolver resolver)
{
	// Snapshot restore
	std::vector<std::unique_ptr<Entity>> loadedEntity;
	if (!ParseEntities(json, name, loadedEntity, resolver))
	{
		return false;
	}

	entities.clear();
	for (auto& entity : loadedEntity)
	{
		entity->world = this;
		entities.push_back(std::move(entity));
	}

	return true;
}