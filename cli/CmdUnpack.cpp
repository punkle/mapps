#include "CmdUnpack.h"
#include "PosixIO.h"
#include "mapps/MappFormat.h"
#include <cstdio>
#include <cstring>
#include <string>

int cmdUnpack(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: mapps unpack <file.mapp> [--output <dir>]\n");
        return 1;
    }

    std::string mappFile = argv[0];
    std::string outputDir;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            outputDir = argv[++i];
        }
    }

    if (outputDir.empty()) {
        // Default: strip .mapp extension
        outputDir = mappFile;
        if (outputDir.size() > 5 && outputDir.substr(outputDir.size() - 5) == ".mapp") {
            outputDir = outputDir.substr(0, outputDir.size() - 5);
        }
    }

    // Read .mapp
    std::vector<uint8_t> mappData;
    if (!PosixIO::readFileBytes(mappFile, mappData)) {
        fprintf(stderr, "Error: failed to read %s\n", mappFile.c_str());
        return 1;
    }

    std::vector<MappFile> files;
    if (!MappFormat::deserialize(mappData, files)) {
        fprintf(stderr, "Error: invalid .mapp file\n");
        return 1;
    }

    if (!PosixIO::mkdirp(outputDir)) {
        fprintf(stderr, "Error: failed to create directory '%s'\n", outputDir.c_str());
        return 1;
    }

    for (const auto &f : files) {
        std::string path = PosixIO::joinPath(outputDir, f.name);
        if (!PosixIO::writeFile(path, f.content)) {
            fprintf(stderr, "Error: failed to write %s\n", path.c_str());
            return 1;
        }
        printf("  %s (%zu bytes)\n", f.name.c_str(), f.content.size());
    }

    printf("Unpacked %zu files to %s/\n", files.size(), outputDir.c_str());
    return 0;
}
