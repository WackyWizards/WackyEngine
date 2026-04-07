#include "Texture.h"

bool Texture::IsValid() const
{
	return image != VK_NULL_HANDLE;
}
