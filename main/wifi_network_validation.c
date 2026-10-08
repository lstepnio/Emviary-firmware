#include "wifi_network_validation.h"

#include <ctype.h>
#include <string.h>

bool wifi_network_credentials_valid(const char *ssid, const char *password)
{
    if (!ssid || !password)
        return false;
    size_t ssid_len = strlen(ssid), pass_len = strlen(password);
    if (!ssid_len || ssid_len > 32)
        return false;
    for (size_t i = 0; i < ssid_len; i++)
        if ((unsigned char) ssid[i] < 32 || (unsigned char) ssid[i] == 127)
            return false;
    if (!pass_len)
        return true;
    if (pass_len == 64) {
        for (size_t i = 0; i < pass_len; i++)
            if (!isxdigit((unsigned char) password[i]))
                return false;
        return true;
    }
    if (pass_len < 8 || pass_len > 63)
        return false;
    for (size_t i = 0; i < pass_len; i++)
        if ((unsigned char) password[i] < 32 || (unsigned char) password[i] > 126)
            return false;
    return true;
}
