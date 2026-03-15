#include "Object.h"
#include <random>

// Internal UUID generator
// RFC-4122 §4.4 — version 4 (random):
//   - 122 bits of cryptographically-seeded random data
//   - bits 12-15 of octet 6  set to 0100  (version = 4)
//   - bits  6-7  of octet 8  set to 10    (variant = RFC 4122)
//
// We use std::random_device to seed a mt19937_64 engine.
// std::random_device maps to:
//   Windows  — BCryptGenRandom (CNG)
//   Linux    — /dev/urandom (or getrandom syscall)
//   macOS    — /dev/urandom
// All of those are cryptographically strong.
// It would be simpler if we only wanted to support Windows, but I want multiplatform in case I move to Linux, lol.

namespace
{
	// One engine per thread, seeded once from random_device.
	// thread_local avoids contention on multi-threaded object creation
	// and means we only pay the random_device cost once per thread.
	std::mt19937_64& RngInstance()
	{
		// Seed with two 64-bit words from random_device for full 128-bit entropy.
		thread_local std::mt19937_64 rng = []
			{
				std::random_device rd;
				const uint64_t s0 = (static_cast<uint64_t>(rd()) << 32) | rd();
				const uint64_t s1 = (static_cast<uint64_t>(rd()) << 32) | rd();
				std::seed_seq seq{ static_cast<uint32_t>(s0 >> 32),
								   static_cast<uint32_t>(s0),
								   static_cast<uint32_t>(s1 >> 32),
								   static_cast<uint32_t>(s1) };
				return std::mt19937_64(seq);
			}();
		return rng;
	}

	/**
	* Returns a fresh RFC-4122 v4 UUID
	*/
	std::string GenerateUUID()
	{
		auto& rng = RngInstance();

		// Fill 16 bytes (128 bits) with random data.
		uint8_t bytes[16]{};
		const uint64_t hi = rng();
		const uint64_t lo = rng();
		for (int i = 0; i < 8; ++i)
		{
			bytes[i] = static_cast<uint8_t>(hi >> (56 - i * 8));
		}

		for (int i = 0; i < 8; ++i)
		{
			bytes[i + 8] = static_cast<uint8_t>(lo >> (56 - i * 8));
		}

		// Apply RFC-4122 version and variant bits.
		bytes[6] = (bytes[6] & 0x0F) | 0x40; // version 4
		bytes[8] = (bytes[8] & 0x3F) | 0x80; // variant 1 (RFC 4122)

		// Format as "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"
		char buf[37];
		snprintf(buf, sizeof(buf),
			"%02X%02X%02X%02X-"
			"%02X%02X-"
			"%02X%02X-"
			"%02X%02X-"
			"%02X%02X%02X%02X%02X%02X",
			bytes[0], bytes[1], bytes[2], bytes[3],
			bytes[4], bytes[5],
			bytes[6], bytes[7],
			bytes[8], bytes[9],
			bytes[10], bytes[11], bytes[12],
			bytes[13], bytes[14], bytes[15]);

		return buf;
	}
}

void Object::GenerateId()
{
	id = GenerateUUID();
}

#include "Game.h"

std::vector<EntityTypeInfo> Game::GetEntityTypes() const
{
	return EntityRegistry::Get().GetAll();
}

std::vector<std::pair<std::string, Field>> Game::GetReflectedFields() const
{
	std::vector<std::pair<std::string, Field>> result;
	for (const auto& [className, fields] : Reflection::GetAllFields())
	{
		for (const auto& field : fields)
		{
			result.push_back({ className, field });
		}
	}
	return result;
}