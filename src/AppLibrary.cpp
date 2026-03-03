#include "mapps/AppLibrary.h"
#include "mapps/BerryRuntime.h"
#include "mapps/MappSignature.h"
#include "mapps/MappTrustStore.h"
#include "MappJson.h"

#include <cstring>

#ifdef ARDUINO
// On ESP32/Arduino, printf() goes to Serial and is visible in the console.
// The firmware's LOG_* macros (from DebugConfiguration.h) aren't usable here
// because they drag in the full firmware header chain.
#ifndef LOG_DEBUG
#define LOG_DEBUG(fmt, ...) printf("[DEBUG] [mapps] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_INFO
#define LOG_INFO(fmt, ...) printf("[INFO] [mapps] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_WARN
#define LOG_WARN(fmt, ...) printf("[WARN] [mapps] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(fmt, ...) printf("[ERROR] [mapps] " fmt "\n", ##__VA_ARGS__)
#endif
#else
#ifndef LOG_DEBUG
#define LOG_DEBUG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_INFO
#define LOG_INFO(fmt, ...) fprintf(stderr, "[INFO] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_WARN
#define LOG_WARN(fmt, ...) fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)
#endif
#endif

static const char *APPS_BASE_PATH = "/apps";
static const char *STATUS_FILE_PATH = "/apps_status.json";
static const char *TRUST_FILE_PATH = "/apps_trust.json";

AppLibrary::AppLibrary(std::shared_ptr<FileBackend> flash, std::shared_ptr<FileBackend> sd,
                       std::shared_ptr<MappTrustStore> trust)
    : flash(std::move(flash)), sd(std::move(sd)), trust(std::move(trust))
{
    loadTrustStore();
    loadStatus();
    discoverApps();
    saveStatus();
}

void AppLibrary::discoverApps()
{
    entries.clear();

    if (flash)
        discoverFrom(flash.get(), APPS_BASE_PATH);

    LOG_INFO("[AppLibrary] Discovered %u flash app(s)", (unsigned)entries.size());

    verifyAllApps();
}

void AppLibrary::discoverSDApps()
{
    if (!sd)
        return;

    LOG_INFO("[AppLibrary] Scanning SD card for apps...");
    size_t beforeCount = entries.size();
    discoverFrom(sd.get(), APPS_BASE_PATH);
    unsigned added = (unsigned)(entries.size() - beforeCount);

    LOG_INFO("[AppLibrary] Discovered %u SD card app(s)", added);
    if (added > 0) {
        verifyAllApps();
        saveStatus();
    }
}

void AppLibrary::discoverFrom(FileBackend *fs, const char *basePath)
{
    auto dirEntries = fs->listDir(basePath);
    LOG_DEBUG("[AppLibrary] listDir('%s') returned %u entries", basePath, (unsigned)dirEntries.size());
    for (const auto &de : dirEntries) {
        if (!de.isDirectory) {
            LOG_DEBUG("[AppLibrary] Skipping '%s' (not a directory)", de.name.c_str());
            continue;
        }

        // Skip if a flash app with the same slug already exists
        if (getIndexBySlug(de.name) >= 0) {
            LOG_DEBUG("[AppLibrary] Skipping '%s' (duplicate slug)", de.name.c_str());
            continue;
        }

        std::string dirPath = std::string(basePath) + "/" + de.name;
        std::string manifestPath = dirPath + "/app.json";

        std::string json = fs->readFile(manifestPath.c_str());
        if (json.empty()) {
            LOG_WARN("[AppLibrary] Skipping '%s': app.json is empty or missing", de.name.c_str());
            continue;
        }
        if (json.size() > 4096) {
            LOG_WARN("[AppLibrary] Skipping '%s': app.json too large (%u bytes)", de.name.c_str(),
                     (unsigned)json.size());
            continue;
        }

        MappManifest manifest;
        if (!parseManifest(json, manifest)) {
            LOG_WARN("[AppLibrary] Skipping '%s': failed to parse app.json", de.name.c_str());
            continue;
        }

        manifest.appPath = dirPath;
        manifest.slug = de.name;

        // Pre-cache source files while filesystem is accessible (avoids SPI contention at launch)
        cacheSourceFiles(fs, manifest);

        AppEntry entry;
        entry.manifest = std::move(manifest);
        entry.status = AppStatus::Discovered;
        entries.push_back(std::move(entry));
    }
}

