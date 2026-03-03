#include "CmdUninstall.h"
#include "MeshtasticSerial.h"
#include "SerialPort.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int cmdUninstall(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: mapps uninstall <slug> [--port /dev/ttyXXX] [--reboot]\n");
        return 1;
    }

    std::string slug = argv[0];
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
        fprintf(stderr, "Error: failed to connect to device\n");
        return 1;
    }
    printf("Connected (node 0x%08x)\n", mesh.getMyNodeNum());

    // Find files belonging to this app from the manifest
    std::string prefix = "/apps/" + slug + "/";
    std::vector<std::string> filesToDelete;
    for (const auto &fi : mesh.getFileManifest()) {
        if (fi.name.rfind(prefix, 0) == 0) {
            filesToDelete.push_back(fi.name);
        }
    }

    if (filesToDelete.empty()) {
        fprintf(stderr, "Error: no files found for app '%s'\n", slug.c_str());
        return 1;
    }

    printf("Removing %zu files for app '%s'...\n", filesToDelete.size(), slug.c_str());

    for (const auto &path : filesToDelete) {
        printf("  Deleting %s...", path.c_str());
        fflush(stdout);
        if (mesh.deleteFile(path)) {
            printf(" done\n");
        } else {
            printf(" FAILED\n");
            fprintf(stderr, "Error: failed to delete %s\n", path.c_str());
            return 1;
        }
    }

    printf("Uninstall complete.\n");

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
