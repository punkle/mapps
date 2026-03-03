#include <mapps/MappAppLibrary.h>
#include <mapps/MappFormat.h>
#include <mapps/MappManifest.h>
#include <mapps/MappSignature.h>
#include <mapps/MappTrustStore.h>
#include <mapps/TrustBackend.h>
#include <unity.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// --- In-memory TrustBackend for testing ---

class InMemoryTrustBackend : public TrustBackend
{
  public:
    struct Developer {
        std::string keyHex;
        std::string name;
    };

    struct Approval {
        std::string signerHex;
        std::string appSlug;
        std::vector<std::string> permissions; // sorted
    };

    bool isDeveloperTrusted(const uint8_t pubKey[32]) const override
    {
        std::string hex = MappSignature::pubKeyToHex(pubKey);
        for (const auto &d : developers) {
            if (d.keyHex == hex)
                return true;
        }
        return false;
    }

    bool trustDeveloper(const uint8_t pubKey[32], const std::string &name) override
    {
        std::string hex = MappSignature::pubKeyToHex(pubKey);
        for (const auto &d : developers) {
            if (d.keyHex == hex)
                return true;
        }
        developers.push_back({hex, name});
        return true;
    }

    bool arePermissionsApproved(const uint8_t signerKey[32], const std::string &appSlug,
                                const std::vector<std::string> &permissions) const override
    {
        std::string hex = MappSignature::pubKeyToHex(signerKey);
        std::vector<std::string> sorted = permissions;
        std::sort(sorted.begin(), sorted.end());

        for (const auto &a : approvedPerms) {
            if (a.signerHex == hex && a.appSlug == appSlug)
                return a.permissions == sorted;
        }
        return sorted.empty();
    }

    bool approvePermissions(const uint8_t signerKey[32], const std::string &appSlug,
                            const std::vector<std::string> &permissions) override
    {
        std::string hex = MappSignature::pubKeyToHex(signerKey);
        std::vector<std::string> sorted = permissions;
        std::sort(sorted.begin(), sorted.end());

        for (auto &a : approvedPerms) {
            if (a.signerHex == hex && a.appSlug == appSlug) {
                a.permissions = sorted;
                return true;
            }
        }
        approvedPerms.push_back({hex, appSlug, sorted});
        return true;
    }

    std::vector<Developer> developers;
    std::vector<Approval> approvedPerms;
};

// --- Test helpers ---

// Deterministic test private key (32 bytes)
static const uint8_t TEST_PRIVKEY[32] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                                          0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
                                          0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20};

// A second deterministic key for testing signer scoping
static const uint8_t TEST_PRIVKEY2[32] = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
                                           0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
                                           0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40};

// Build a signed .mapp binary in memory
static std::vector<uint8_t> buildSignedMapp(const std::string &name, const std::string &version,
                                            const std::string &author, const std::string &berrySource,
                                            const std::vector<std::string> &permissions,
                                            const uint8_t privKey[32])
{
    // Derive public key
    uint8_t pubKey[MappSignature::PUBKEY_SIZE];
    MappSignature::derivePublicKey(privKey, pubKey);

    // Build manifest
    MappManifest manifest;
    manifest.name = name;
    manifest.version = version;
    manifest.author = author;
    manifest.entries["bui"] = "main.be";
    manifest.permissions = permissions;
    manifest.signature = "app.sig";

    std::string manifestJson = serializeManifest(manifest);

    // Prepare files for digest (excluding app.json and .sig)
    std::vector<std::pair<std::string, std::string>> fileContents;
    fileContents.push_back({"main.be", berrySource});

    // Compute digest over stripped manifest + files
    std::string strippedJson = MappSignature::stripSignatureField(manifestJson);
    uint8_t digest[MappSignature::DIGEST_SIZE];
    MappSignature::computeDigest(strippedJson, fileContents, digest);

    // Sign
    uint8_t signature[MappSignature::SIGNATURE_SIZE];
    MappSignature::signDigest(privKey, pubKey, digest, signature);

    // Build .sig file
    std::vector<uint8_t> sigFile = MappSignature::buildSigFile(pubKey, signature);

    // Assemble .mapp files
    std::vector<MappFile> files;
    files.push_back({"app.json", manifestJson});
    files.push_back({"main.be", berrySource});
    files.push_back({"app.sig", std::string(sigFile.begin(), sigFile.end())});

    return MappFormat::serialize(files);
}

