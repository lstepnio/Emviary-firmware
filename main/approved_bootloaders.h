#pragma once

#include <stddef.h>

typedef struct {
    size_t size;
    const char *sha256;
} ota_approved_bootloader_t;

// Add only exact size + SHA256 pairs from independently verified bootloaders
// built with compatible rollback settings and this device's flash layout.
// Application OTA never installs a bootloader, so compatible earlier builds
// need explicit approval even when their build timestamp changes their digest.
// E1002 v0.8.0 ESP-IDF 6.0 rollback bootloader; original partition layout.
#ifndef EMVIARY_APPROVED_BOOTLOADERS
#define EMVIARY_APPROVED_BOOTLOADERS \
    {22576, "6b38e8bd8b4b194db752351c21a277b450f62efc94e2309f040a7f6ac8cf3828"}, \
    {15232, "8eff374aac9c866df130851d57638d2479b82c8b4d5981fe54a5ef36d9affdcc"},
#endif

static const ota_approved_bootloader_t ota_approved_bootloaders[] = {
    EMVIARY_APPROVED_BOOTLOADERS
    {0, NULL}, // Sentinel keeps the empty registry valid C.
};
