#pragma once
#include <stddef.h>
#include <stdint.h>
// Strict bounded preflight for the existing SDK loader, not a second loader.
bool validateDriverElf(const uint8_t* bytes, size_t size, size_t* codeBytes = nullptr);
