#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Abstract interface for trust and permission management.
// Consumers implement this for their platform (JSON file, NVS, database, etc.).
class TrustBackend
{
  public:
    virtual ~TrustBackend() = default;

    // Check if a developer's public key is trusted
    virtual bool isDeveloperTrusted(const uint8_t pubKey[32]) const = 0;

    // Add a developer as trusted. Returns true on success.
    virtual bool trustDeveloper(const uint8_t pubKey[32], const std::string &name) = 0;

    // Check if the exact set of permissions has been approved for this (signer, app) pair.
    // Permissions are sorted before comparison — ordering does not matter.
    virtual bool arePermissionsApproved(const uint8_t signerKey[32], const std::string &appSlug,
                                        const std::vector<std::string> &permissions) const = 0;

    // Approve the given permissions for this (signer, app) pair. Returns true on success.
    // Replaces any previous approval for the same (signer, app).
    virtual bool approvePermissions(const uint8_t signerKey[32], const std::string &appSlug,
                                    const std::vector<std::string> &permissions) = 0;
};
