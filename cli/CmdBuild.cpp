#include "CmdBuild.h"
#include "PosixIO.h"
#include "mapps/MappFormat.h"
#include "mapps/MappManifest.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

int cmdBuild(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: mapps build <app-dir> [--output slug.mapp]\n");
        return 1;
    }

    std::string appDir = argv[0];
    // Remove trailing slash
    while (appDir.size() > 1 && appDir.back() == '/')
        appDir.pop_back();

    std::string output;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output = argv[++i];
        }
    }

    if (output.empty()) {
        output = PosixIO::basename(appDir) + ".mapp";
    }

    if (!PosixIO::dirExists(appDir)) {
        fprintf(stderr, "Error: directory '%s' not found\n", appDir.c_str());
        return 1;
    }

    // Read and parse manifest first to know which files to include
    std::string manifestContent;
    if (!PosixIO::readFile(PosixIO::joinPath(appDir, "app.json"), manifestContent)) {
        fprintf(stderr, "Error: app.json not found in '%s'\n", appDir.c_str());
        return 1;
    }

    MappManifest manifest;
    if (!parseManifest(manifestContent, manifest)) {
        fprintf(stderr, "Error: failed to parse app.json\n");
        return 1;
    }

    // Build set of files referenced by the manifest
    std::set<std::string> manifestFiles;
    for (const auto &ep : manifest.entries) {
        manifestFiles.insert(ep.second);
    }

    std::vector<MappFile> files;

    // app.json must be first
    files.push_back({"app.json", manifestContent});

    // Add entry files in sorted order
    std::vector<std::string> sortedFiles(manifestFiles.begin(), manifestFiles.end());
    std::sort(sortedFiles.begin(), sortedFiles.end());

    for (const auto &filename : sortedFiles) {
        std::string fullPath = PosixIO::joinPath(appDir, filename);
        std::string content;
        if (!PosixIO::readFile(fullPath, content)) {
            fprintf(stderr, "Error: failed to read %s\n", fullPath.c_str());
            return 1;
        }
        files.push_back({filename, content});
    }

    std::vector<uint8_t> mapp = MappFormat::serialize(files);

    if (!PosixIO::writeFileBytes(output, mapp)) {
        fprintf(stderr, "Error: failed to write %s\n", output.c_str());
        return 1;
    }

    printf("Built %s (%zu bytes, %zu files)\n", output.c_str(), mapp.size(), files.size());
    return 0;
}
