#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Binary signature file format (101 bytes) — matches firmware MSIG:
//   Bytes 0-3:    Magic "MSIG" (0x4D534947, little-endian)
//   Byte 4:       Version 0x01
//   Bytes 5-36:   Developer Ed25519 public key (32 bytes)
//   Bytes 37-100: Ed25519 signature (64 bytes)

class MappSignature
{
  public:
    static constexpr uint32_t SIG_MAGIC = 0x4D534947; // "MSIG"
    static constexpr uint8_t SIG_VERSION = 0x01;
    static constexpr size_t SIG_FILE_SIZE = 101;
    static constexpr size_t PUBKEY_SIZE = 32;
    static constexpr size_t PRIVKEY_SIZE = 32;
    static constexpr size_t SIGNATURE_SIZE = 64;
    static constexpr size_t DIGEST_SIZE = 64; // Blake2b-512

    struct SigFileData {
        uint8_t pubKey[PUBKEY_SIZE];
        uint8_t signature[SIGNATURE_SIZE];
        bool valid;
    };

    // Parse a binary .sig into its components
    static SigFileData parseSigFile(const uint8_t *data, size_t size);

    // Build a 101-byte MSIG binary from components
    static std::vector<uint8_t> buildSigFile(const uint8_t pubKey[PUBKEY_SIZE],
                                             const uint8_t signature[SIGNATURE_SIZE]);

    // Compute Blake2b-512 digest over stripped manifest JSON + file contents
    // fileContents: pairs of (filename, content) — excluding app.json and *.sig
    static void computeDigest(const std::string &strippedJson,
                              const std::vector<std::pair<std::string, std::string>> &fileContents,
                              uint8_t digest[DIGEST_SIZE]);

    // Ed25519 sign a 64-byte digest
    static void signDigest(const uint8_t privKey[PRIVKEY_SIZE],
                           const uint8_t pubKey[PUBKEY_SIZE],
                           const uint8_t digest[DIGEST_SIZE],
                           uint8_t signature[SIGNATURE_SIZE]);

    // Ed25519 verify a signature over a 64-byte digest
    static bool verifySignature(const uint8_t pubKey[PUBKEY_SIZE],
                                const uint8_t signature[SIGNATURE_SIZE],
                                const uint8_t digest[DIGEST_SIZE]);

    // Derive Ed25519 public key from private key
    static void derivePublicKey(const uint8_t privKey[PRIVKEY_SIZE],
                                uint8_t pubKey[PUBKEY_SIZE]);

    // Strip the "signature" field from manifest JSON (must match firmware's jsonStripField)
    static std::string stripSignatureField(const std::string &json);

    // Convert first 8 bytes of public key to hex fingerprint
    static std::string pubKeyFingerprint(const uint8_t pubKey[PUBKEY_SIZE]);

    // Convert full public key (32 bytes) to hex string (64 chars)
    static std::string pubKeyToHex(const uint8_t pubKey[PUBKEY_SIZE]);

    // Parse hex string back to bytes. Returns false if invalid.
    static bool hexToBytes(const std::string &hex, uint8_t *out, size_t outLen);
};
