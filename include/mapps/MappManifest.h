#pragma once

#include <map>
#include <string>
#include <vector>

struct MappFile;

struct MappManifest {
    std::string name;
    std::string version;
    std::string author;
    std::map<std::string, std::string> entries; // e.g. "bui" -> "bui.be", "mui" -> "mui.be"
    std::vector<std::string> permissions;
    std::string signature; // .sig filename if present

    // Set by discovery (not parsed from app.json)
    std::string slug;    // directory name, used as stable identifier
    std::string appPath; // filesystem path to app directory

    std::string getEntryFile(const std::string &entryPoint) const
    {
        auto it = entries.find(entryPoint);
        return (it != entries.end()) ? it->second : "";
    }

    // Look up an entry point and return its file content from parsed .mapp files.
    // Returns empty string if the entry point or file is not found.
    std::string getEntrySource(const std::string &entryPoint, const std::vector<MappFile> &files) const;
};

// Parse an app.json string into a MappManifest struct
bool parseManifest(const std::string &json, MappManifest &manifest);

// Serialize a MappManifest to compact JSON (for app.json)
std::string serializeManifest(const MappManifest &manifest);