void AppLibrary::cacheSourceFiles(FileBackend *fs, const MappManifest &manifest)
{
    for (const auto &ep : manifest.entries) {
        std::string cacheKey = manifest.slug + ":" + ep.second;
        if (sourceCache.count(cacheKey))
            continue;

        std::string filePath = manifest.appPath + "/" + ep.second;
        std::string content = fs->readFile(filePath.c_str());
        if (!content.empty()) {
            LOG_DEBUG("[AppLibrary] Cached source '%s' (%u bytes)", cacheKey.c_str(), (unsigned)content.size());
            sourceCache[cacheKey] = std::move(content);
        }
    }
}

const AppEntry *AppLibrary::getApp(int index) const
{
    if (index < 0 || index >= (int)entries.size())
        return nullptr;
    return &entries[index];
}

bool AppLibrary::isLaunchable(int index) const
{
    if (index < 0 || index >= (int)entries.size())
        return false;
    return entries[index].status == AppStatus::Ready;
}

int AppLibrary::getLaunchableCount() const
{
    int count = 0;
    for (const auto &entry : entries) {
        if (entry.status == AppStatus::Ready)
            count++;
    }
    return count;
}

void AppLibrary::verifyAllApps()
{
    // Log trusted developers
    if (trust) {
        const auto &devs = trust->getDevelopers();
        if (devs.empty()) {
            LOG_INFO("[AppLibrary] No trusted developers");
        } else {
            for (const auto &dev : devs) {
                LOG_INFO("[AppLibrary] Trusted developer: %s (%s...)", dev.name.c_str(),
                         dev.keyHex.substr(0, 16).c_str());
            }
        }
    }

    unsigned removed = 0;
    for (int i = 0; i < (int)entries.size(); /* increment in body */) {
        AppEntry &entry = entries[i];
        if (entry.status != AppStatus::Discovered) {
            LOG_INFO("[AppLibrary] App '%s': %s", entry.manifest.name.c_str(), statusToString(entry.status));
            i++;
            continue;
        }

        verifyAndCheckTrust(i);

        if (entry.status == AppStatus::Unsigned || entry.status == AppStatus::SignatureFailed) {
            LOG_WARN("[AppLibrary] App '%s': %s — removed from app list", entry.manifest.name.c_str(),
                     entry.status == AppStatus::Unsigned ? "unsigned" : "signature failed");
            entries.erase(entries.begin() + i);
            removed++;
        } else {
            LOG_INFO("[AppLibrary] App '%s': %s", entry.manifest.name.c_str(), statusToString(entry.status));
            i++;
        }
    }

    if (removed > 0) {
        LOG_INFO("[AppLibrary] Removed %u app(s) that failed signature verification", removed);
    }
}

