#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern int offline_test_alloc_fail;
static inline void *heap_caps_malloc(size_t n, int caps)
{
    (void) caps;
    if (offline_test_alloc_fail != 0) {
        if (offline_test_alloc_fail > 0) offline_test_alloc_fail--;
        return NULL;
    }
    return malloc(n);
}
