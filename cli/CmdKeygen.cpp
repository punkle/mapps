#include "CmdKeygen.h"
#include "NativeRng.h"
#include "PosixIO.h"
#include "mapps/MappSignature.h"
#include <cstdio>
#include <cstring>
#include <string>

int cmdKeygen(int argc, char **argv)
{
    std::string keyPath;

    for (int i = 0; i < argc; i++) {
        if (keyPath.empty() && argv[i][0] != '-') {
            keyPath = argv[i];
        }
    }

    if (keyPath.empty()) {
        fprintf(stderr, "Usage: mapps keygen <keyfile>\n");
        fprintf(stderr, "\nGenerates an Ed25519 keypair for signing .mapp files.\n");
        fprintf(stderr, "Creates <keyfile> (private) and <keyfile>.pub (public).\n");
        return 1;
    }

    uint8_t privKey[MappSignature::PRIVKEY_SIZE];
    uint8_t pubKey[MappSignature::PUBKEY_SIZE];

    if (!NativeRng::getRandomBytes(privKey, sizeof(privKey))) {
        fprintf(stderr, "Error: failed to read /dev/urandom\n");
        return 1;
    }

    MappSignature::derivePublicKey(privKey, pubKey);

    // Write private key
    std::vector<uint8_t> privData(privKey, privKey + sizeof(privKey));
    if (!PosixIO::writeFileBytes(keyPath, privData)) {
        fprintf(stderr, "Error: failed to write %s\n", keyPath.c_str());
        return 1;
    }

    // Write public key as .pub
    std::string pubPath = keyPath + ".pub";
    std::vector<uint8_t> pubData(pubKey, pubKey + sizeof(pubKey));
    if (!PosixIO::writeFileBytes(pubPath, pubData)) {
        fprintf(stderr, "Error: failed to write %s\n", pubPath.c_str());
        return 1;
    }

    printf("Generated keypair:\n");
    printf("  Private key: %s\n", keyPath.c_str());
    printf("  Public key:  %s\n", pubPath.c_str());
    printf("  Fingerprint: %s\n", MappSignature::pubKeyFingerprint(pubKey).c_str());
    return 0;
}
