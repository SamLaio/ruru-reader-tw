#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

bool writeBinaryFileAtomic(const char* moduleName, const std::string& path, const uint8_t* data, size_t size);
bool writeTextFileAtomic(const char* moduleName, const std::string& path, const std::string& payload);
