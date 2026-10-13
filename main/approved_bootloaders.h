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
// Verified E1002 ESP-IDF 6.0 rollback bootloaders; original partition layout.
// v0.8.7 was installed and its complete USB recovery regions verified.
#ifndef EMVIARY_APPROVED_BOOTLOADERS
#define EMVIARY_APPROVED_BOOTLOADERS \
    {22576, "6b38e8bd8b4b194db752351c21a277b450f62efc94e2309f040a7f6ac8cf3828"}, \
    {15232, "8eff374aac9c866df130851d57638d2479b82c8b4d5981fe54a5ef36d9affdcc"}, \
    {15232, "57f4b520e90ee9f98d7ea72434d5932505082a241904cfc44a8784e67d11f829"}, \
    {15232, "11e9281d6693079e6dad224a2762c1d6a4f320377cf4959f674d22f40ba9bd56"},
#endif

static const ota_approved_bootloader_t ota_approved_bootloaders[] = {
    EMVIARY_APPROVED_BOOTLOADERS
    {0, NULL}, // Sentinel keeps the empty registry valid C.
};
