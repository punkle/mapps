#pragma once

#include "mapps/FileBackend.h"
#include "mapps/MappManifest.h"
#include "mapps/MappTrustStore.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class AppStateBackend;
class BerryRuntime;
class AppRuntime;

enum class AppStatus {
    Discovered,        // Manifest parsed, no verification yet
    SignatureValid,    // Signature cryptographically verified, not yet user-approved
    SignatureFailed,   // Signature verification failed (terminal)
    SignatureApproved, // Developer signature approved by user/policy
    Ready,             // Signature + permissions approved -- launchable
    Unsigned,          // No signature file present
};

struct AppEntry {
    MappManifest manifest;
    AppStatus status = AppStatus::Discovered;

    // Signature verification state
    uint8_t developerPubKey[32] = {};
    std::string pubKeyFingerprint; // hex of first 8 bytes of pubkey
};

// Unified app lifecycle manager.
// Discovers apps from filesystem via injected FileBackend, verifies signatures,
// manages trust via TrustBackend, and creates Berry runtimes.
class AppLibrary
{
  public:
    // flash: FileBackend for on-device flash storage
    // sd:    FileBackend for SD card storage (may be nullptr)
    // trust: MappTrustStore for developer/permission management and persistence
    AppLibrary(std::shared_ptr<FileBackend> flash, std::shared_ptr<FileBackend> sd,
               std::shared_ptr<MappTrustStore> trust);

    // Scan flash /apps/ directory for available apps
    void discoverApps();

    // Scan SD card /apps/ directory for additional apps
    void discoverSDApps();

    // Query
    const std::vector<AppEntry> &getApps() const { return entries; }
    const AppEntry *getApp(int index) const;
    const AppEntry *getAppBySlug(const std::string &slug) const;
    int getIndexBySlug(const std::string &slug) const;
    bool isLaunchable(int index) const;
    int getLaunchableCount() const;

    // Verify all discovered apps at startup, removing unsigned/failed ones
    void verifyAllApps();

    // Approval chain
    bool verifySignature(int index);
    bool approveSignature(int index);
    bool approvePermissions(int index);
    bool ensureReady(int index);

    // Run crypto verification + trust store lookups, advancing status as far as
    // possible without user prompts.  Returns the resulting AppStatus.
    AppStatus verifyAndCheckTrust(int index);

    // Create a runtime for an app — returns abstract AppRuntime.
    // Caller adds bindings, then calls start(). Returns nullptr if app not found/not approved.
    AppRuntime *createRuntime(const std::string &slug, const std::string &entryPoint);

    // Tear down and remove a runtime
    void stopRuntime(const std::string &slug, const std::string &entryPoint);

    // Set app state backend (used for newly created runtimes)
    void setAppStateBackend(std::shared_ptr<AppStateBackend> backend) { appStateBackend = backend; }
    std::shared_ptr<AppStateBackend> getAppStateBackend() const { return appStateBackend; }

    // Persistence (uses flash FileBackend)
    void loadStatus();
    void saveStatus();

    // Load trust store from flash
    void loadTrustStore();

    // Save trust store to flash
    void saveTrustStore();

    // Access the trust store
    std::shared_ptr<MappTrustStore> getTrustStore() const { return trust; }

  private:
    std::vector<AppEntry> entries;
    std::shared_ptr<FileBackend> flash;
    std::shared_ptr<FileBackend> sd;
    std::shared_ptr<MappTrustStore> trust;
    std::shared_ptr<AppStateBackend> appStateBackend;
    std::map<std::string, std::unique_ptr<BerryRuntime>> activeRuntimes; // key: "slug:entryPoint"
    std::map<std::string, std::string> sourceCache; // key: "slug:entryFile" -> source code

    // Discover apps from a FileBackend at basePath, adding to entries
    void discoverFrom(FileBackend *fs, const char *basePath);
    void cacheSourceFiles(FileBackend *fs, const MappManifest &manifest);

    static const char *statusToString(AppStatus status);
    static AppStatus statusFromString(const std::string &str);
};
