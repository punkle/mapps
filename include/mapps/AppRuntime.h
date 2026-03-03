#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

// Runtime-agnostic value type for binding args/returns
struct AppValue {
    enum Type { Nil, Int, Float, String, Bool };
    Type type = Nil;
    int intVal = 0;
    float floatVal = 0;
    std::string strVal;
    bool boolVal = false;

    AppValue() : type(Nil) {}
    AppValue(int v) : type(Int), intVal(v) {}
    AppValue(float v) : type(Float), floatVal(v) {}
    AppValue(const std::string &v) : type(String), strVal(v) {}
    AppValue(const char *v) : type(String), strVal(v) {}
    AppValue(bool v) : type(Bool), boolVal(v) {}
};

// Generic native function: takes args, returns a value (Nil for void)
using NativeAppFunction = std::function<AppValue(const std::vector<AppValue> &args)>;

// Abstract interface for app runtimes.
// Callers use this without knowing the underlying engine (Berry, etc.).
class AppRuntime
{
  public:
    virtual ~AppRuntime() = default;

    // Add a named group of bindings (e.g. "display" -> {draw_string, width, ...}).
    // The runtime maps these into the scripting environment as a module/class.
    // Returns this for chaining.
    virtual AppRuntime *addBindings(const std::string &moduleName,
                                    const std::map<std::string, NativeAppFunction> &bindings) = 0;

    // Override the auto-generated bootstrap for a module with custom script code.
    // The native functions from addBindings() are still registered; only the
    // script-side wrapper class is replaced.
    virtual void setBootstrap(const std::string &moduleName, const std::string &script) { (void)moduleName; (void)script; }

    // Boot the VM, register all bindings, load and execute the app script.
    virtual bool start() = 0;

    // Tear down the VM.
    virtual void stop() = 0;

    // Call a named global function in the app script.
    virtual AppValue call(const std::string &function) = 0;
    virtual AppValue call(const std::string &function, const std::vector<AppValue> &args) = 0;

    virtual bool isRunning() const = 0;
    virtual bool hasPermission(const std::string &perm) const = 0;

    // Return last error message from start()/call() (empty if none)
    virtual std::string getLastError() const { return ""; }
};