bool AppLibrary::verifySignature(int index)
{
    if (index < 0 || index >= (int)entries.size())
        return false;

    AppEntry &entry = entries[index];

    // Already verified or beyond
    if (entry.status != AppStatus::Discovered)
        return entry.status != AppStatus::SignatureFailed && entry.status != AppStatus::Unsigned;

    MappManifest &manifest = entry.manifest;

    if (manifest.signature.empty()) {
        LOG_WARN("[AppLibrary] App '%s' is unsigned", manifest.name.c_str());
        entry.status = AppStatus::Unsigned;
        return false;
    }

    // Determine which FileBackend to use based on appPath
    // SD apps have paths starting with /apps and sd backend exists
    // For simplicity: try flash first, then sd
    FileBackend *fs = flash.get();
    std::string sigPath = manifest.appPath + "/" + manifest.signature;
    std::string sigData = fs ? fs->readFile(sigPath.c_str()) : "";
    if (sigData.empty() && sd) {
        fs = sd.get();
        sigData = fs->readFile(sigPath.c_str());
    }

    if (sigData.empty()) {
        LOG_WARN("[AppLibrary] Cannot read signature file: %s", sigPath.c_str());
        entry.status = AppStatus::SignatureFailed;
        return false;
    }

    // Read manifest JSON
    std::string manifestPath = manifest.appPath + "/app.json";
    std::string manifestJson = fs->readFile(manifestPath.c_str());
    if (manifestJson.empty()) {
        entry.status = AppStatus::SignatureFailed;
        return false;
    }

    // Read all entry files
    std::vector<std::pair<std::string, std::string>> fileContents;
    for (const auto &ep : manifest.entries) {
        std::string entryPath = manifest.appPath + "/" + ep.second;
        std::string entryContent = fs->readFile(entryPath.c_str());
        if (entryContent.empty()) {
            entry.status = AppStatus::SignatureFailed;
            return false;
        }
        fileContents.push_back({ep.second, entryContent});
    }

    // Parse the binary .sig file
    MappSignature::SigFileData sig =
        MappSignature::parseSigFile((const uint8_t *)sigData.data(), sigData.size());
    if (!sig.valid) {
        entry.status = AppStatus::SignatureFailed;
        LOG_WARN("[AppLibrary] Invalid signature file for '%s'", manifest.name.c_str());
        return false;
    }

    // Strip "signature" field from manifest JSON for digest computation
    std::string strippedJson = MappSignature::stripSignatureField(manifestJson);

    // Compute Blake2b-512 digest over stripped JSON + file contents
    uint8_t digest[MappSignature::DIGEST_SIZE];
    MappSignature::computeDigest(strippedJson, fileContents, digest);

    // Verify Ed25519 signature
    bool valid = MappSignature::verifySignature(sig.pubKey, sig.signature, digest);

    if (valid) {
        entry.status = AppStatus::SignatureValid;

        // Store pubkey info in entry
        memcpy(entry.developerPubKey, sig.pubKey, MappSignature::PUBKEY_SIZE);
        entry.pubKeyFingerprint = MappSignature::pubKeyFingerprint(sig.pubKey);

        // Auto-advance to SignatureApproved if developer already trusted
        if (trust && trust->isDeveloperTrusted(entry.developerPubKey)) {
            entry.status = AppStatus::SignatureApproved;
        }
        return true;
    } else {
        entry.status = AppStatus::SignatureFailed;
        LOG_WARN("[AppLibrary] Signature verification failed for '%s'", manifest.name.c_str());
        return false;
    }
}

bool AppLibrary::approveSignature(int index)
{
    if (index < 0 || index >= (int)entries.size())
        return false;

    AppEntry &entry = entries[index];

    if (entry.status != AppStatus::SignatureValid)
        return entry.status == AppStatus::SignatureApproved || entry.status == AppStatus::Ready;

    if (trust)
        trust->trustDeveloper(entry.developerPubKey, entry.manifest.author);
    entry.status = AppStatus::SignatureApproved;
    saveTrustStore();
    return true;
}

bool AppLibrary::approvePermissions(int index)
{
    if (index < 0 || index >= (int)entries.size())
        return false;

    AppEntry &entry = entries[index];

    if (entry.status != AppStatus::SignatureApproved)
        return entry.status == AppStatus::Ready;

    if (trust)
        trust->approvePermissions(entry.developerPubKey, entry.manifest.slug, entry.manifest.permissions);
    saveTrustStore();
    entry.status = AppStatus::Ready;
    saveStatus();
    return true;
}

