#include "mapps/MappManifest.h"
#include "mapps/MappFormat.h"
#include "MappJson.h"

std::string MappManifest::getEntrySource(const std::string &entryPoint, const std::vector<MappFile> &files) const
{
    std::string filename = getEntryFile(entryPoint);
    if (filename.empty())
        return "";
    for (const auto &f : files) {
        if (f.name == filename)
            return f.content;
    }
    return "";
}

bool parseManifest(const std::string &json, MappManifest &manifest)
{
    MappJson::JsonValue root = MappJson::parse(json);
    if (!root.isObject())
        return false;

    auto &obj = root.obj;

    // Required fields
    if (obj.count("name") && obj["name"].isString())
        manifest.name = obj["name"].str;
    if (obj.count("version") && obj["version"].isString())
        manifest.version = obj["version"].str;
    if (obj.count("author") && obj["author"].isString())
        manifest.author = obj["author"].str;

    // Entry points: {"entry": {"bui": "main.be"}}
    if (obj.count("entry") && obj["entry"].isObject()) {
        for (const auto &kv : obj["entry"].obj) {
            if (kv.second.isString())
                manifest.entries[kv.first] = kv.second.str;
        }
    }

    // Permissions: ["http-client", ...]
    if (obj.count("permissions") && obj["permissions"].isArray()) {
        for (const auto &p : obj["permissions"].arr) {
            if (p.isString())
                manifest.permissions.push_back(p.str);
        }
    }

    // Signature filename (optional)
    if (obj.count("signature") && obj["signature"].isString())
        manifest.signature = obj["signature"].str;

    return !manifest.name.empty();
}

std::string serializeManifest(const MappManifest &manifest)
{
    MappJson::JsonValue root;
    root.type = MappJson::JsonValue::Object;

    root.obj["name"] = MappJson::JsonValue(manifest.name);
    root.obj["version"] = MappJson::JsonValue(manifest.version);
    root.obj["author"] = MappJson::JsonValue(manifest.author);

    // Entry points
    MappJson::JsonValue entry;
    entry.type = MappJson::JsonValue::Object;
    for (const auto &kv : manifest.entries) {
        entry.obj[kv.first] = MappJson::JsonValue(kv.second);
    }
    root.obj["entry"] = entry;

    // Permissions
    MappJson::JsonValue perms;
    perms.type = MappJson::JsonValue::Array;
    for (const auto &p : manifest.permissions) {
        perms.arr.push_back(MappJson::JsonValue(p));
    }
    root.obj["permissions"] = perms;

    // Signature (if present)
    if (!manifest.signature.empty()) {
        root.obj["signature"] = MappJson::JsonValue(manifest.signature);
    }

    return MappJson::serialize(root);
}
