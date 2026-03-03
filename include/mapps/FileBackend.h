#pragma once

#include <string>
#include <vector>

// Abstract interface for filesystem access.
// Consumers implement this for their platform (flash, SD card, POSIX, etc.).
class FileBackend
{
  public:
    virtual ~FileBackend() = default;

    struct DirEntry {
        std::string name;
        bool isDirectory;
    };

    // List entries in a directory. Returns empty vector on error.
    virtual std::vector<DirEntry> listDir(const char *path) = 0;

    // Read entire file contents. Returns empty string on error.
    virtual std::string readFile(const char *path) = 0;

    // Write data to a file (creates or overwrites). Returns true on success.
    virtual bool writeFile(const char *path, const std::string &data) = 0;
};
