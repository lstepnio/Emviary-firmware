#include "psa/crypto.h"
#include "recovery_image_digest.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
// Provider stub validates streaming and cleanup, not the SDK's SHA256 algorithm.
static int failure, updates, aborts;
static size_t bytes;
int psa_hash_setup(psa_hash_operation_t *op, int algorithm)
{
    assert(algorithm == PSA_ALG_SHA_256);
    op->active = 1;
    return failure == 1 ? -1 : PSA_SUCCESS;
}
int psa_hash_update(psa_hash_operation_t *op, const uint8_t *data, size_t count)
{
    assert(op->active && count > 0 && count <= 1024);
    updates++;
    bytes += count;
    for (size_t i = 0; i < count; i++)
        assert(data[i] == 'a');
    return failure == 2 ? -1 : PSA_SUCCESS;
}
int psa_hash_finish(psa_hash_operation_t *op, uint8_t *out, size_t capacity, size_t *size)
{
    assert(op->active && capacity == 32);
    for (int i = 0; i < 32; i++)
        out[i] = i;
    *size = failure == 4 ? 31 : 32;
    return failure == 3 ? -1 : PSA_SUCCESS;
}
int psa_hash_abort(psa_hash_operation_t *op)
{
    aborts++;
    op->active = 0;
    return PSA_SUCCESS;
}
int main(void)
{
    char path[] = "/tmp/emviary-digest-XXXXXX";
    int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    FILE *file = fdopen(descriptor, "wb");
    assert(file);
    for (int i = 0; i < 2050; i++)
        assert(fputc('a', file) != EOF);
    assert(fclose(file) == 0);
    char hex[65];
    assert(recovery_image_sha256(path, hex));
    assert(bytes == 2050 && updates == 3 && aborts == 1);
    assert(!strcmp(hex, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"));
    for (failure = 1; failure <= 4; failure++) {
        int before = aborts;
        strcpy(hex, "stale");
        assert(!recovery_image_sha256(path, hex));
        assert(!hex[0] && aborts == before + 1);
    }
    unlink(path);
    assert(!recovery_image_sha256(path, hex));
    assert(!hex[0]);
    puts("PSA image digest streaming and failure cleanup tests passed");
}
