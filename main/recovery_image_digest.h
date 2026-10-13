#pragma once
#include <stdbool.h>
// SHA256 of the complete downloaded body. Output is populated only on success.
bool recovery_image_sha256(const char *path, char hex[65]);
