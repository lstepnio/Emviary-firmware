#include "config_validation.h"
#include <string.h>
static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool config_input_valid(const char *text, size_t capacity, bool header_name) {
    if (!text || !capacity) return false;
    size_t i = 0;
    for (; text[i]; i++) {
        unsigned char c = text[i];
        if (i + 1 >= capacity || c < 32 || c == 127) return false;
        if (header_name && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || strchr("!#$%&'*+-.^_`|~", c))) return false;
    }
    return true;
}
bool config_form_field(const char *body, const char *key, char *out, size_t capacity) {
    if (!body || !key || !out || !capacity) return false;
    size_t key_len = strlen(key);
    for (const char *p = body; *p;) {
        const char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        if ((size_t)(end-p) > key_len && !strncmp(p,key,key_len) && p[key_len]=='=') {
            size_t n = 0;
            for (const char *v=p+key_len+1; v<end; v++) {
                unsigned char c = *v;
                if (c == '+') c = ' ';
                else if (c == '%') {
                    if (end-v < 3 || hex(v[1]) < 0 || hex(v[2]) < 0) return false;
                    c = (hex(v[1]) << 4) | hex(v[2]); v += 2;
                }
                if (!c || n+1 >= capacity) return false;
                out[n++] = c;
            }
            out[n] = 0; return true;
        }
        p = *end ? end+1 : end;
    }
    return false;
}

bool config_json_shape_valid(const char *json, size_t length)
{
    char containers[16];
    size_t depth = 0;
    bool in_string = false, escaped = false, started = false, finished = false;
    if (!json || !length) return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = json[i];
        if (!c || (c < 32 && (in_string || (c != '\t' && c != '\n' && c != '\r'))))
            return false;
        if (in_string) {
            if (escaped) {
                // cJSON strings are C strings; an escaped NUL would otherwise
                // hide the remainder of a credential, URL, or object key.
                if (c == 'u' && length - i >= 5 && !memcmp(json + i + 1, "0000", 4))
                    return false;
                escaped = false;
            } else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (finished) return false;
        if (!started) {
            if (c != '{' && c != '[') return false;
            started = true;
        }
        if (c == '"') in_string = true;
        else if (c == '{' || c == '[') {
            if (depth == sizeof(containers)) return false;
            containers[depth++] = c;
        } else if (c == '}' || c == ']') {
            if (!depth || containers[depth-1] != (c == '}' ? '{' : '[')) return false;
            depth--;
            if (!depth) finished = true;
        }
    }
    return finished && !in_string && !escaped && !depth;
}
