#pragma once

// Lua 5.0.2 is built as C; its headers carry no extern "C" guards.
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
