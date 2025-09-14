// This file is a replacement for libvis/libvis.h.
// It provides a part of its functionality for the standalone libvis_vulkan library
// (such that this library does not need to know the original file's location and include all of it).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace vis {

// Import std namespace into vis namespace, reducing the typing effort for "std::".
using namespace std;

// Type definitions which are more concise and thus easier to read and write (no
// underscore). int is used as-is. 'u' stands for unsigned, 's' stands for signed.
typedef size_t usize;
typedef int64_t s64;
typedef uint64_t u64;
typedef int32_t s32;
typedef uint32_t u32;
typedef int16_t s16;
typedef uint16_t u16;
typedef int8_t s8;
typedef uint8_t u8;

}
