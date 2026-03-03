#include "mapps/MappTrustStore.h"
#include "MappJson.h"
#include <algorithm>
#include <fstream>
#include <sstream>

static std::string toHex(const uint8_t *data, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        result += hex[(data[i] >> 4) & 0x0F];
        result += hex[data[i] & 0x0F];
    }
    return result;
}

bool MappTrustStore::load(const std::string &path)
{
    std::ifstream f(path);
    if (!f.is_open())
        return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
}

bool MappTrustStore::save(const std::string &path) const
{
    std::ofstream f(path);
    if (!f.is_open())
        return false;
    f << serialize();
    return f.good();
}

bool MappTrustStore::parse(const std::string &json)
{
    developers.clear();
    approvals.clear();
    MappJson::JsonValue root = MappJson::parse(json);
    if (!root.isObject())
        return false;

    if (root.obj.count("developers") && root.obj["developers"].isArray()) {
        for (const auto &item : root.obj["developers"].arr) {
            if (!item.isObject())
                continue;
            TrustedDeveloper dev;
            if (item.obj.count("key") && item.obj.at("key").isString())
                dev.keyHex = item.obj.at("key").str;
            if (item.obj.count("name") && item.obj.at("name").isString())
                dev.name = item.obj.at("name").str;
            if (!dev.keyHex.empty())
                developers.push_back(dev);
        }
    }

    // Parse approvals (optional — old JSON may not have this field)
    if (root.obj.count("approvals") && root.obj["approvals"].isArray()) {
        for (const auto &item : root.obj["approvals"].arr) {
            if (!item.isObject())
                continue;
            PermissionApproval approval;
            if (item.obj.count("signer") && item.obj.at("signer").isString())
                approval.signerHex = item.obj.at("signer").str;
            if (item.obj.count("app") && item.obj.at("app").isString())
                approval.appSlug = item.obj.at("app").str;
            if (item.obj.count("permissions") && item.obj.at("permissions").isArray()) {
                for (const auto &p : item.obj.at("permissions").arr) {
                    if (p.isString())
                        approval.permissions.push_back(p.str);
                }
            }
            std::sort(approval.permissions.begin(), approval.permissions.end());
            if (!approval.signerHex.empty() && !approval.appSlug.empty())
                approvals.push_back(approval);
        }
    }

    return true;
}

std::string MappTrustStore::serialize() const
{
    MappJson::JsonValue root;
    root.type = MappJson::JsonValue::Object;

    MappJson::JsonValue devArr;
    devArr.type = MappJson::JsonValue::Array;

    for (const auto &dev : developers) {
        MappJson::JsonValue obj;
        obj.type = MappJson::JsonValue::Object;
        obj.obj["key"] = MappJson::JsonValue(dev.keyHex);
        obj.obj["name"] = MappJson::JsonValue(dev.name);
        devArr.arr.push_back(obj);
    }

    root.obj["developers"] = devArr;

    MappJson::JsonValue appArr;
    appArr.type = MappJson::JsonValue::Array;

    for (const auto &approval : approvals) {
        MappJson::JsonValue obj;
        obj.type = MappJson::JsonValue::Object;
        obj.obj["signer"] = MappJson::JsonValue(approval.signerHex);
        obj.obj["app"] = MappJson::JsonValue(approval.appSlug);

        MappJson::JsonValue perms;
        perms.type = MappJson::JsonValue::Array;
        for (const auto &p : approval.permissions) {
            perms.arr.push_back(MappJson::JsonValue(p));
        }
        obj.obj["permissions"] = perms;
        appArr.arr.push_back(obj);
    }

    root.obj["approvals"] = appArr;
    return MappJson::serialize(root);
}

bool MappTrustStore::isDeveloperTrusted(const uint8_t pubKey[32]) const
{
    std::string hex = toHex(pubKey, 32);
    for (const auto &dev : developers) {
        if (dev.keyHex == hex)
            return true;
    }
    return false;
}

bool MappTrustStore::trustDeveloper(const uint8_t pubKey[32], const std::string &name)
{
    addDeveloper(pubKey, name);
    return true;
}

void MappTrustStore::addDeveloper(const uint8_t pubKey[32], const std::string &name)
{
    std::string hex = toHex(pubKey, 32);
    for (const auto &dev : developers) {
        if (dev.keyHex == hex)
            return;
    }
    TrustedDeveloper dev;
    dev.keyHex = hex;
    dev.name = name;
    developers.push_back(dev);
}

bool MappTrustStore::arePermissionsApproved(const uint8_t signerKey[32], const std::string &appSlug,
                                            const std::vector<std::string> &permissions) const
{
    std::string hex = toHex(signerKey, 32);

    // Sort a copy of the requested permissions for comparison
    std::vector<std::string> sorted = permissions;
    std::sort(sorted.begin(), sorted.end());

    for (const auto &approval : approvals) {
        if (approval.signerHex == hex && approval.appSlug == appSlug) {
            return approval.permissions == sorted;
        }
    }

    // Empty permissions are considered approved if no entry exists
    return sorted.empty();
}

bool MappTrustStore::approvePermissions(const uint8_t signerKey[32], const std::string &appSlug,
                                        const std::vector<std::string> &permissions)
{
    std::string hex = toHex(signerKey, 32);

    std::vector<std::string> sorted = permissions;
    std::sort(sorted.begin(), sorted.end());

    // Replace existing approval for this (signer, app) pair
    for (auto &approval : approvals) {
        if (approval.signerHex == hex && approval.appSlug == appSlug) {
            approval.permissions = sorted;
            return true;
        }
    }

    PermissionApproval approval;
    approval.signerHex = hex;
    approval.appSlug = appSlug;
    approval.permissions = sorted;
    approvals.push_back(approval);
    return true;
}
