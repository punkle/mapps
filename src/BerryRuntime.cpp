#include "mapps/BerryRuntime.h"
#include "mapps/AppStateBackend.h"
#include "AppStateBinding.h"
#include "JsonBinding.h"

extern "C" {
#include "be_exec.h"
}

#include <cstdio>

// Logging macros — consumers can override via -D flags
#ifndef LOG_DEBUG
#define LOG_DEBUG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_INFO
#define LOG_INFO(fmt, ...) fprintf(stderr, "[INFO] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_WARN
#define LOG_WARN(fmt, ...) fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__)
#endif

// Per-VM storage of flattened bindings (needed by static C callbacks)
static std::map<bvm *, const std::map<std::string, NativeAppFunction> *> vmBindings;

// Generic Berry native function wrapper — called for every bound function.
// The function name is stored as an upvalue (string at index 0).
static int berryNativeWrapper(bvm *vm)
{
    // Read upvalue 0 (the function name) from the current closure
    be_getupval(vm, 0, 0);
    const char *funcName = be_tostring(vm, -1);
    be_pop(vm, 1);
    if (!funcName)
        be_return_nil(vm);

    auto it = vmBindings.find(vm);
    if (it == vmBindings.end())
        be_return_nil(vm);

    const auto &bindings = *it->second;
    auto funcIt = bindings.find(funcName);
    if (funcIt == bindings.end())
        be_return_nil(vm);

    // Pop Berry args into vector<AppValue>
    int argc = be_top(vm);
    std::vector<AppValue> args;
    args.reserve(argc);
    for (int i = 1; i <= argc; i++) {
        if (be_isint(vm, i)) {
            args.push_back(AppValue(be_toint(vm, i)));
        } else if (be_isreal(vm, i)) {
            args.push_back(AppValue((float)be_toreal(vm, i)));
        } else if (be_isstring(vm, i)) {
            args.push_back(AppValue(be_tostring(vm, i)));
        } else if (be_isbool(vm, i)) {
            args.push_back(AppValue((bool)be_tobool(vm, i)));
        } else {
            args.push_back(AppValue()); // Nil
        }
    }

    // Call the native function
    AppValue result = funcIt->second(args);

    // Push return value
    switch (result.type) {
    case AppValue::Int:
        be_pushint(vm, result.intVal);
        be_return(vm);
    case AppValue::Float:
        be_pushreal(vm, result.floatVal);
        be_return(vm);
    case AppValue::String:
        be_pushstring(vm, result.strVal.c_str());
        be_return(vm);
    case AppValue::Bool:
        be_pushbool(vm, result.boolVal);
        be_return(vm);
    case AppValue::Nil:
    default:
        be_return_nil(vm);
    }
}

// --- Push AppValue onto Berry stack ---
static void pushAppValue(bvm *vm, const AppValue &v)
{
    switch (v.type) {
    case AppValue::Int:
        be_pushint(vm, v.intVal);
        break;
    case AppValue::Float:
        be_pushreal(vm, v.floatVal);
        break;
    case AppValue::String:
        be_pushstring(vm, v.strVal.c_str());
        break;
    case AppValue::Bool:
        be_pushbool(vm, v.boolVal);
        break;
    case AppValue::Nil:
    default:
        be_pushnil(vm);
        break;
    }
}

// --- Pop Berry return value into AppValue ---
static AppValue popAppValue(bvm *vm, int index)
{
    if (be_isint(vm, index))
        return AppValue(be_toint(vm, index));
    if (be_isreal(vm, index))
        return AppValue((float)be_toreal(vm, index));
    if (be_isstring(vm, index))
        return AppValue(be_tostring(vm, index));
    if (be_isbool(vm, index))
        return AppValue((bool)be_tobool(vm, index));
    return AppValue();
}

// ---- BerryRuntime implementation ----

BerryRuntime::BerryRuntime(const std::string &slug, const std::vector<std::string> &perms)
    : appSlug(slug), permissions(perms), vm(nullptr), instructionCount(0)
{
}

BerryRuntime::~BerryRuntime()
{
    destroyVM();
}

void BerryRuntime::setSource(const std::string &source)
{
    sourceCode = source;
}

void BerryRuntime::setAppStateBackend(std::shared_ptr<AppStateBackend> backend)
{
    appStateBackend = backend;
}

AppRuntime *BerryRuntime::addBindings(const std::string &moduleName,
                                      const std::map<std::string, NativeAppFunction> &bindings)
{
    moduleBindings[moduleName] = bindings;
    return this;
}

void BerryRuntime::setBootstrap(const std::string &moduleName, const std::string &script)
{
    customBootstraps[moduleName] = script;
}