AppStatus AppLibrary::verifyAndCheckTrust(int index)
{
    if (index < 0 || index >= (int)entries.size())
        return AppStatus::Discovered;

    AppEntry &entry = entries[index];

    // Already launchable or terminal
    if (entry.status == AppStatus::Ready || entry.status == AppStatus::SignatureFailed ||
        entry.status == AppStatus::Unsigned)
        return entry.status;

    // Run crypto verification (advances Discovered → SignatureValid/SignatureApproved/Unsigned/SignatureFailed)
    if (entry.status == AppStatus::Discovered) {
        verifySignature(index);
    }

    // If developer already trusted, verifySignature() auto-advanced to SignatureApproved.
    // Now check if permissions were also previously approved.
    if (entry.status == AppStatus::SignatureApproved) {
        if (trust &&
            trust->arePermissionsApproved(entry.developerPubKey, entry.manifest.slug, entry.manifest.permissions)) {
            entry.status = AppStatus::Ready;
            saveStatus();
        }
    }

    return entry.status;
}

bool AppLibrary::ensureReady(int index)
{
    if (index < 0 || index >= (int)entries.size())
        return false;

    AppStatus status = verifyAndCheckTrust(index);
    return status == AppStatus::Ready;
}

const AppEntry *AppLibrary::getAppBySlug(const std::string &slug) const
{
    int idx = getIndexBySlug(slug);
    return (idx >= 0) ? &entries[idx] : nullptr;
}

int AppLibrary::getIndexBySlug(const std::string &slug) const
{
    for (int i = 0; i < (int)entries.size(); i++) {
        if (entries[i].manifest.slug == slug)
            return i;
    }
    return -1;
}

AppRuntime *AppLibrary::createRuntime(const std::string &slug, const std::string &entryPoint)
{
    int index = getIndexBySlug(slug);
    if (index < 0) {
        LOG_ERROR("[AppLibrary] No app with slug '%s'", slug.c_str());
        return nullptr;
    }

    // Check launchable
    if (!isLaunchable(index)) {
        if (!ensureReady(index)) {
            LOG_WARN("[AppLibrary] App '%s' not approved", slug.c_str());
            return nullptr;
        }
    }

    const MappManifest &manifest = entries[index].manifest;
    std::string entryFile = manifest.getEntryFile(entryPoint);
    if (entryFile.empty()) {
        LOG_ERROR("[AppLibrary] No entry point '%s' in app '%s'", entryPoint.c_str(), slug.c_str());
        return nullptr;
    }

    std::string runtimeKey = slug + ":" + entryPoint;

    // Stop existing runtime for this key if any
    activeRuntimes.erase(runtimeKey);

    // Use cached source if available (pre-read during discovery), otherwise read now
    std::string cacheKey = slug + ":" + entryFile;
    std::string source;
    auto cacheIt = sourceCache.find(cacheKey);
    if (cacheIt != sourceCache.end()) {
        source = cacheIt->second;
        LOG_DEBUG("[AppLibrary] Using cached source for '%s' (%u bytes)", cacheKey.c_str(), (unsigned)source.size());
    } else {
        std::string sourcePath = manifest.appPath + "/" + entryFile;

        LOG_DEBUG("[AppLibrary] Reading source file '%s'", sourcePath.c_str());
        if (flash)
            source = flash->readFile(sourcePath.c_str());
        if (source.empty() && sd) {
            LOG_DEBUG("[AppLibrary] Flash read empty, trying SD for '%s'", sourcePath.c_str());
            source = sd->readFile(sourcePath.c_str());
        }
    }

    if (source.empty()) {
        LOG_ERROR("[AppLibrary] Cannot read entry file '%s' for app '%s'", entryFile.c_str(), slug.c_str());
        return nullptr;
    }

    auto runtime = std::unique_ptr<BerryRuntime>(new BerryRuntime(manifest.slug, manifest.permissions));
    runtime->setSource(source);

    if (appStateBackend)
        runtime->setAppStateBackend(appStateBackend);

    BerryRuntime *ptr = runtime.get();
    activeRuntimes[runtimeKey] = std::move(runtime);

    LOG_INFO("[AppLibrary] Created runtime for '%s' entry '%s'", slug.c_str(), entryPoint.c_str());
    return ptr;
}

