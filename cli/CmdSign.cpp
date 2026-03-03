#include "CmdSign.h"
#include "PosixIO.h"
#include "mapps/MappFormat.h"
#include "mapps/MappManifest.h"
#include "mapps/MappSignature.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

int cmdSign(int argc, char **argv)
{
    std::string mappFile;
    std::string keyPath;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
            keyPath = argv[++i];
        } else if (mappFile.empty() && argv[i][0] != '-') {
            mappFile = argv[i];
        }
    }

    if (keyPath.empty() || mappFile.empty()) {
        fprintf(stderr, "Usage: mapps sign <file.mapp> --key <keyfile>\n");
        return 1;
    }

    // Read private key
    std::vector<uint8_t> privKeyData;
    if (!PosixIO::readFileBytes(keyPath, privKeyData) || privKeyData.size() != MappSignature::PRIVKEY_SIZE) {
        fprintf(stderr, "Error: failed to read private key from %s (expected %zu bytes)\n", keyPath.c_str(),
                (size_t)MappSignature::PRIVKEY_SIZE);
        return 1;
    }

    // Derive public key
    uint8_t pubKey[MappSignature::PUBKEY_SIZE];
    MappSignature::derivePublicKey(privKeyData.data(), pubKey);

    // Read .mapp file
    std::vector<uint8_t> mappData;
    if (!PosixIO::readFileBytes(mappFile, mappData)) {
        fprintf(stderr, "Error: failed to read %s\n", mappFile.c_str());
        return 1;
    }

    // Deserialize
    std::vector<MappFile> files;
    if (!MappFormat::deserialize(mappData, files)) {
        fprintf(stderr, "Error: invalid .mapp file\n");
        return 1;
    }

    if (files.empty() || files[0].name != "app.json") {
        fprintf(stderr, "Error: first file must be app.json\n");
        return 1;
    }

    // Parse manifest
    MappManifest manifest;
    if (!parseManifest(files[0].content, manifest)) {
        fprintf(stderr, "Error: failed to parse app.json\n");
        return 1;
    }

    // Determine sig filename
    std::string sigName = "app.sig";

    // Clear any existing signature field so the canonical form matches what verify
    // computes after stripping the signature field. Without this, re-signing an
    // already-signed .mapp would include the old "signature" field in the digest,
    // but verify would strip it, causing a mismatch.
    manifest.signature.clear();
    std::string canonicalJson = serializeManifest(manifest);

    // Build a lookup of file contents from the .mapp
    std::map<std::string, std::string> fileMap;
    for (size_t i = 1; i < files.size(); i++) {
        fileMap[files[i].name] = files[i].content;
    }

    // Collect only files referenced in manifest entries, in sorted entry-point order
    // (std::map iterates sorted by key). This matches how the device computes the digest.
    std::vector<std::pair<std::string, std::string>> fileContents;
    for (const auto &ep : manifest.entries) {
        auto it = fileMap.find(ep.second);
        if (it == fileMap.end()) {
            fprintf(stderr, "Error: entry file '%s' not found in .mapp\n", ep.second.c_str());
            return 1;
        }
        fileContents.push_back({ep.second, it->second});
    }

    uint8_t digest[MappSignature::DIGEST_SIZE];
    MappSignature::computeDigest(canonicalJson, fileContents, digest);

    // Sign
    uint8_t signature[MappSignature::SIGNATURE_SIZE];
    MappSignature::signDigest(privKeyData.data(), pubKey, digest, signature);

    // Build .sig file
    std::vector<uint8_t> sigFile = MappSignature::buildSigFile(pubKey, signature);

    // Remove old .sig files and update manifest with signature field
    manifest.signature = sigName;
    std::vector<MappFile> newFiles;
    for (auto &f : files) {
        if (f.name.size() >= 4 && f.name.substr(f.name.size() - 4) == ".sig")
            continue;
        if (f.name == "app.json") {
            f.content = serializeManifest(manifest);
        }
        newFiles.push_back(f);
    }

    // Add sig file
    newFiles.push_back({sigName, std::string(sigFile.begin(), sigFile.end())});

    // Re-serialize
    std::vector<uint8_t> newMapp = MappFormat::serialize(newFiles);
    if (!PosixIO::writeFileBytes(mappFile, newMapp)) {
        fprintf(stderr, "Error: failed to write %s\n", mappFile.c_str());
        return 1;
    }

    printf("Signed %s (fingerprint: %s)\n", mappFile.c_str(), MappSignature::pubKeyFingerprint(pubKey).c_str());
    return 0;
}