bool BerryRuntime::start()
{
    LOG_DEBUG("[Berry] start: app '%s'", appSlug.c_str());

    lastError.clear();
    instructionCount = 0;

    // Auto-register json bindings (always available)
    if (moduleBindings.find("json") == moduleBindings.end()) {
        addBindings("json", createJsonBindings());
    }

    // Auto-register app_state bindings if backend is set and not already provided
    if (appStateBackend && moduleBindings.find("app_state") == moduleBindings.end()) {
        LOG_DEBUG("[Berry] start: app_state backend present, registering bindings");
        addBindings("app_state", createAppStateBindings(appSlug, appStateBackend));
    } else if (!appStateBackend) {
        LOG_DEBUG("[Berry] start: NO app_state backend set");
    }

    // Log registered modules
    LOG_DEBUG("[Berry] start: %u module(s) registered:", (unsigned)moduleBindings.size());
    for (const auto &mod : moduleBindings) {
        LOG_DEBUG("[Berry] start:   module '%s' with %u function(s)", mod.first.c_str(), (unsigned)mod.second.size());
    }

    // Flatten module bindings: "display" + "draw_string" -> "_display_draw_string"
    flatBindings.clear();
    for (const auto &mod : moduleBindings) {
        for (const auto &fn : mod.second) {
            std::string flatName = "_" + mod.first + "_" + fn.first;
            flatBindings[flatName] = fn.second;
        }
    }

    createVM();
    if (!vm) {
        lastError = "VM creation failed";
        LOG_ERROR("[Berry] start: %s", lastError.c_str());
        return false;
    }

    // Generate and execute bootstrap classes for each binding module
    std::string bootstrap = generateBootstrap();
    if (!bootstrap.empty()) {
        LOG_DEBUG("[Berry] start: bootstrap script (%u bytes):\n%s", (unsigned)bootstrap.size(), bootstrap.c_str());
        int result = be_loadbuffer(vm, "bootstrap", bootstrap.c_str(), bootstrap.size());
        if (result != 0) {
            lastError = std::string("bootstrap load error: ") + be_tostring(vm, -1);
            LOG_ERROR("[Berry] start: %s", lastError.c_str());
            be_pop(vm, 1);
            destroyVM();
            return false;
        }
        result = be_pcall(vm, 0);
        if (result != 0) {
            lastError = std::string("bootstrap exec error: ") + be_tostring(vm, -1);
            LOG_ERROR("[Berry] start: %s", lastError.c_str());
            be_pop(vm, 1);
            destroyVM();
            return false;
        }
        be_pop(vm, 1);
    } else {
        LOG_DEBUG("[Berry] start: no bootstrap (no module bindings)");
    }

    if (sourceCode.empty()) {
        lastError = "no source code provided (call setSource() first)";
        LOG_ERROR("[Berry] start: %s", lastError.c_str());
        destroyVM();
        return false;
    }

    LOG_DEBUG("[Berry] start: loading %u bytes of source:\n%s", (unsigned)sourceCode.size(), sourceCode.c_str());

    // Load and execute the Berry script
    int result = be_loadbuffer(vm, "app", sourceCode.c_str(), sourceCode.size());
    if (result != 0) {
        lastError = std::string("source load error: ") + be_tostring(vm, -1);
        LOG_ERROR("[Berry] start: %s", lastError.c_str());
        be_pop(vm, 1);
        destroyVM();
        return false;
    }

    result = be_pcall(vm, 0);
    if (result != 0) {
        lastError = std::string("source exec error: ") + be_tostring(vm, -1);
        LOG_ERROR("[Berry] start: %s", lastError.c_str());
        be_pop(vm, 1);
        destroyVM();
        return false;
    }
    be_pop(vm, 1);

    LOG_INFO("[Berry] start: '%s' launched successfully", appSlug.c_str());
    return true;
}

void BerryRuntime::stop()
{
    call("cleanup");
    destroyVM();
    instructionCount = 0;
    flatBindings.clear();
}

AppValue BerryRuntime::call(const std::string &function)
{
    std::vector<AppValue> noArgs;
    return call(function, noArgs);
}