// Build an unsigned .mapp binary in memory
static std::vector<uint8_t> buildUnsignedMapp(const std::string &name, const std::string &berrySource,
                                              const std::vector<std::string> &permissions)
{
    MappManifest manifest;
    manifest.name = name;
    manifest.version = "0.1";
    manifest.author = "Test";
    manifest.entries["bui"] = "main.be";
    manifest.permissions = permissions;

    std::vector<MappFile> files;
    files.push_back({"app.json", serializeManifest(manifest)});
    files.push_back({"main.be", berrySource});

    return MappFormat::serialize(files);
}

// --- Tests ---

static std::shared_ptr<InMemoryTrustBackend> backend;
static std::shared_ptr<MappAppLibrary> library;

void setUp()
{
    backend = std::make_shared<InMemoryTrustBackend>();
    library = std::make_shared<MappAppLibrary>(backend);
}

void tearDown() {}

static void test_full_lifecycle()
{
    auto data = buildSignedMapp("Weather", "1.0", "Alice", "def init() end\n", {"http-client"}, TEST_PRIVKEY);
    auto app = library->loadApp(data);

    TEST_ASSERT_NOT_NULL(app.get());
    TEST_ASSERT_TRUE(app->getSignature().isValid());
    TEST_ASSERT_FALSE(app->isVerified()); // signer not yet trusted

    app->getSignature().trust("Alice");
    TEST_ASSERT_TRUE(app->getSignature().isTrusted());
    TEST_ASSERT_FALSE(app->isVerified()); // permissions not yet approved

    app->approvePermissions();
    TEST_ASSERT_TRUE(app->arePermissionsApproved());
    TEST_ASSERT_TRUE(app->isVerified()); // all checks pass
}

static void test_trust_persists_across_apps()
{
    auto data1 = buildSignedMapp("App One", "1.0", "Alice", "def init() end\n", {}, TEST_PRIVKEY);
    auto data2 = buildSignedMapp("App Two", "1.0", "Alice", "def draw() end\n", {}, TEST_PRIVKEY);

    auto app1 = library->loadApp(data1);
    app1->getSignature().trust("Alice");

    auto app2 = library->loadApp(data2);
    TEST_ASSERT_TRUE(app2->getSignature().isTrusted()); // same signer, already trusted
}

static void test_permissions_scoped_by_signer()
{
    auto data1 = buildSignedMapp("Weather", "1.0", "Alice", "def init() end\n", {"http-client"}, TEST_PRIVKEY);
    auto data2 = buildSignedMapp("Weather", "1.0", "Bob", "def init() end\n", {"http-client"}, TEST_PRIVKEY2);

    auto app1 = library->loadApp(data1);
    app1->getSignature().trust("Alice");
    app1->approvePermissions();
    TEST_ASSERT_TRUE(app1->isVerified());

    // Same app name, different signer — not approved
    auto app2 = library->loadApp(data2);
    app2->getSignature().trust("Bob");
    TEST_ASSERT_FALSE(app2->arePermissionsApproved());
    TEST_ASSERT_FALSE(app2->isVerified());
}

static void test_permissions_scoped_by_app_slug()
{
    auto data1 = buildSignedMapp("Weather", "1.0", "Alice", "def init() end\n", {"http-client"}, TEST_PRIVKEY);
    auto data2 = buildSignedMapp("Tracker", "1.0", "Alice", "def init() end\n", {"http-client"}, TEST_PRIVKEY);

    auto app1 = library->loadApp(data1);
    app1->getSignature().trust("Alice");
    app1->approvePermissions();
    TEST_ASSERT_TRUE(app1->isVerified());

    // Different app slug, same signer — not approved
    auto app2 = library->loadApp(data2);
    TEST_ASSERT_TRUE(app2->getSignature().isTrusted()); // same signer
    TEST_ASSERT_FALSE(app2->arePermissionsApproved());   // different slug
}

