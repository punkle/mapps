#include "JsonBinding.h"
#include "MappJson.h"

std::map<std::string, NativeAppFunction> createJsonBindings()
{
    std::map<std::string, NativeAppFunction> bindings;

    // json.get_string(jsonStr, key) -> string or nil
    bindings["get_string"] = [](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() < 2 || args[0].type != AppValue::String || args[1].type != AppValue::String)
            return AppValue();
        MappJson::JsonValue root = MappJson::parse(args[0].strVal);
        if (!root.isObject())
            return AppValue();
        auto it = root.obj.find(args[1].strVal);
        if (it == root.obj.end() || !it->second.isString())
            return AppValue();
        return AppValue(it->second.str);
    };

    // json.get_int(jsonStr, key) -> int or nil
    bindings["get_int"] = [](const std::vector<AppValue> &args) -> AppValue {
        if (args.size() < 2 || args[0].type != AppValue::String || args[1].type != AppValue::String)
            return AppValue();
        MappJson::JsonValue root = MappJson::parse(args[0].strVal);
        if (!root.isObject())
            return AppValue();
        auto it = root.obj.find(args[1].strVal);
        if (it == root.obj.end() || !it->second.isNumber())
            return AppValue();
        return AppValue((int)it->second.num);
    };

    return bindings;
}
