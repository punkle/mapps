#include "mapps/MappAppLibrary.h"
#include "mapps/MappFormat.h"
#include "mapps/MappManifest.h"
#include "mapps/MappSignature.h"

#include <algorithm>
#include <cctype>
#include <cstring>

static std::string slugify(const std::string &name)
{
    std::string result;
    result.reserve(name.size());
    bool lastWasHyphen = true; // prevent leading hyphen
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            lastWasHyphen = false;
        } else if (!lastWasHyphen) {
            result += '-';
            lastWasHyphen = true;
        }
    }
    // Remove trailing hyphen
    if (!result.empty() && result.back() == '-')
        result.pop_back();
    return result;
}

MappAppLibrary::MappAppLibrary(std::shared_ptr<TrustBackend> backend) : backend(std::move(backend)) {}

std::unique_ptr<MappApp> MappAppLibrary::loadApp(const std::vector<uint8_t> &data)
{
    return loadApp(data.data(), data.size());
}

std::unique_ptr<MappApp> MappAppLibrary::loadApp(const uint8_t *data, size_t size)
{
    // 1. Deserialize .mapp binary
    std::vector<MappFile> files;
    if (!MappFormat::deserialize(data, size, files))
        return nullptr;

    if (files.empty() || files[0].name != "app.json")
        return nullptr;

    // 2. Parse manifest
    MappManifest manifest;
    if (!parseManifest(files[0].content, manifest))
        return nullptr;

    // Build the app object
    auto app = std::unique_ptr<MappApp>(new MappApp());
    app->manifest = manifest;
    app->files = std::move(files);
    app->slug = slugify(manifest.name);
    app->backend = backend;
    app->signatureInfo.backend = backend;

    // 3. If signed, verify the signature
    if (!manifest.signature.empty()) {
        // Find .sig file
        std::string sigContent;
        for (const auto &f : app->files) {
            if (f.name == manifest.signature) {
                sigContent = f.content;
                break;
            }
        }

        if (!sigContent.empty()) {
            MappSignature::SigFileData sig = MappSignature::parseSigFile(
                reinterpret_cast<const uint8_t *>(sigContent.data()), sigContent.size());

            if (sig.valid) {
                // Compute digest
                std::string strippedJson = MappSignature::stripSignatureField(app->files[0].content);

                std::vector<std::pair<std::string, std::string>> fileContents;
                for (size_t i = 1; i < app->files.size(); i++) {
                    const auto &name = app->files[i].name;
                    if (name.size() >= 4 && name.substr(name.size() - 4) == ".sig")
                        continue;
                    fileContents.push_back({name, app->files[i].content});
                }

                uint8_t digest[MappSignature::DIGEST_SIZE];
                MappSignature::computeDigest(strippedJson, fileContents, digest);

                // Verify
                if (MappSignature::verifySignature(sig.pubKey, sig.signature, digest)) {
                    app->signatureInfo.valid = true;
                    memcpy(app->signatureInfo.pubKey, sig.pubKey, MappSignature::PUBKEY_SIZE);
                }
            }
        }
    }

    return app;
}