static void test_permission_change_invalidates_approval()
{
    auto dataV1 = buildSignedMapp("Weather", "1.0", "Alice", "def init() end\n", {"http-client"}, TEST_PRIVKEY);
    auto appV1 = library->loadApp(dataV1);
    appV1->getSignature().trust("Alice");
    appV1->approvePermissions();
    TEST_ASSERT_TRUE(appV1->isVerified());

    // New version adds a permission
    auto dataV2 =
        buildSignedMapp("Weather", "2.0", "Alice", "def init() end\n", {"http-client", "bluetooth"}, TEST_PRIVKEY);
    auto appV2 = library->loadApp(dataV2);
    TEST_ASSERT_TRUE(appV2->getSignature().isTrusted());
    TEST_ASSERT_FALSE(appV2->arePermissionsApproved()); // new permission set != old
    TEST_ASSERT_FALSE(appV2->isVerified());
}

static void test_unsigned_app()
{
    auto data = buildUnsignedMapp("Simple", "def init() end\n", {});
    auto app = library->loadApp(data);

    TEST_ASSERT_NOT_NULL(app.get());
    TEST_ASSERT_FALSE(app->getSignature().isValid());
    TEST_ASSERT_FALSE(app->getSignature().trust("Someone"));
    TEST_ASSERT_FALSE(app->approvePermissions());
    TEST_ASSERT_FALSE(app->isVerified());
}

static void test_corrupted_signature()
{
    auto data = buildSignedMapp("Weather", "1.0", "Alice", "def init() end\n", {}, TEST_PRIVKEY);

    // Corrupt one byte in the .mapp data (somewhere in the signature area, near the end)
    data[data.size() - 10] ^= 0xFF;

    auto app = library->loadApp(data);
    // Might be nullptr if corruption hit the binary structure, or might load but with invalid sig
    if (app) {
        TEST_ASSERT_FALSE(app->getSignature().isValid());
        TEST_ASSERT_FALSE(app->isVerified());
    }
}

static void test_get_entry_source()
{
    std::string source = "def init() return 42 end\ndef draw() end\n";
    auto data = buildSignedMapp("Weather", "1.0", "Alice", source, {}, TEST_PRIVKEY);
    auto app = library->loadApp(data);

    TEST_ASSERT_NOT_NULL(app.get());
    TEST_ASSERT_EQUAL_STRING(source.c_str(), app->getEntrySource("bui").c_str());
    TEST_ASSERT_EQUAL_STRING("", app->getEntrySource("nonexistent").c_str());
}

static void test_manifest_access()
{
    auto data = buildSignedMapp("Weather", "1.0", "Alice", "def init() end\n", {"http-client"}, TEST_PRIVKEY);
    auto app = library->loadApp(data);

    TEST_ASSERT_NOT_NULL(app.get());
    TEST_ASSERT_EQUAL_STRING("Weather", app->getManifest().name.c_str());
    TEST_ASSERT_EQUAL_STRING("1.0", app->getManifest().version.c_str());
    TEST_ASSERT_EQUAL_STRING("Alice", app->getManifest().author.c_str());
    TEST_ASSERT_EQUAL(1, app->getManifest().permissions.size());
    TEST_ASSERT_EQUAL_STRING("http-client", app->getManifest().permissions[0].c_str());
}

static void test_slug_derivation()
{
    auto data = buildSignedMapp("My Weather App", "1.0", "Alice", "def init() end\n", {}, TEST_PRIVKEY);
    auto app = library->loadApp(data);

    TEST_ASSERT_NOT_NULL(app.get());
    TEST_ASSERT_EQUAL_STRING("my-weather-app", app->getSlug().c_str());
}

static void test_null_on_bad_binary()
{
    std::vector<uint8_t> garbage = {0x00, 0x01, 0x02, 0x03};
    auto app = library->loadApp(garbage);
    TEST_ASSERT_NULL(app.get());
}

