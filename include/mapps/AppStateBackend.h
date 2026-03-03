#pragma once

#include <string>

// Abstract storage interface for app state persistence.
// All types are stored as tagged strings (e.g. "i:42", "f:3.14", "b:1", "s:hello").
// The backend is type-agnostic — encoding/decoding happens in AppStateBinding.
class AppStateBackend
{
  public:
    virtual ~AppStateBackend() = default;

    virtual std::string get(const std::string &appSlug, const std::string &key, bool &found) = 0;
    virtual bool set(const std::string &appSlug, const std::string &key, const std::string &value) = 0;
    virtual bool remove(const std::string &appSlug, const std::string &key) = 0;
    virtual bool clear(const std::string &appSlug) = 0;
};
