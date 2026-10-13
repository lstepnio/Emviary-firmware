#include "recovery_protocol.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t recovery_crc32(const void *data, size_t length)
{
    uint32_t crc = ~0u;
    const unsigned char *p = data;
    while (length--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}
bool recovery_identifier_valid(const char *v)
{
    if (!v || !*v || strlen(v) > RECOVERY_ID_MAX)
        return false;
    for (; *v; v++)
        if (!((*v >= 'a' && *v <= 'z') || (*v >= 'A' && *v <= 'Z') || (*v >= '0' && *v <= '9') ||
              *v == '-' || *v == '_'))
            return false;
    return true;
}
bool recovery_frame_decode(char *line, uint32_t *seq, char **json)
{
    if (strncmp(line, "!EMV1:", 6))
        return false;
    char *p = line + 6, *end;
    if (*p < '0' || *p > '9')
        return false;
    unsigned long n = strtoul(p, &end, 10);
    if (n > UINT32_MAX || *end != ':')
        return false;
    p = end + 1;
    if (*p < '0' || *p > '9')
        return false;
    unsigned long len = strtoul(p, &end, 10);
    if (!len || len > RECOVERY_JSON_MAX || *end != ':')
        return false;
    p = end + 1;
    if (strlen(p) < len + 9 || p[len] != ':')
        return false;
    char *crc = p + len + 1;
    if (strlen(crc) != 8)
        return false;
    for (int i = 0; i < 8; i++)
        if (!((crc[i] >= '0' && crc[i] <= '9') || (crc[i] >= 'a' && crc[i] <= 'f') ||
              (crc[i] >= 'A' && crc[i] <= 'F')))
            return false;
    unsigned long expected = strtoul(crc, &end, 16);
    if (*end || expected != recovery_crc32(p, len))
        return false;
    p[len] = 0;
    *json = p;
    *seq = (uint32_t)n;
    return true;
}
int recovery_frame_encode(char *out, size_t cap, uint32_t seq, const char *json)
{
    int n =
        snprintf(out, cap, "!EMV1:%lu:%u:%s:%08lx\n", (unsigned long)seq, (unsigned)strlen(json),
                 json, (unsigned long)recovery_crc32(json, strlen(json)));
    return n > 0 && (size_t)n < cap ? n : -1;
}
bool recovery_lease_active(recovery_lease_t *l, uint64_t now)
{
    if (l->active &&
        (now - l->renewed >= RECOVERY_LEASE_MS || now - l->started >= RECOVERY_ABSOLUTE_MS)) {
        memset(l, 0, sizeof(*l));
    }
    return l->active;
}
bool recovery_lease_match(recovery_lease_t *l, const char *s, const char *n, uint64_t now)
{
    return recovery_identifier_valid(s) && recovery_identifier_valid(n) &&
           recovery_lease_active(l, now) && !strcmp(l->session, s) && !strcmp(l->nonce, n);
}
bool recovery_lease_begin(recovery_lease_t *l, const char *s, const char *n, uint64_t now)
{
    if (!recovery_identifier_valid(s) || !recovery_identifier_valid(n))
        return false;
    if (recovery_lease_active(l, now)) {
        if (!recovery_lease_match(l, s, n, now))
            return false;
        l->renewed = now;
        return true;
    }
    snprintf(l->session, sizeof(l->session), "%s", s);
    snprintf(l->nonce, sizeof(l->nonce), "%s", n);
    l->started = l->renewed = now;
    l->active = true;
    return true;
}
bool recovery_display_request_test(recovery_display_state_t *state)
{
    if (state->used)
        return false;
    state->used = true;
    state->test_pending = true;
    return true;
}
bool recovery_display_request_restore(recovery_display_state_t *state)
{
    if (!state->used || state->test_pending)
        return false;
    if (state->restored || state->restoring || state->restore_pending)
        return true;
    state->restore_pending = true;
    return true;
}