static void test_truststore_approval_roundtrip()
{
    uint8_t pubKey[32];
    MappSignature::derivePublicKey(TEST_PRIVKEY, pubKey);

    MappTrustStore store;
    store.trustDeveloper(pubKey, "Alice");
    store.approvePermissions(pubKey, "weather", {"http-client"});

    std::string json = store.serialize();

    MappTrustStore store2;
    TEST_ASSERT_TRUE(store2.parse(json));
    TEST_ASSERT_TRUE(store2.isDeveloperTrusted(pubKey));
    TEST_ASSERT_TRUE(store2.arePermissionsApproved(pubKey, "weather", {"http-client"}));
    TEST_ASSERT_FALSE(store2.arePermissionsApproved(pubKey, "weather", {"http-client", "bluetooth"}));
    TEST_ASSERT_FALSE(store2.arePermissionsApproved(pubKey, "other-app", {"http-client"}));
}

static void test_truststore_backward_compat()
{
    // Old JSON format without "approvals" field
    std::string oldJson = R"({"developers":[{"key":"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20","name":"Alice"}]})";

    MappTrustStore store;
    TEST_ASSERT_TRUE(store.parse(oldJson));
    TEST_ASSERT_EQUAL(1, store.getDevelopers().size());
    TEST_ASSERT_EQUAL(0, store.getApprovals().size());

    // Can still serialize with approvals
    store.approvePermissions(
        reinterpret_cast<const uint8_t *>("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10"
                                          "\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f\x20"),
        "weather", {"http-client"});

    std::string json = store.serialize();
    MappTrustStore store2;
    TEST_ASSERT_TRUE(store2.parse(json));
    TEST_ASSERT_EQUAL(1, store2.getApprovals().size());
}

static void test_empty_permissions_auto_approved()
{
    auto data = buildSignedMapp("Simple", "1.0", "Alice", "def init() end\n", {}, TEST_PRIVKEY);
    auto app = library->loadApp(data);

    app->getSignature().trust("Alice");
    // No permissions means auto-approved
    TEST_ASSERT_TRUE(app->arePermissionsApproved());
    TEST_ASSERT_TRUE(app->isVerified());
}

static void test_signature_fingerprint()
{
    auto data = buildSignedMapp("Weather", "1.0", "Alice", "def init() end\n", {}, TEST_PRIVKEY);
    auto app = library->loadApp(data);

    TEST_ASSERT_NOT_NULL(app.get());
    TEST_ASSERT_TRUE(app->getSignature().isValid());

    std::string fp = app->getSignature().fingerprint();
    TEST_ASSERT_EQUAL(16, fp.size()); // 8 bytes = 16 hex chars

    std::string fullHex = app->getSignature().signerKeyHex();
    TEST_ASSERT_EQUAL(64, fullHex.size()); // 32 bytes = 64 hex chars

    // Fingerprint is the first 16 chars of the full hex
    TEST_ASSERT_EQUAL_STRING(fullHex.substr(0, 16).c_str(), fp.c_str());
}

