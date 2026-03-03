#pragma once

#include <map>
#include <string>
#include <vector>

// Minimal JSON parser for app.json and keystore files.
// Supports: strings, arrays of strings, nested objects (one level for "entry").
// No external dependencies.

namespace MappJson {

struct JsonValue {
    enum Type { Null, String, Number, Bool, Array, Object };
    Type type = Null;
    std::string str;
    double num = 0;
    bool boolVal = false;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;

    JsonValue() = default;
    explicit JsonValue(const std::string &s) : type(String), str(s) {}
    explicit JsonValue(double n) : type(Number), num(n) {}
    explicit JsonValue(int n) : type(Number), num(n) {}
    explicit JsonValue(bool b) : type(Bool), boolVal(b) {}

    bool isString() const { return type == String; }
    bool isNumber() const { return type == Number; }
    bool isBool() const { return type == Bool; }
    bool isArray() const { return type == Array; }
    bool isObject() const { return type == Object; }
};

// Parse a JSON string into a JsonValue. Returns Null type on failure.
JsonValue parse(const std::string &json);

// Serialize a JsonValue to compact JSON string (no whitespace)
std::string serialize(const JsonValue &val);

} // namespace MappJson
