#include "CmdVerify.h"
#include "PosixIO.h"
#include "mapps/MappFormat.h"
#include "mapps/MappManifest.h"
#include "mapps/MappSignature.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

int cmdVerify(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: mapps verify <file.mapp>\n");
        return 1;
    }

    std::string mappFile = argv[0];

    // Read .mapp
    std::vector<uint8_t> mappData;
    if (!PosixIO::readFileBytes(mappFile, mappData)) {
        fprintf(stderr, "Error: failed to read %s\n", mappFile.c_str());
        return 1;
    }

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

    if (manifest.signature.empty()) {
        fprintf(stderr, "Error: app is not signed (no 'signature' field in manifest)\n");
        return 1;
    }

    // Find .sig file
    std::string sigContent;
    for (const auto &f : files) {
        if (f.name == manifest.signature) {
            sigContent = f.content;
            break;
        }
    }

    if (sigContent.empty()) {
        fprintf(stderr, "Error: signature file '%s' not found in .mapp\n", manifest.signature.c_str());
        return 1;
    }

    // Parse sig
    MappSignature::SigFileData sig =
        MappSignature::parseSigFile(reinterpret_cast<const uint8_t *>(sigContent.data()), sigContent.size());
    if (!sig.valid) {
        fprintf(stderr, "Error: invalid signature file format\n");
        return 1;
    }

    // Compute digest
    std::string strippedJson = MappSignature::stripSignatureField(files[0].content);

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
    MappSignature::computeDigest(strippedJson, fileContents, digest);

    // Verify
    bool valid = MappSignature::verifySignature(sig.pubKey, sig.signature, digest);

    std::string fingerprint = MappSignature::pubKeyFingerprint(sig.pubKey);

    if (!valid) {
        fprintf(stderr, "FAILED: signature verification failed for %s\n", mappFile.c_str());
        fprintf(stderr, "  Signer fingerprint: %s\n", fingerprint.c_str());
        return 1;
    }

    printf("OK: %s\n", mappFile.c_str());
    printf("  App:         %s v%s by %s\n", manifest.name.c_str(), manifest.version.c_str(), manifest.author.c_str());
    printf("  Signer:      %s\n", fingerprint.c_str());

    return 0;
}
