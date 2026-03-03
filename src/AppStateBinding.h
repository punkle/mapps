#pragma once

#include "mapps/AppRuntime.h"
#include "mapps/AppStateBackend.h"

#include <map>
#include <memory>
#include <string>

// Factory function that creates the app_state binding map.
// Registers: set, get, remove, clear.
// Lambdas capture appSlug + backend to scope state per-app.
std::map<std::string, NativeAppFunction> createAppStateBindings(
    const std::string &appSlug, std::shared_ptr<AppStateBackend> backend);
