#include "PosixIO.h"
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace PosixIO {

bool readFile(const std::string &path, std::string &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool readFileBytes(const std::string &path, std::vector<uint8_t> &out)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        return false;
    auto size = f.tellg();
    f.seekg(0);
    out.resize(size);
    f.read(reinterpret_cast<char *>(out.data()), size);
    return f.good();
}

bool writeFile(const std::string &path, const std::string &content)
{
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;
    f.write(content.data(), content.size());
    return f.good();
}

bool writeFileBytes(const std::string &path, const std::vector<uint8_t> &data)
{
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;
    f.write(reinterpret_cast<const char *>(data.data()), data.size());
    return f.good();
}

bool fileExists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool dirExists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool mkdirp(const std::string &path)
{
    // Walk path components, creating each level
    std::string current;
    for (size_t i = 0; i < path.size(); i++) {
        current += path[i];
        if (path[i] == '/' || i == path.size() - 1) {
            if (!current.empty() && current != "/") {
                struct stat st;
                if (stat(current.c_str(), &st) != 0) {
                    if (mkdir(current.c_str(), 0755) != 0)
                        return false;
                }
            }
        }
    }
    return true;
}

bool listDirectory(const std::string &path, std::vector<std::string> &entries)
{
    entries.clear();
    DIR *dir = opendir(path.c_str());
    if (!dir)
        return false;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name != "." && name != "..")
            entries.push_back(name);
    }
    closedir(dir);
    return true;
}

std::string basename(const std::string &path)
{
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return path;
    return path.substr(pos + 1);
}

std::string dirname(const std::string &path)
{
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return ".";
    if (pos == 0)
        return "/";
    return path.substr(0, pos);
}

std::string joinPath(const std::string &a, const std::string &b)
{
    if (a.empty())
        return b;
    if (a.back() == '/')
        return a + b;
    return a + "/" + b;
}

} // namespace PosixIO