// Helper that re-signs an already-signed .mapp binary (mirrors cmdSign logic)
static std::vector<uint8_t> resignMapp(const std::vector<uint8_t> &signedData, const uint8_t privKey[32])
{
    // Derive public key
    uint8_t pubKey[MappSignature::PUBKEY_SIZE];
    MappSignature::derivePublicKey(privKey, pubKey);

    // Deserialize existing .mapp
    std::vector<MappFile> files;
    TEST_ASSERT_TRUE(MappFormat::deserialize(signedData, files));
    TEST_ASSERT_TRUE(files.size() >= 1);
    TEST_ASSERT_EQUAL_STRING("app.json", files[0].name.c_str());

    // Parse manifest (which already has "signature" field from first signing)
    MappManifest manifest;
    TEST_ASSERT_TRUE(parseManifest(files[0].content, manifest));
    TEST_ASSERT_FALSE(manifest.signature.empty()); // was already signed

    // This is the cmdSign code path: clear signature before computing canonical JSON
    manifest.signature.clear();
    std::string canonicalJson = serializeManifest(manifest);

    // Collect non-manifest, non-sig files for digest
    std::vector<std::pair<std::string, std::string>> fileContents;
    for (size_t i = 1; i < files.size(); i++) {
        if (files[i].name.size() >= 4 && files[i].name.substr(files[i].name.size() - 4) == ".sig")
            continue;
        fileContents.push_back({files[i].name, files[i].content});
    }

    uint8_t digest[MappSignature::DIGEST_SIZE];
    MappSignature::computeDigest(canonicalJson, fileContents, digest);

    uint8_t signature[MappSignature::SIGNATURE_SIZE];
    MappSignature::signDigest(privKey, pubKey, digest, signature);

    std::vector<uint8_t> sigFile = MappSignature::buildSigFile(pubKey, signature);

    // Rebuild .mapp: remove old .sig, update manifest with new signature
    std::string sigName = "app.sig";
    manifest.signature = sigName;
    std::vector<MappFile> newFiles;
    for (auto &f : files) {
        if (f.name.size() >= 4 && f.name.substr(f.name.size() - 4) == ".sig")
            continue;
        if (f.name == "app.json")
            f.content = serializeManifest(manifest);
        newFiles.push_back(f);
    }
    newFiles.push_back({sigName, std::string(sigFile.begin(), sigFile.end())});

    return MappFormat::serialize(newFiles);
}

static void test_resign_produces_verifiable_package()
{
    // Sign once
    auto data = buildSignedMapp("Weather", "1.0", "Alice", "def init() end\n", {"http-client"}, TEST_PRIVKEY);
    auto app = library->loadApp(data);
    TEST_ASSERT_NOT_NULL(app.get());
    TEST_ASSERT_TRUE(app->getSignature().isValid());

    // Re-sign with same key
    auto data2 = resignMapp(data, TEST_PRIVKEY);
    auto app2 = library->loadApp(data2);
    TEST_ASSERT_NOT_NULL(app2.get());
    TEST_ASSERT_TRUE_MESSAGE(app2->getSignature().isValid(), "re-signed .mapp should verify");
}

static void test_resign_with_different_key()
{
    // Sign once with key 1
    auto data = buildSignedMapp("Weather", "1.0", "Alice", "def init() end\n", {}, TEST_PRIVKEY);
    auto app = library->loadApp(data);
    TEST_ASSERT_TRUE(app->getSignature().isValid());

    // Re-sign with key 2
    auto data2 = resignMapp(data, TEST_PRIVKEY2);
    auto app2 = library->loadApp(data2);
    TEST_ASSERT_NOT_NULL(app2.get());
    TEST_ASSERT_TRUE_MESSAGE(app2->getSignature().isValid(), "re-signed with different key should verify");

    // Signer should now be key 2
    uint8_t pubKey2[MappSignature::PUBKEY_SIZE];
    MappSignature::derivePublicKey(TEST_PRIVKEY2, pubKey2);
    std::string expected = MappSignature::pubKeyFingerprint(pubKey2);
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), app2->getSignature().fingerprint().c_str());
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_full_lifecycle);
    RUN_TEST(test_trust_persists_across_apps);
    RUN_TEST(test_permissions_scoped_by_signer);
    RUN_TEST(test_permissions_scoped_by_app_slug);
    RUN_TEST(test_permission_change_invalidates_approval);
    RUN_TEST(test_unsigned_app);
    RUN_TEST(test_corrupted_signature);
    RUN_TEST(test_get_entry_source);
    RUN_TEST(test_manifest_access);
    RUN_TEST(test_slug_derivation);
    RUN_TEST(test_null_on_bad_binary);
    RUN_TEST(test_truststore_approval_roundtrip);
    RUN_TEST(test_truststore_backward_compat);
    RUN_TEST(test_empty_permissions_auto_approved);
    RUN_TEST(test_signature_fingerprint);
    RUN_TEST(test_resign_produces_verifiable_package);
    RUN_TEST(test_resign_with_different_key);

    return UNITY_END();
}
