#pragma once

#include "TrustBackend.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// Keystore for trusted developer public keys and permission approvals.
// Stored as JSON with format matching firmware's apps_trust.json.

class MappTrustStore : public TrustBackend
{
  public:
    struct TrustedDeveloper {
        std::string keyHex; // full 64-char hex of public key
        std::string name;   // developer/author name
    };

    struct PermissionApproval {
        std::string signerHex; // full 64-char hex of signer's public key
        std::string appSlug;   // app identifier
        std::vector<std::string> permissions; // sorted
    };

    // Load keystore from JSON file. Returns false on error.
    bool load(const std::string &path);

    // Save keystore to JSON file. Returns false on error.
    bool save(const std::string &path) const;

    // Parse keystore from JSON string
    bool parse(const std::string &json);

    // Serialize keystore to JSON string
    std::string serialize() const;

    // Check if a public key is trusted
    bool isDeveloperTrusted(const uint8_t pubKey[32]) const override;

    // Add a trusted developer as trusted. Returns true on success.
    bool trustDeveloper(const uint8_t pubKey[32], const std::string &name) override;

    // Add a trusted developer (no-op if already present) — legacy API
    void addDeveloper(const uint8_t pubKey[32], const std::string &name);

    // Check if permissions are approved for a (signer, app) pair
    bool arePermissionsApproved(const uint8_t signerKey[32], const std::string &appSlug,
                                const std::vector<std::string> &permissions) const override;

    // Approve permissions for a (signer, app) pair
    bool approvePermissions(const uint8_t signerKey[32], const std::string &appSlug,
                            const std::vector<std::string> &permissions) override;

    const std::vector<TrustedDeveloper> &getDevelopers() const { return developers; }
    const std::vector<PermissionApproval> &getApprovals() const { return approvals; }

  private:
    std::vector<TrustedDeveloper> developers;
    std::vector<PermissionApproval> approvals;
};
