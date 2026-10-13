#pragma once

// Cloud-only E1002 configuration is authoritative on the server. Receiving
// its payload does not represent a local edit and must not write cfg_updated.
// Other boards and local/manual settings retain their timestamp behavior.
#if defined(EMVIARY_CLOUD_ONLY) && CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1002
#define REMOTE_CONFIG_TOUCH_TIMESTAMP 0
#else
#define REMOTE_CONFIG_TOUCH_TIMESTAMP 1
#endif
