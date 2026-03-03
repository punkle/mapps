#include "MappJson.h"

namespace MappJson {

// Skip whitespace
static size_t skipWS(const std::string &s, size_t pos)
{
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
        pos++;
    return pos;
}

// Parse a JSON string (starting at opening quote). Returns position after closing quote.
static size_t parseString(const std::string &s, size_t pos, std::string &out)
{
    out.clear();
    if (pos >= s.size() || s[pos] != '"')
        return std::string::npos;
    pos++; // skip opening quote

    while (pos < s.size()) {
        char c = s[pos];
        if (c == '"') {
            return pos + 1;
        }
        if (c == '\\' && pos + 1 < s.size()) {
            pos++;
            char esc = s[pos];
            switch (esc) {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            default:
                out += esc;
                break;
            }
        } else {
            out += c;
        }
        pos++;
    }
    return std::string::npos;
}

// Forward declaration
static size_t parseValue(const std::string &s, size_t pos, JsonValue &val);

static size_t parseArray(const std::string &s, size_t pos, JsonValue &val)
{
    val.type = JsonValue::Array;
    val.arr.clear();
    if (pos >= s.size() || s[pos] != '[')
        return std::string::npos;
    pos++; // skip [

    pos = skipWS(s, pos);
    if (pos < s.size() && s[pos] == ']')
        return pos + 1;

    while (pos < s.size()) {
        JsonValue elem;
        pos = parseValue(s, pos, elem);
        if (pos == std::string::npos)
            return std::string::npos;
        val.arr.push_back(elem);

        pos = skipWS(s, pos);
        if (pos >= s.size())
            return std::string::npos;
        if (s[pos] == ']')
            return pos + 1;
        if (s[pos] != ',')
            return std::string::npos;
        pos++; // skip comma
        pos = skipWS(s, pos);
    }
    return std::string::npos;
}

static size_t parseObject(const std::string &s, size_t pos, JsonValue &val)
{
    val.type = JsonValue::Object;
    val.obj.clear();
    if (pos >= s.size() || s[pos] != '{')
        return std::string::npos;
    pos++; // skip {

    pos = skipWS(s, pos);
    if (pos < s.size() && s[pos] == '}')
        return pos + 1;

    while (pos < s.size()) {
        pos = skipWS(s, pos);
        std::string key;
        pos = parseString(s, pos, key);
        if (pos == std::string::npos)
            return std::string::npos;

        pos = skipWS(s, pos);
        if (pos >= s.size() || s[pos] != ':')
            return std::string::npos;
        pos++; // skip colon
        pos = skipWS(s, pos);

        JsonValue child;
        pos = parseValue(s, pos, child);
        if (pos == std::string::npos)
            return std::string::npos;
        val.obj[key] = child;

        pos = skipWS(s, pos);
        if (pos >= s.size())
            return std::string::npos;
        if (s[pos] == '}')
            return pos + 1;
        if (s[pos] != ',')
            return std::string::npos;
        pos++; // skip comma
    }
    return std::string::npos;
}

static size_t parseNumber(const std::string &s, size_t pos, JsonValue &val)
{
    size_t start = pos;
    if (pos < s.size() && s[pos] == '-')
        pos++;
    if (pos >= s.size() || s[pos] < '0' || s[pos] > '9')
        return std::string::npos;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9')
        pos++;
    if (pos < s.size() && s[pos] == '.') {
        pos++;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9')
            pos++;
    }
    val.type = JsonValue::Number;
    val.num = std::stod(s.substr(start, pos - start));
    return pos;
}

static size_t parseValue(const std::string &s, size_t pos, JsonValue &val)
{
    pos = skipWS(s, pos);
    if (pos >= s.size())
        return std::string::npos;

    char c = s[pos];
    if (c == '"') {
        val.type = JsonValue::String;
        return parseString(s, pos, val.str);
    } else if (c == '[') {
        return parseArray(s, pos, val);
    } else if (c == '{') {
        return parseObject(s, pos, val);
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        return parseNumber(s, pos, val);
    } else if (s.compare(pos, 4, "true") == 0) {
        val.type = JsonValue::Bool;
        val.boolVal = true;
        return pos + 4;
    } else if (s.compare(pos, 5, "false") == 0) {
        val.type = JsonValue::Bool;
        val.boolVal = false;
        return pos + 5;
    } else if (s.compare(pos, 4, "null") == 0) {
        val.type = JsonValue::Null;
        return pos + 4;
    }
    return std::string::npos;
}

JsonValue parse(const std::string &json)
{
    JsonValue val;
    size_t pos = skipWS(json, 0);
    size_t end = parseValue(json, pos, val);
    if (end == std::string::npos)
        val.type = JsonValue::Null;
    return val;
}

static void serializeString(const std::string &s, std::string &out)
{
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    out += '"';
}

static void serializeValue(const JsonValue &val, std::string &out)
{
    switch (val.type) {
    case JsonValue::Null:
        out += "null";
        break;
    case JsonValue::Bool:
        out += val.boolVal ? "true" : "false";
        break;
    case JsonValue::String:
        serializeString(val.str, out);
        break;
    case JsonValue::Number: {
        // Output as integer if no fractional part
        int intVal = (int)val.num;
        if ((double)intVal == val.num)
            out += std::to_string(intVal);
        else
            out += std::to_string(val.num);
        break;
    }
    case JsonValue::Array:
        out += '[';
        for (size_t i = 0; i < val.arr.size(); i++) {
            if (i > 0)
                out += ',';
            serializeValue(val.arr[i], out);
        }
        out += ']';
        break;
    case JsonValue::Object:
        out += '{';
        {
            bool first = true;
            for (const auto &kv : val.obj) {
                if (!first)
                    out += ',';
                first = false;
                serializeString(kv.first, out);
                out += ':';
                serializeValue(kv.second, out);
            }
        }
        out += '}';
        break;
    }
}

std::string serialize(const JsonValue &val)
{
    std::string out;
    serializeValue(val, out);
    return out;
}

} // namespace MappJson
