#pragma once

#include "mapps/AppStateBackend.h"

#include <map>
#include <string>

// CLI-only AppStateBackend implementation.
// Stores per-app state as simple key=value text files at a configurable base directory.
// File format: one "key=tagged_value" per line.
class PosixAppStateBackend : public AppStateBackend
{
  public:
    // baseDir is the directory where state files are stored (e.g. "./state/")
    explicit PosixAppStateBackend(const std::string &baseDir);

    std::string get(const std::string &appSlug, const std::string &key, bool &found) override;
    bool set(const std::string &appSlug, const std::string &key, const std::string &value) override;
    bool remove(const std::string &appSlug, const std::string &key) override;
    bool clear(const std::string &appSlug) override;

  private:
    std::string statePath(const std::string &appSlug) const;
    std::map<std::string, std::string> loadState(const std::string &appSlug);
    bool saveState(const std::string &appSlug, const std::map<std::string, std::string> &state);

    std::string baseDir;
};
