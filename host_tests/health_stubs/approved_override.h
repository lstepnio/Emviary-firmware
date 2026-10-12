#pragma once

// Synthetic fixtures are confined to this host-test target. Production's
// registry remains empty until a real compatible bootloader is verified.
#undef EMVIARY_APPROVED_BOOTLOADERS
#define EMVIARY_APPROVED_BOOTLOADERS \
    {777, "0101010101010101010101010101010101010101010101010101010101010101"},
