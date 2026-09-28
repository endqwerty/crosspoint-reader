#pragma once

#include <cstdint>

using oflag_t = uint8_t;
constexpr oflag_t O_RDONLY = 0;
constexpr oflag_t O_WRONLY = 1;
constexpr oflag_t O_RDWR = 2;
constexpr oflag_t O_APPEND = 8;
constexpr oflag_t O_CREAT = 16;
constexpr oflag_t O_TRUNC = 32;
