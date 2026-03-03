#pragma once

#include <cstdint>
#include <string>
#include <vector>

// .mapp Binary Format (v1):
//   Header (7 bytes):
//     [0-3] Magic "MAPP" (0x4D415050, little-endian)
//     [4]   Version 0x01
//     [5-6] File count (uint16_t LE)
//   File entries (sequential, first MUST be "app.json"):
//     [0-1]   Filename length (uint16_t LE)
//     [2..N]  Filename (UTF-8, no null terminator)
//     [N..+4] Content length (uint32_t LE)
//     [+4..M] Content (raw bytes)

struct MappFile {
    std::string name;
    std::string content;
};

class MappFormat
{
  public:
    static constexpr uint32_t MAGIC = 0x4D415050; // "MAPP"
    static constexpr uint8_t VERSION = 0x01;
    static constexpr size_t HEADER_SIZE = 7;

    // Serialize a list of files into .mapp binary format
    static std::vector<uint8_t> serialize(const std::vector<MappFile> &files);

    // Deserialize .mapp binary data into a list of files
    // Returns false if the data is invalid
    static bool deserialize(const uint8_t *data, size_t size, std::vector<MappFile> &files);
    static bool deserialize(const std::vector<uint8_t> &data, std::vector<MappFile> &files);
};
