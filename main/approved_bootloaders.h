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
// Empty until a known compatible bootloader has been explicitly approved.
#ifndef EMVIARY_APPROVED_BOOTLOADERS
#define EMVIARY_APPROVED_BOOTLOADERS
#endif

static const ota_approved_bootloader_t ota_approved_bootloaders[] = {
    EMVIARY_APPROVED_BOOTLOADERS
    {0, NULL}, // Sentinel keeps the empty registry valid C.
};