AppValue BerryRuntime::call(const std::string &function, const std::vector<AppValue> &args)
{
    if (!vm) {
        LOG_ERROR("[Berry] call('%s'): vm is NULL", function.c_str());
        return AppValue();
    }

    // Reset instruction counter
    instructionCount = 0;

    int stackBefore = be_top(vm);

    be_getglobal(vm, function.c_str());
    if (!be_isfunction(vm, -1)) {
        be_pop(vm, 1);
        return AppValue();
    }

    // Push arguments
    for (const auto &arg : args) {
        pushAppValue(vm, arg);
    }

    int argc = (int)args.size();
    LOG_DEBUG("[Berry] call('%s', argc=%d) stack before=%d", function.c_str(), argc, stackBefore);
    int result = be_pcall(vm, argc);
    int stackAfterPcall = be_top(vm);
    LOG_DEBUG("[Berry] call('%s'): pcall returned %d, stack after=%d", function.c_str(), result, stackAfterPcall);
    if (result != 0) {
        LOG_ERROR("[Berry] call('%s'): error: %s", function.c_str(), be_tostring(vm, -1));
        be_pop(vm, argc + 1);
        return AppValue();
    }

    // After be_pcall, return value is at -(argc+1) (the function slot).
    // Pop args first, then read and pop the return value.
    be_pop(vm, argc);
    AppValue retVal = popAppValue(vm, -1);
    be_pop(vm, 1); // pop return value

    int stackAfter = be_top(vm);
    if (stackAfter != stackBefore) {
        LOG_WARN("[Berry] call('%s'): stack drift! before=%d after=%d", function.c_str(), stackBefore, stackAfter);
    }

    return retVal;
}

// --- Generate Berry bootstrap script from moduleBindings ---
// For module "display" with functions {draw_string, width, ...}, generates:
//   class display
//     static def draw_string(a,b,c,d,e,f,g,h) return _display_draw_string(a,b,c,d,e,f,g,h) end
//     static def width(a,b,c,d,e,f,g,h) return _display_width(a,b,c,d,e,f,g,h) end
//   end
std::string BerryRuntime::generateBootstrap() const
{
    if (moduleBindings.empty())
        return "";

    static const char *genericArgs = "a,b,c,d,e,f,g,h";

    std::string script;
    for (const auto &mod : moduleBindings) {
        auto customIt = customBootstraps.find(mod.first);
        if (customIt != customBootstraps.end()) {
            // Use the custom bootstrap script for this module
            script += customIt->second + "\n";
        } else {
            // Auto-generate a static class wrapper
            script += "class " + mod.first + "\n";
            for (const auto &fn : mod.second) {
                std::string flatName = "_" + mod.first + "_" + fn.first;
                script +=
                    "  static def " + fn.first + "(" + genericArgs + ") return " + flatName + "(" + genericArgs + ") end\n";
            }
            script += "end\n";
        }
    }
    return script;
}

// --- VM lifecycle ---

struct RegisterData {
    BerryRuntime *runtime;
};

static void registerBindingsProtected(bvm *vm, void *data)
{
    RegisterData *rd = static_cast<RegisterData *>(data);

    // Store flattened bindings pointer for this VM
    vmBindings[vm] = &rd->runtime->flatBindings;

    // Register each flattened binding as a Berry global function with name as upvalue
    for (const auto &kv : rd->runtime->flatBindings) {
        be_pushntvclosure(vm, berryNativeWrapper, 1); // closure with 1 upvalue slot
        be_pushstring(vm, kv.first.c_str());          // push function name
        be_setupval(vm, -2, 0);                       // set as upvalue 0 of closure
        be_pop(vm, 1);                                // pop the string
        be_setglobal(vm, kv.first.c_str());           // register closure as global
        be_pop(vm, 1);                                // pop the closure
    }

    // Store runtime pointer for obsHook
    be_pushcomptr(vm, rd->runtime);
    be_setglobal(vm, "_berry_runtime");
    be_pop(vm, 1);
}

void BerryRuntime::createVM()
{
    if (vm) {
        destroyVM();
    }

    vm = be_vm_new();
    if (!vm) {
        LOG_ERROR("[Berry] createVM: be_vm_new returned NULL");
        return;
    }

    be_set_obs_hook(vm, obsHook);
    registerBindings();
}

void BerryRuntime::destroyVM()
{
    if (vm) {
        vmBindings.erase(vm);
        be_vm_delete(vm);
        vm = nullptr;
    }
}

void BerryRuntime::registerBindings()
{
    if (!vm)
        return;

    RegisterData rd;
    rd.runtime = this;

    int result = be_execprotected(vm, registerBindingsProtected, &rd);
    if (result != 0) {
        LOG_ERROR("[Berry] registerBindings: failed with error %d", result);
    }
}

bool BerryRuntime::hasPermission(const std::string &perm) const
{
    for (const auto &p : permissions) {
        if (p == perm)
            return true;
    }
    return false;
}

void BerryRuntime::obsHook(bvm *vm, int event, ...)
{
    if (event != BE_OBS_VM_HEARTBEAT)
        return;

    be_getglobal(vm, "_berry_runtime");
    if (!be_iscomptr(vm, -1)) {
        be_pop(vm, 1);
        return;
    }
    BerryRuntime *runtime = static_cast<BerryRuntime *>(be_tocomptr(vm, -1));
    be_pop(vm, 1);

    runtime->instructionCount++;
    if (runtime->instructionCount > MAX_INSTRUCTIONS) {
        be_raise(vm, "runtime_error", "script exceeded instruction limit");
    }
}
