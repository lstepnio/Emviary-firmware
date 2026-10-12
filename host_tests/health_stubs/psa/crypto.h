#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct { int active; } psa_hash_operation_t;
#define PSA_HASH_OPERATION_INIT {0}
#define PSA_ALG_SHA_256 1
#define PSA_SUCCESS 0
int psa_hash_setup(psa_hash_operation_t *op, int alg);
int psa_hash_update(psa_hash_operation_t *op, const uint8_t *input, size_t size);
int psa_hash_finish(psa_hash_operation_t *op, uint8_t *output, size_t capacity, size_t *size);
int psa_hash_abort(psa_hash_operation_t *op);
