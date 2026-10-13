#include "diagnostic_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *pixels;
static int width, height, count;
static void pixel(void *unused, int x, int y)
{
    assert(x >= 0 && y >= 0 && x < width && y < height);
    pixels[y * width + x] = 1;
    count++;
}
static void render(const char *path, int w, int h, const display_diagnostic_status_t *s)
{
    width = w;
    height = h;
    count = 0;
    pixels = calloc((size_t)w * h, 1);
    assert(pixels);
    diagnostic_layout_render(w, h, s, pixel, NULL);
    assert(count > 10000);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P4\n%d %d\n", w, h);
    for (int y = 0; y < h; y++)
        for (int b = 0; b < (w + 7) / 8; b++) {
            unsigned byte = 0;
            for (int bit = 0; bit < 8; bit++) {
                int x = b * 8 + bit;
                if (x < w && pixels[y * w + x])
                    byte |= 128 >> bit;
            }
            fputc(byte, f);
        }
    assert(fclose(f) == 0);
    free(pixels);
}
int main(int argc, char **argv)
{
    const char *prefix = argc > 1 ? argv[1] : "diagnostic";
    char path[256];
    display_diagnostic_status_t s = {0};
    strcpy(s.frame_name, "Emily");
    strcpy(s.hostname, "emily-e1002");
    strcpy(s.ip_address, "192.168.1.42");
    strcpy(s.wifi_name, "Home Wi-Fi");
    strcpy(s.cloud_status, "Connected - picture updated");
    strcpy(s.last_refresh, "Today, 9:42 AM");
    strcpy(s.battery_status, "84% - charging");
    strcpy(s.next_wake, "Today, 10:00 PM");
    strcpy(s.firmware, "v0.8.8");
    s.wifi_connected = true;
    s.rssi_valid = true;
    s.rssi_dbm = -48;
    snprintf(path, sizeof(path), "%s-landscape.pbm", prefix);
    render(path, 800, 480, &s);
    snprintf(path, sizeof(path), "%s-portrait.pbm", prefix);
    render(path, 480, 800, &s);
    memset(s.wifi_name, 'W', sizeof(s.wifi_name));
    memset(s.ip_address, 'A', sizeof(s.ip_address));
    memset(s.last_refresh, 'M', sizeof(s.last_refresh));
    memset(s.firmware, 'W', sizeof(s.firmware));
    s.wifi_connected = false;
    s.rssi_valid = false;
    s.ip_last_known = true;
    strcpy(s.cloud_status, "Last request failed - saved picture kept");
    snprintf(path, sizeof(path), "%s-offline-landscape.pbm", prefix);
    render(path, 800, 480, &s);
    snprintf(path, sizeof(path), "%s-offline-portrait.pbm", prefix);
    render(path, 480, 800, &s);
    puts("diagnostic layout bounds, bounded fields and landscape/portrait render tests passed");
}