void AppLibrary::stopRuntime(const std::string &slug, const std::string &entryPoint)
{
    std::string runtimeKey = slug + ":" + entryPoint;
    auto it = activeRuntimes.find(runtimeKey);
    if (it != activeRuntimes.end()) {
        it->second->stop();
        activeRuntimes.erase(it);
        LOG_INFO("[AppLibrary] Stopped '%s' entry '%s'", slug.c_str(), entryPoint.c_str());
    }
}

void AppLibrary::loadStatus()
{
    if (!flash)
        return;

    std::string json = flash->readFile(STATUS_FILE_PATH);
    if (json.empty())
        return;

    // The trust store handles its own loading -- this is for future extended status persistence
    LOG_DEBUG("[AppLibrary] Status file loaded (%u bytes)", (unsigned)json.size());
}

void AppLibrary::saveStatus()
{
    if (!flash)
        return;

    MappJson::JsonValue root;
    root.type = MappJson::JsonValue::Object;

    MappJson::JsonValue appsArr;
    appsArr.type = MappJson::JsonValue::Array;

    for (const auto &entry : entries) {
        const MappManifest &manifest = entry.manifest;

        MappJson::JsonValue obj;
        obj.type = MappJson::JsonValue::Object;
        obj.obj["name"] = MappJson::JsonValue(manifest.name);
        obj.obj["path"] = MappJson::JsonValue(manifest.appPath);
        obj.obj["version"] = MappJson::JsonValue(manifest.version);
        obj.obj["devkey"] = MappJson::JsonValue(entry.pubKeyFingerprint);
        obj.obj["status"] = MappJson::JsonValue(std::string(statusToString(entry.status)));

        MappJson::JsonValue permsArr;
        permsArr.type = MappJson::JsonValue::Array;
        for (const auto &p : manifest.permissions)
            permsArr.arr.push_back(MappJson::JsonValue(p));
        obj.obj["perms"] = permsArr;

        appsArr.arr.push_back(obj);
    }

    MappJson::JsonValue devArr;
    devArr.type = MappJson::JsonValue::Array;

    root.obj["developers"] = devArr;
    root.obj["apps"] = appsArr;

    std::string json = MappJson::serialize(root);
    flash->writeFile(STATUS_FILE_PATH, json);
    LOG_DEBUG("[AppLibrary] Status file saved");
}

void AppLibrary::loadTrustStore()
{
    if (!flash)
        return;

    if (!trust)
        return;

    std::string json = flash->readFile(TRUST_FILE_PATH);
    if (!json.empty()) {
        trust->parse(json);
        LOG_DEBUG("[AppLibrary] Trust store loaded");
    }
}

void AppLibrary::saveTrustStore()
{
    if (!flash)
        return;

    if (!trust)
        return;

    std::string json = trust->serialize();
    if (!json.empty()) {
        flash->writeFile(TRUST_FILE_PATH, json);
        LOG_DEBUG("[AppLibrary] Trust store saved");
    }
}

const char *AppLibrary::statusToString(AppStatus status)
{
    switch (status) {
    case AppStatus::Discovered:
        return "discovered";
    case AppStatus::SignatureValid:
        return "sig_valid";
    case AppStatus::SignatureFailed:
        return "sig_failed";
    case AppStatus::SignatureApproved:
        return "sig_approved";
    case AppStatus::Ready:
        return "ready";
    case AppStatus::Unsigned:
        return "unsigned";
    default:
        return "discovered";
    }
}

AppStatus AppLibrary::statusFromString(const std::string &str)
{
    if (str == "discovered")
        return AppStatus::Discovered;
    if (str == "sig_valid")
        return AppStatus::SignatureValid;
    if (str == "sig_failed")
        return AppStatus::SignatureFailed;
    if (str == "sig_approved")
        return AppStatus::SignatureApproved;
    if (str == "ready")
        return AppStatus::Ready;
    if (str == "unsigned")
        return AppStatus::Unsigned;
    return AppStatus::Discovered;
}
