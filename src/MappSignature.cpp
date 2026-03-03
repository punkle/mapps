#include "mapps/MappSignature.h"

#include <BLAKE2b.h>
#include <Ed25519.h>
#include <cstring>

// Strip a JSON field and its value — must match firmware's jsonStripField exactly
std::string MappSignature::stripSignatureField(const std::string &json)
{
    const char *key = "signature";
    std::string pattern = std::string("\"") + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos)
        return json;

    size_t fieldStart = pos;

    size_t colon = json.find(':', pos + pattern.size());
    if (colon == std::string::npos)
        return json;
    size_t valStart = json.find_first_not_of(" \t\n\r", colon + 1);
    if (valStart == std::string::npos)
        return json;

    size_t fieldEnd;
    if (json[valStart] == '"') {
        // Find closing quote, skipping escaped quotes
        size_t closeQuote = valStart + 1;
        while (closeQuote < json.size()) {
            if (json[closeQuote] == '\\') {
                closeQuote += 2; // skip escaped character
                continue;
            }
            if (json[closeQuote] == '"')
                break;
            closeQuote++;
        }
        if (closeQuote >= json.size())
            return json;
        fieldEnd = closeQuote + 1;
    } else if (json[valStart] == '[') {
        size_t closeBracket = json.find(']', valStart + 1);
        if (closeBracket == std::string::npos)
            return json;
        fieldEnd = closeBracket + 1;
    } else {
        return json;
    }

    size_t removeStart = fieldStart;
    size_t removeEnd = fieldEnd;

    // Handle trailing comma
    size_t afterField = json.find_first_not_of(" \t\n\r", fieldEnd);
    if (afterField != std::string::npos && json[afterField] == ',') {
        removeEnd = afterField + 1;
        size_t afterComma = json.find_first_not_of(" \t\n\r", removeEnd);
        if (afterComma != std::string::npos)
            removeEnd = afterComma;
    } else {
        // No trailing comma — check for leading comma
        if (removeStart > 0) {
            size_t beforeField = json.find_last_not_of(" \t\n\r", removeStart - 1);
            if (beforeField != std::string::npos && json[beforeField] == ',') {
                removeStart = beforeField;
            }
        }
    }

    return json.substr(0, removeStart) + json.substr(removeEnd);
}

MappSignature::SigFileData MappSignature::parseSigFile(const uint8_t *data, size_t size)
{
    SigFileData result = {};
    result.valid = false;

    if (!data || size != SIG_FILE_SIZE)
        return result;

    // Check magic "MSIG" (little-endian)
    uint32_t magic = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    if (magic != SIG_MAGIC)
        return result;

    if (data[4] != SIG_VERSION)
        return result;

    memcpy(result.pubKey, data + 5, PUBKEY_SIZE);
    memcpy(result.signature, data + 37, SIGNATURE_SIZE);
    result.valid = true;
    return result;
}

std::vector<uint8_t> MappSignature::buildSigFile(const uint8_t pubKey[PUBKEY_SIZE],
                                                  const uint8_t signature[SIGNATURE_SIZE])
{
    std::vector<uint8_t> out(SIG_FILE_SIZE);
    // Magic "MSIG" little-endian
    out[0] = SIG_MAGIC & 0xFF;
    out[1] = (SIG_MAGIC >> 8) & 0xFF;
    out[2] = (SIG_MAGIC >> 16) & 0xFF;
    out[3] = (SIG_MAGIC >> 24) & 0xFF;
    out[4] = SIG_VERSION;
    memcpy(out.data() + 5, pubKey, PUBKEY_SIZE);
    memcpy(out.data() + 37, signature, SIGNATURE_SIZE);
    return out;
}

void MappSignature::computeDigest(const std::string &strippedJson,
                                  const std::vector<std::pair<std::string, std::string>> &fileContents,
                                  uint8_t digest[DIGEST_SIZE])
{
    BLAKE2b blake;
    blake.reset(DIGEST_SIZE);

    // Hash the stripped manifest JSON
    blake.update(strippedJson.data(), strippedJson.size());

    // Hash each file: filename + \0 + content + \0
    for (const auto &file : fileContents) {
        blake.update(file.first.data(), file.first.size());
        uint8_t nul = 0;
        blake.update(&nul, 1);
        blake.update(file.second.data(), file.second.size());
        blake.update(&nul, 1);
    }

    blake.finalize(digest, DIGEST_SIZE);
}

void MappSignature::signDigest(const uint8_t privKey[PRIVKEY_SIZE],
                               const uint8_t pubKey[PUBKEY_SIZE],
                               const uint8_t digest[DIGEST_SIZE],
                               uint8_t signature[SIGNATURE_SIZE])
{
    Ed25519::sign(signature, privKey, pubKey, digest, DIGEST_SIZE);
}

bool MappSignature::verifySignature(const uint8_t pubKey[PUBKEY_SIZE],
                                    const uint8_t signature[SIGNATURE_SIZE],
                                    const uint8_t digest[DIGEST_SIZE])
{
    return Ed25519::verify(signature, pubKey, digest, DIGEST_SIZE);
}

void MappSignature::derivePublicKey(const uint8_t privKey[PRIVKEY_SIZE],
                                    uint8_t pubKey[PUBKEY_SIZE])
{
    Ed25519::derivePublicKey(pubKey, privKey);
}

std::string MappSignature::pubKeyFingerprint(const uint8_t pubKey[PUBKEY_SIZE])
{
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(16);
    for (int i = 0; i < 8; i++) {
        result += hex[(pubKey[i] >> 4) & 0x0F];
        result += hex[pubKey[i] & 0x0F];
    }
    return result;
}

std::string MappSignature::pubKeyToHex(const uint8_t pubKey[PUBKEY_SIZE])
{
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (size_t i = 0; i < PUBKEY_SIZE; i++) {
        result += hex[(pubKey[i] >> 4) & 0x0F];
        result += hex[pubKey[i] & 0x0F];
    }
    return result;
}

bool MappSignature::hexToBytes(const std::string &hex, uint8_t *out, size_t outLen)
{
    if (hex.size() != outLen * 2)
        return false;

    for (size_t i = 0; i < outLen; i++) {
        uint8_t hi, lo;
        char ch = hex[i * 2];
        if (ch >= '0' && ch <= '9')
            hi = ch - '0';
        else if (ch >= 'a' && ch <= 'f')
            hi = 10 + ch - 'a';
        else if (ch >= 'A' && ch <= 'F')
            hi = 10 + ch - 'A';
        else
            return false;

        char cl = hex[i * 2 + 1];
        if (cl >= '0' && cl <= '9')
            lo = cl - '0';
        else if (cl >= 'a' && cl <= 'f')
            lo = 10 + cl - 'a';
        else if (cl >= 'A' && cl <= 'F')
            lo = 10 + cl - 'A';
        else
            return false;

        out[i] = (hi << 4) | lo;
    }
    return true;
}
