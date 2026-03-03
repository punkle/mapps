#pragma once

#include "mapps/AppRuntime.h"

#include <map>
#include <string>

// Factory function that creates the json binding map.
// Registers: get_string, get_int.
// Uses MappJson (no external JSON dependency).
std::map<std::string, NativeAppFunction> createJsonBindings();
