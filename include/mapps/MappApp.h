#pragma once

#include "MappFormat.h"
#include "MappManifest.h"
#include "MappSignature.h"
#include "TrustBackend.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

class MappAppLibrary;

// Value object wrapping signature data for a loaded app.
class MappSignatureInfo
{
    friend class MappAppLibrary;
    friend class MappApp;

  public:
    // Was the cryptographic signature verification successful?
    bool isValid() const { return valid; }

    // Is the signer's key trusted in the trust backend?
    bool isTrusted() const
    {
        if (!valid || !backend)
            return false;
        return backend->isDeveloperTrusted(pubKey);
    }

    // Trust the signer in the trust backend. Returns false if signature is invalid.
    bool trust(const std::string &name = "")
    {
        if (!valid || !backend)
            return false;
        return backend->trustDeveloper(pubKey, name);
    }

    // First 8 bytes of public key as 16 hex characters
    std::string fingerprint() const
    {
        if (!valid)
            return "";
        return MappSignature::pubKeyFingerprint(pubKey);
    }

    // Full 32-byte public key as 64 hex characters
    std::string signerKeyHex() const
    {
        if (!valid)
            return "";
        return MappSignature::pubKeyToHex(pubKey);
    }

  private:
    bool valid = false;
    uint8_t pubKey[MappSignature::PUBKEY_SIZE] = {};
    std::shared_ptr<TrustBackend> backend;
};

// A loaded .mapp application with verification state.
class MappApp
{
    friend class MappAppLibrary;

  public:
    // True iff signature is valid AND signer is trusted AND permissions are approved
    bool isVerified() const
    {
        if (!signatureInfo.isValid() || !signatureInfo.isTrusted())
            return false;
        return arePermissionsApproved();
    }

    const MappManifest &getManifest() const { return manifest; }
    const std::vector<MappFile> &getFiles() const { return files; }

    // App slug (derived from manifest name, lowercase with hyphens)
    const std::string &getSlug() const { return slug; }

    MappSignatureInfo &getSignature() { return signatureInfo; }
    const MappSignatureInfo &getSignature() const { return signatureInfo; }

    // Approve the app's requested permissions for this signer.
    // Returns false if signature is invalid.
    bool approvePermissions()
    {
        if (!signatureInfo.isValid() || !backend)
            return false;
        return backend->approvePermissions(signatureInfo.pubKey, slug, manifest.permissions);
    }

    // Check if the app's requested permissions are approved for this signer.
    bool arePermissionsApproved() const
    {
        if (!signatureInfo.isValid() || !backend)
            return false;
        return backend->arePermissionsApproved(signatureInfo.pubKey, slug, manifest.permissions);
    }

    // Get the source code for a given entry point (e.g., "bui").
    // Returns empty string if not found.
    std::string getEntrySource(const std::string &entryPoint) const
    {
        return manifest.getEntrySource(entryPoint, files);
    }

  private:
    MappManifest manifest;
    std::vector<MappFile> files;
    std::string slug;
    MappSignatureInfo signatureInfo;
    std::shared_ptr<TrustBackend> backend;
};
