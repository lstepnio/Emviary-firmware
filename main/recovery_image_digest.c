#include "recovery_image_digest.h"
#include "psa/crypto.h"
#include <stdio.h>

bool recovery_image_sha256(const char *path, char hex[65])
{
    hex[0] = 0;
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    unsigned char chunk[1024], digest[32];
    size_t count, length = 0;
    bool ok = psa_hash_setup(&operation, PSA_ALG_SHA_256) == PSA_SUCCESS;
    while (ok && (count = fread(chunk, 1, sizeof(chunk), file)))
        ok = psa_hash_update(&operation, chunk, count) == PSA_SUCCESS;
    ok = ok && !ferror(file);
    if (fclose(file) != 0)
        ok = false;
    if (ok)
        ok = psa_hash_finish(&operation, digest, sizeof(digest), &length) == PSA_SUCCESS &&
             length == 32;
    psa_hash_abort(&operation);
    if (ok)
        for (int i = 0; i < 32; i++)
            snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return ok;
}
