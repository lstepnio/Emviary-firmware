#pragma once
#include <stdbool.h>
#define CONFIG_H
#define CONFIG_MANAGER_H
#define CONFIG_BOARD_DRIVER_SEEEDSTUDIO_RETERMINAL_E1002 1
#define FS_MOUNT_POINT "quiet_log_storage"
bool config_manager_get_debug_log_enabled(void);
void config_manager_set_debug_log_enabled(bool enabled);
