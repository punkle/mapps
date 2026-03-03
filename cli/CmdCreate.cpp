#include "CmdCreate.h"
#include "PosixIO.h"
#include "mapps/MappManifest.h"
#include <cstdio>
#include <cstring>
#include <string>

int cmdCreate(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: mapps create <slug> [--name \"Name\"] [--author \"Author\"]\n");
        return 1;
    }

    std::string slug = argv[0];
    std::string name = slug;
    std::string author = "Unknown";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else if (strcmp(argv[i], "--author") == 0 && i + 1 < argc) {
            author = argv[++i];
        }
    }

    if (PosixIO::dirExists(slug)) {
        fprintf(stderr, "Error: directory '%s' already exists\n", slug.c_str());
        return 1;
    }

    if (!PosixIO::mkdirp(slug)) {
        fprintf(stderr, "Error: failed to create directory '%s'\n", slug.c_str());
        return 1;
    }

    // Create app.json manifest
    MappManifest manifest;
    manifest.name = name;
    manifest.version = "0.1";
    manifest.author = author;
    manifest.entries["bui"] = "bui.be";
    manifest.entries["mui"] = "mui.be";

    std::string manifestJson = serializeManifest(manifest);
    std::string manifestPath = PosixIO::joinPath(slug, "app.json");
    if (!PosixIO::writeFile(manifestPath, manifestJson)) {
        fprintf(stderr, "Error: failed to write %s\n", manifestPath.c_str());
        return 1;
    }

    // Create bui.be — built-in UI entry point (firmware display bindings)
    std::string buiBe =
        "# " + name + " - Built-in UI\n"
        "\n"
        "def draw()\n"
        "  var w = display.width()\n"
        "  var h = display.height()\n"
        "  display.draw_string(w / 2 - 30, h / 2 - 5, 'Hello, World!')\n"
        "end\n";
    std::string buiPath = PosixIO::joinPath(slug, "bui.be");
    if (!PosixIO::writeFile(buiPath, buiBe)) {
        fprintf(stderr, "Error: failed to write %s\n", buiPath.c_str());
        return 1;
    }

    // Create mui.be — Meshtastic UI entry point (LVGL bindings via device-ui)
    std::string muiBe =
        "# " + name + " - Meshtastic UI\n"
        "\n"
        "var root = ui.root()\n"
        "\n"
        "var label = ui.label(root, 'Hello, World!')\n"
        "ui.set_pos(label, 10, 10)\n"
        "\n"
        "var count = 0\n"
        "var btn = ui.button(root, 'Tap me')\n"
        "ui.set_pos(btn, 10, 40)\n"
        "ui.set_size(btn, 120, 40)\n"
        "ui.on_click(btn, def ()\n"
        "  count += 1\n"
        "  ui.set_text(label, 'Tapped ' + str(count) + ' times')\n"
        "end)\n";
    std::string muiPath = PosixIO::joinPath(slug, "mui.be");
    if (!PosixIO::writeFile(muiPath, muiBe)) {
        fprintf(stderr, "Error: failed to write %s\n", muiPath.c_str());
        return 1;
    }

    printf("Created app '%s' in %s/\n", name.c_str(), slug.c_str());
    printf("  %s\n", manifestPath.c_str());
    printf("  %s\n", buiPath.c_str());
    printf("  %s\n", muiPath.c_str());
    return 0;
}
