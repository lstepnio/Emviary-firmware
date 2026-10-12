#pragma once
#include <stdio.h>
#define CONFIG_H
#define FS_MOUNT_POINT "/host-health-test"
#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1
FILE *health_fopen(const char *path, const char *mode);
size_t health_fwrite(const void *ptr, size_t size, size_t count, FILE *file);
size_t health_fread(void *ptr, size_t size, size_t count, FILE *file);
int health_fclose(FILE *file);
int health_unlink(const char *path);
#define fopen health_fopen
#define fwrite health_fwrite
#define fread health_fread
#define fclose health_fclose
#define unlink health_unlink
