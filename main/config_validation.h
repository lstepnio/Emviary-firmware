#pragma once
#include <stdbool.h>
#include <stddef.h>
// Capacity includes the terminating NUL. Empty values explicitly clear a setting.
bool config_input_valid(const char *text, size_t capacity, bool header_name);
// Strict, order-independent form decoding: reject overflow, bad escapes and NUL.
bool config_form_field(const char *body, const char *key, char *out, size_t capacity);
// Bound parser recursion before cJSON allocation; length excludes terminating NUL.
// Reject raw control bytes, escaped NULs and data following the root container.
bool config_json_shape_valid(const char *json, size_t length);
