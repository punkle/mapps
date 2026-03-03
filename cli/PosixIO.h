#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace PosixIO {

// Read entire file into a string. Returns false on error.
bool readFile(const std::string &path, std::string &out);

// Read entire file into a byte vector. Returns false on error.
bool readFileBytes(const std::string &path, std::vector<uint8_t> &out);

// Write string to file. Returns false on error.
bool writeFile(const std::string &path, const std::string &content);

// Write bytes to file. Returns false on error.
bool writeFileBytes(const std::string &path, const std::vector<uint8_t> &data);

// Check if a file exists
bool fileExists(const std::string &path);

// Check if a directory exists
bool dirExists(const std::string &path);

// Create a directory (and parents). Returns false on error.
bool mkdirp(const std::string &path);

// List files in a directory (non-recursive). Returns filenames only, not full paths.
bool listDirectory(const std::string &path, std::vector<std::string> &entries);

// Get the filename from a path (basename)
std::string basename(const std::string &path);

// Get directory part of a path
std::string dirname(const std::string &path);

// Join path components
std::string joinPath(const std::string &a, const std::string &b);

} // namespace PosixIO
