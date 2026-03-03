#include "CmdInstall.h"
#include "MeshtasticSerial.h"
#include "PosixIO.h"
#include "SerialPort.h"
#include "mapps/MappFormat.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

int cmdInstall(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: mapps install <file.mapp> [--port /dev/ttyXXX] [--reboot]\n");
        return 1;
    }

    std::string mappPath = argv[0];
    std::string portPath;
    bool doReboot = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            portPath = argv[++i];
        } else if (strcmp(argv[i], "--reboot") == 0) {
            doReboot = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    // Read .mapp file
    std::vector<uint8_t> mappData;
    if (!PosixIO::readFileBytes(mappPath, mappData)) {
        fprintf(stderr, "Error: cannot read '%s'\n", mappPath.c_str());
        return 1;
    }

    // Deserialize .mapp
    std::vector<MappFile> files;
    if (!MappFormat::deserialize(mappData, files)) {
        fprintf(stderr, "Error: invalid .mapp file '%s'\n", mappPath.c_str());
        return 1;
    }

    if (files.empty()) {
        fprintf(stderr, "Error: .mapp contains no files\n");
        return 1;
    }

    // Extract slug from filename: "counter.mapp" -> "counter"
    std::string basename = PosixIO::basename(mappPath);
    std::string slug = basename;
    size_t dotPos = slug.rfind(".mapp");
    if (dotPos != std::string::npos)
        slug = slug.substr(0, dotPos);

    printf("Installing '%s' (%zu files) to /apps/%s/\n", basename.c_str(), files.size(), slug.c_str());

    // Auto-detect port if not specified
    if (portPath.empty()) {
        portPath = SerialPort::findPort();
        if (portPath.empty())
            return 1;
    }

    auto startTime = std::chrono::steady_clock::now();

    printf("Using serial port: %s\n", portPath.c_str());

    // Open serial port
    SerialPort serial;
    if (!serial.open(portPath)) {
        return 1;
    }

    // Connect to device
    MeshtasticSerial mesh(serial);
    printf("Connecting to device...\n");
    if (!mesh.connect()) {
        fprintf(stderr, "Error: failed to connect to device\n");
        return 1;
    }
    printf("Connected (node 0x%08x)\n", mesh.getMyNodeNum());

    // Upload each file
    for (size_t i = 0; i < files.size(); i++) {
        const auto &f = files[i];
        std::string devicePath = "/apps/" + slug + "/" + f.name;

        printf("[%zu/%zu] Uploading %s (%zu bytes)...", i + 1, files.size(), f.name.c_str(), f.content.size());
        fflush(stdout);

        bool ok = mesh.sendFile(devicePath, reinterpret_cast<const uint8_t *>(f.content.data()), f.content.size(),
                                [&](size_t sent, size_t total) {
                                    int pct = (int)(sent * 100 / total);
                                    printf("\r[%zu/%zu] Uploading %s (%zu bytes)... %d%%", i + 1, files.size(), f.name.c_str(),
                                           f.content.size(), pct);
                                    fflush(stdout);
                                });

        if (!ok) {
            printf("\n");
            fprintf(stderr, "Error: failed to upload %s\n", f.name.c_str());
            return 1;
        }
        printf("\r[%zu/%zu] Uploading %s (%zu bytes)... done\n", i + 1, files.size(), f.name.c_str(), f.content.size());
    }

    auto elapsed = std::chrono::steady_clock::now() - startTime;
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    printf("Install complete in %lld:%02lld.\n", secs / 60, secs % 60);

    if (doReboot) {
        printf("Requesting device reboot...\n");
        if (!mesh.sendReboot(2)) {
            fprintf(stderr, "Warning: reboot command may not have been accepted\n");
        } else {
            printf("Device will reboot in 2 seconds.\n");
        }
    }

    return 0;
}
