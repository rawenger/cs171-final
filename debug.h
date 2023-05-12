//
// Created by ryan on 4/13/23.
//

#pragma once

#include "fmt/core.h"
#include "fmt/format.h"

#ifdef NDEBUG
#define DBG(...)
#define DBG_ERR(...)
#else
#define DBG(fmt__, ...)         do { fmt::print(fmt__ __VA_OPT__(,) __VA_ARGS__); fflush(stdout); } while(0)
#define DBG_ERR(fmt__, ...)     do { fmt::print(stderr, fmt__ __VA_OPT__(,) __VA_ARGS__); fflush(stderr); } while(0)
#endif


