#include "CmdList.h"
#include "MeshtasticSerial.h"
#include "SerialPort.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

// Simple JSON value extraction: find "key":"value" and return value
static std::string jsonStringValue(const std::string &json, const std::string &key)
{
    std::string needle = "\"" + key + "\":\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos)
        return "";
    pos += needle.size();
    size_t end = json.find('"', pos);
    if (end == std::string::npos)
        return "";
    return json.substr(pos, end - pos);
}

int cmdList(int argc, char **argv)
{
    std::string portPath;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            portPath = argv[++i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (portPath.empty()) {
        portPath = SerialPort::findPort();
        if (portPath.empty())
            return 1;
    }

    SerialPort serial;
    if (!serial.open(portPath))
        return 1;

    MeshtasticSerial mesh(serial);
    if (!mesh.connect()) {
        fprintf(stderr, "Error: failed to connect to device on %s\n", portPath.c_str());
        return 1;
    }

    // Find unique app slugs from file manifest
    // Files under /apps/<slug>/... indicate installed apps
    std::map<std::string, std::vector<const DeviceFileInfo *>> appFiles;
    for (const auto &fi : mesh.getFileManifest()) {
        if (fi.name.rfind("/apps/", 0) == 0) {
            // Extract slug: /apps/<slug>/...
            size_t slugStart = 6; // length of "/apps/"
            size_t slugEnd = fi.name.find('/', slugStart);
            if (slugEnd != std::string::npos) {
                std::string slug = fi.name.substr(slugStart, slugEnd - slugStart);
                appFiles[slug].push_back(&fi);
            }
        }
    }

    if (appFiles.empty()) {
        return 0;
    }

    for (const auto &entry : appFiles) {
        const std::string &slug = entry.first;

        // Try to download app.json to get name and version
        std::string appJsonPath = "/apps/" + slug + "/app.json";
        std::vector<uint8_t> jsonData;
        std::string name = slug;
        std::string version = "?";

        if (mesh.downloadFile(appJsonPath, jsonData)) {
            std::string json(jsonData.begin(), jsonData.end());
            std::string parsedName = jsonStringValue(json, "name");
            std::string parsedVersion = jsonStringValue(json, "version");
            if (!parsedName.empty())
                name = parsedName;
            if (!parsedVersion.empty())
                version = parsedVersion;
        }

        // Calculate total size
        uint32_t totalSize = 0;
        for (const auto *fi : entry.second)
            totalSize += fi->size;

        printf("  %-20s v%-10s (%u bytes, %zu files)\n", name.c_str(), version.c_str(), totalSize, entry.second.size());
    }

    return 0;
}
