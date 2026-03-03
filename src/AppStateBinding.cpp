#include "AppStateBinding.h"

#include <cmath>

// Tagged string encoding for type-safe storage:
//   "i:42"     -> Int 42
//   "f:3.14"   -> Float 3.14
//   "b:1"      -> Bool true
//   "b:0"      -> Bool false
//   "s:hello"  -> String "hello"

static std::string encodeValue(const AppValue &v)
{
    switch (v.type) {
    case AppValue::Int:
        return "i:" + std::to_string(v.intVal);
    case AppValue::Float:
        return "f:" + std::to_string(v.floatVal);
    case AppValue::Bool:
        return std::string("b:") + (v.boolVal ? "1" : "0");
    case AppValue::String:
        return "s:" + v.strVal;
    case AppValue::Nil:
    default:
        return "";
    }
}

static AppValue decodeValue(const std::string &encoded)
{
    if (encoded.size() < 2 || encoded[1] != ':')
        return AppValue();

    char tag = encoded[0];
    std::string payload = encoded.substr(2);

    switch (tag) {
    case 'i':
        return AppValue(std::stoi(payload));
    case 'f':
        return AppValue(std::stof(payload));
    case 'b':
        return AppValue(payload == "1");
    case 's':
        return AppValue(payload);
    default:
        return AppValue();
    }
}

std::map<std::string, NativeAppFunction> createAppStateBindings(
    const std::string &appSlug, std::shared_ptr<AppStateBackend> backend)
{
    std::map<std::string, NativeAppFunction> bindings;

    // app_state.set(key, value) -> bool
    bindings["set"] = [appSlug, backend](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() < 2 || args[0].type != AppValue::String)
            return AppValue(false);
        std::string encoded = encodeValue(args[1]);
        if (encoded.empty())
            return AppValue(false);
        return AppValue(backend->set(appSlug, args[0].strVal, encoded));
    };

    // app_state.get(key) -> value or nil
    bindings["get"] = [appSlug, backend](const std::vector<AppValue> &args) -> AppValue {
        if (args.empty() || args[0].type != AppValue::String)
            return AppValue();
        bool found = false;
        std::string encoded = backend->get(appSlug, args[0].strVal, found);
        if (!found)
            return AppValue();
        return decodeValue(encoded);
    };

    // app_state.remove(key) -> bool
    bindings["remove"] = [appSlug, backend](const std::vector<AppValue> &args) -> AppValue {
        if (args.empty() || args[0].type != AppValue::String)
            return AppValue(false);
        return AppValue(backend->remove(appSlug, args[0].strVal));
    };

    // app_state.clear() -> bool
    bindings["clear"] = [appSlug, backend](const std::vector<AppValue> &args) -> AppValue {
        (void)args;
        return AppValue(backend->clear(appSlug));
    };

    return bindings;
}
