#include "mapps/MappFormat.h"
#include <cstring>

static void writeU16LE(std::vector<uint8_t> &out, uint16_t val)
{
    out.push_back(val & 0xFF);
    out.push_back((val >> 8) & 0xFF);
}

static void writeU32LE(std::vector<uint8_t> &out, uint32_t val)
{
    out.push_back(val & 0xFF);
    out.push_back((val >> 8) & 0xFF);
    out.push_back((val >> 16) & 0xFF);
    out.push_back((val >> 24) & 0xFF);
}

static uint16_t readU16LE(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t readU32LE(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

std::vector<uint8_t> MappFormat::serialize(const std::vector<MappFile> &files)
{
    if (files.size() > 0xFFFF)
        return {}; // file count must fit in uint16_t

    std::vector<uint8_t> out;

    // Header: magic + version + file count
    writeU32LE(out, MAGIC);
    out.push_back(VERSION);
    writeU16LE(out, (uint16_t)files.size());

    // File entries
    for (const auto &f : files) {
        if (f.name.size() > 0xFFFF)
            return {}; // filename length must fit in uint16_t
        if (f.content.size() > 0xFFFFFFFF)
            return {}; // content length must fit in uint32_t
        writeU16LE(out, (uint16_t)f.name.size());
        out.insert(out.end(), f.name.begin(), f.name.end());
        writeU32LE(out, (uint32_t)f.content.size());
        out.insert(out.end(), f.content.begin(), f.content.end());
    }

    return out;
}

bool MappFormat::deserialize(const uint8_t *data, size_t size, std::vector<MappFile> &files)
{
    files.clear();

    if (!data || size < HEADER_SIZE)
        return false;

    uint32_t magic = readU32LE(data);
    if (magic != MAGIC)
        return false;

    if (data[4] != VERSION)
        return false;

    uint16_t fileCount = readU16LE(data + 5);
    size_t pos = HEADER_SIZE;

    for (uint16_t i = 0; i < fileCount; i++) {
        if (pos + 2 > size)
            return false;
        uint16_t nameLen = readU16LE(data + pos);
        pos += 2;

        if (pos + nameLen > size)
            return false;
        std::string name((const char *)(data + pos), nameLen);
        pos += nameLen;

        if (pos + 4 > size)
            return false;
        uint32_t contentLen = readU32LE(data + pos);
        pos += 4;

        if (pos + contentLen > size)
            return false;
        std::string content((const char *)(data + pos), contentLen);
        pos += contentLen;

        files.push_back({name, content});
    }

    return true;
}

bool MappFormat::deserialize(const std::vector<uint8_t> &data, std::vector<MappFile> &files)
{
    return deserialize(data.data(), data.size(), files);
}
