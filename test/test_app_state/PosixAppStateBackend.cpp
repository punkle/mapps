#include "PosixAppStateBackend.h"
#include "PosixIO.h"

#include <sstream>

// Escape newlines and backslashes so each key=value fits on one line
static std::string escapeValue(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else
            out += c;
    }
    return out;
}

static std::string unescapeValue(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i + 1] == 'n') {
                out += '\n';
                i++;
            } else if (s[i + 1] == '\\') {
                out += '\\';
                i++;
            } else {
                out += s[i];
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

PosixAppStateBackend::PosixAppStateBackend(const std::string &dir)
    : baseDir(dir)
{
}

std::string PosixAppStateBackend::statePath(const std::string &appSlug) const
{
    return PosixIO::joinPath(baseDir, appSlug + ".state");
}

std::map<std::string, std::string> PosixAppStateBackend::loadState(const std::string &appSlug)
{
    std::map<std::string, std::string> state;
    std::string content;
    if (!PosixIO::readFile(statePath(appSlug), content))
        return state;

    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            state[line.substr(0, eq)] = unescapeValue(line.substr(eq + 1));
        }
    }
    return state;
}

bool PosixAppStateBackend::saveState(const std::string &appSlug,
                                     const std::map<std::string, std::string> &state)
{
    PosixIO::mkdirp(baseDir);
    std::string content;
    for (const auto &kv : state) {
        content += kv.first + "=" + escapeValue(kv.second) + "\n";
    }
    return PosixIO::writeFile(statePath(appSlug), content);
}

std::string PosixAppStateBackend::get(const std::string &appSlug, const std::string &key, bool &found)
{
    auto state = loadState(appSlug);
    auto it = state.find(key);
    if (it != state.end()) {
        found = true;
        return it->second;
    }
    found = false;
    return "";
}

bool PosixAppStateBackend::set(const std::string &appSlug, const std::string &key, const std::string &value)
{
    auto state = loadState(appSlug);
    state[key] = value;
    return saveState(appSlug, state);
}

bool PosixAppStateBackend::remove(const std::string &appSlug, const std::string &key)
{
    auto state = loadState(appSlug);
    state.erase(key);
    return saveState(appSlug, state);
}

bool PosixAppStateBackend::clear(const std::string &appSlug)
{
    std::map<std::string, std::string> empty;
    return saveState(appSlug, empty);
}
