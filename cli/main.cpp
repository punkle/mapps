#include "CmdBuild.h"
#include "CmdCreate.h"
#include "CmdInstall.h"
#include "CmdKeygen.h"
#include "CmdList.h"
#include "CmdSign.h"
#include "CmdUninstall.h"
#include "CmdUnpack.h"
#include "CmdVerify.h"
#include <cstdio>
#include <cstring>

static void printUsage()
{
    printf("mapps - Meshtastic App Packaging Tool\n\n");
    printf("Usage: mapps <command> [options]\n\n");
    printf("Commands:\n");
    printf("  create <slug> [--name \"Name\"] [--author \"Author\"]   Scaffold a new app\n");
    printf("  build <app-dir> [--output file.mapp]                  Package app directory into .mapp\n");
    printf("  keygen <keyfile>                                      Generate Ed25519 keypair\n");
    printf("  sign <file.mapp> --key <keyfile>                      Sign a .mapp file\n");
    printf("  verify <file.mapp>                                    Verify .mapp signature\n");
    printf("  unpack <file.mapp> [--output <dir>]                   Extract .mapp to directory\n");
    printf("  install <file.mapp> [--port /dev/ttyXXX] [--reboot]  Upload .mapp to device over serial\n");
    printf("  list [--port /dev/ttyXXX]                             List installed apps on device\n");
    printf("  uninstall <slug> [--port /dev/ttyXXX] [--reboot]     Remove an app from device\n");
}

#ifndef PIO_UNIT_TESTING
int main(int argc, char **argv)
{
    if (argc < 2) {
        printUsage();
        return 1;
    }

    const char *cmd = argv[1];
    int subArgc = argc - 2;
    char **subArgv = argv + 2;

    if (strcmp(cmd, "create") == 0) {
        return cmdCreate(subArgc, subArgv);
    } else if (strcmp(cmd, "build") == 0) {
        return cmdBuild(subArgc, subArgv);
    } else if (strcmp(cmd, "keygen") == 0) {
        return cmdKeygen(subArgc, subArgv);
    } else if (strcmp(cmd, "sign") == 0) {
        return cmdSign(subArgc, subArgv);
    } else if (strcmp(cmd, "verify") == 0) {
        return cmdVerify(subArgc, subArgv);
    } else if (strcmp(cmd, "unpack") == 0) {
        return cmdUnpack(subArgc, subArgv);
    } else if (strcmp(cmd, "install") == 0) {
        return cmdInstall(subArgc, subArgv);
    } else if (strcmp(cmd, "list") == 0) {
        return cmdList(subArgc, subArgv);
    } else if (strcmp(cmd, "uninstall") == 0) {
        return cmdUninstall(subArgc, subArgv);
    } else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0) {
        printUsage();
        return 0;
    } else {
        fprintf(stderr, "Unknown command: %s\n\n", cmd);
        printUsage();
        return 1;
    }
}
#endif
