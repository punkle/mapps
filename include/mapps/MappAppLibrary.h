#pragma once

#include "MappApp.h"
#include "TrustBackend.h"

#include <cstdint>
#include <memory>
#include <vector>

// High-level API for loading and verifying .mapp applications.
class MappAppLibrary
{
  public:
    explicit MappAppLibrary(std::shared_ptr<TrustBackend> backend);

    // Load a .mapp from raw bytes.
    // Returns nullptr only on structural parse failure (bad binary, missing app.json).
    // Unsigned or bad-signature apps return a valid MappApp with signatureInfo.valid == false.
    std::unique_ptr<MappApp> loadApp(const std::vector<uint8_t> &data);
    std::unique_ptr<MappApp> loadApp(const uint8_t *data, size_t size);

  private:
    std::shared_ptr<TrustBackend> backend;
};
