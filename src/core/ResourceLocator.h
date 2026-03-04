#pragma once

#include <string>

namespace core::resources {
std::string ResolveResourcePath(const char* subdirectory, const char* fileName);
}
