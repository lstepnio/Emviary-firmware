#include "diagnostic_layout.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    uint16_t offset;
    uint8_t width, advance, stride;
} diagnostic_glyph_t;
typedef struct {
    const unsigned char *bits;
    const diagnostic_glyph_t *glyphs;
    uint8_t height;
} diagnostic_font_t;
#include "diagnostic_assets.h"
typedef struct {
    int width, height;
    diagnostic_pixel_fn pixel;
    void *context;
} canvas_t;
static void dot(canvas_t *c, int x, int y)
{
    if (x >= 0 && y >= 0 && x < c->width && y < c->height)
        c->pixel(c->context, x, y);
}
static unsigned codepoint(const unsigned char **p)
{
    unsigned c = *(*p)++;
    if (c < 128)
        return c;
    if ((c & 0xe0) == 0xc0 && ((*p)[0] & 0xc0) == 0x80) {
        unsigned d = *(*p)++;
        return ((c & 31) << 6) | (d & 63);
    }
    while ((**p & 0xc0) == 0x80)
        (*p)++;
    return '?';
}
static unsigned glyph_index(unsigned c)
{
    if (c >= 32 && c <= 126)
        return c - 32;
    if (c >= 160 && c <= 255)
        return c - 65;
    return '?' - 32;
}
static int text_width(const diagnostic_font_t *font, const char *s)
{
    int w = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p)
        w += font->glyphs[glyph_index(codepoint(&p))].advance;
    return w;
}
static void glyph(canvas_t *c, int x, int y, const diagnostic_font_t *f, unsigned index)
{
    const diagnostic_glyph_t *g = &f->glyphs[index];
    for (int row = 0; row < f->height; row++)
        for (int column = 0; column < g->width; column++)
            if (f->bits[g->offset + row * g->stride + column / 8] & (128 >> (column % 8)))
                dot(c, x + column, y + row);
}
static void text(canvas_t *c, int x, int y, int max_width, const diagnostic_font_t *font,
                 const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    int end = x + max_width;
    int ellipsis = text_width(font, "...");
    bool shortened = text_width(font, s) > max_width;
    while (*p) {
        unsigned index = glyph_index(codepoint(&p));
        int advance = font->glyphs[index].advance;
        if (x + advance + (shortened ? ellipsis : 0) > end)
            break;
        glyph(c, x, y, font, index);
        x += advance;
    }
    if (shortened)
        for (int i = 0; i < 3 && x + font->glyphs['.' - 32].advance <= end; i++) {
            glyph(c, x, y, font, '.' - 32);
            x += font->glyphs['.' - 32].advance;
        }
}
static void field(canvas_t *c, int x, int y, int width, const char *label, const char *value,
                  size_t capacity, const char *fallback)
{
    char safe[96];
    snprintf(safe, sizeof(safe), "%.*s", (int)capacity, value);
    text(c, x, y, width, &font_label, label);
    text(c, x, y + 22, width, &font_value, safe[0] ? safe : fallback);
}
static void divider(canvas_t *c, int x, int y, int width)
{
    for (int i = 0; i < width; i += 2)
        dot(c, x + i, y);
}
static void qr(canvas_t *c, int x, int y, int scale)
{
    for (int row = 0; row < DIAGNOSTIC_QR_SIZE; row++)
        for (int col = 0; col < DIAGNOSTIC_QR_SIZE; col++)
            if (diagnostic_qr[row * DIAGNOSTIC_QR_SIZE + col])
                for (int yy = 0; yy < scale; yy++)
                    for (int xx = 0; xx < scale; xx++)
                        dot(c, x + col * scale + xx, y + row * scale + yy);
}
static void centered(canvas_t *c, int y, const diagnostic_font_t *font, const char *s, int start,
                     int width)
{
    int actual = text_width(font, s);
    text(c, start + (width - actual) / 2, y, width, font, s);
}
void diagnostic_layout_render(int width, int height, const display_diagnostic_status_t *s,
                              diagnostic_pixel_fn pixel, void *context)
{
    if (!s || !pixel || width < 400 || height < 440)
        return;
    canvas_t c = {width, height, pixel, context};
    bool portrait = height > width;
    int margin = 28, left_width = portrait ? width - 56 : 468, column = (left_width - 24) / 2;
    text(&c, margin, 20, width - 56, &font_title, "Your frame");
    char identity[132];
    snprintf(identity, sizeof(identity), "%.*s%s%.*s", 63, s->frame_name,
             s->frame_name[0] && s->hostname[0] ? " | " : "", 63, s->hostname);
    text(&c, margin, 72, width - 56, &font_label, identity[0] ? identity : "Frame status");
    divider(&c, margin, 104, width - 56);
    field(&c, margin, 118, left_width, "Last cloud refresh", s->last_refresh,
          sizeof(s->last_refresh), "Not yet refreshed");
    char cloud[64];
    snprintf(cloud, sizeof(cloud), "%.*s", 47, s->cloud_status);
    text(&c, margin, 175, left_width, &font_label, cloud[0] ? cloud : "Status unavailable");
    int network_y = portrait ? 272 : 210, bottom_y = portrait ? 370 : 308;
    field(&c, margin, network_y, column, "Wi-Fi", s->wifi_name, sizeof(s->wifi_name),
          "No saved network");
    char signal[64];
    snprintf(signal, sizeof(signal),
             s->wifi_connected ? (s->rssi_valid ? "Connected  %d dBm" : "Connected") : "Offline",
             s->rssi_dbm);
    text(&c, margin, network_y + 57, column, &font_label, signal);
    field(&c, margin + column + 24, network_y, column,
          s->ip_last_known ? "IP address (last known)" : "IP address", s->ip_address,
          sizeof(s->ip_address), "Unavailable");
    field(&c, margin, bottom_y, column, "Battery", s->battery_status, sizeof(s->battery_status),
          "Unavailable");
    if (portrait) {
        field(&c, margin, 210, left_width, "Next scheduled wake", s->next_wake,
              sizeof(s->next_wake), "Not scheduled");
        field(&c, margin + column + 24, bottom_y, column, "Firmware", s->firmware,
              sizeof(s->firmware), "Unavailable");
        divider(&c, margin, 437, width - 56);
        centered(&c, 451, &font_value, "Open Emviary", 0, width);
        int scale = 6;
        qr(&c, (width - 37 * scale) / 2, 491, scale);
        centered(&c, 720, &font_label, "emviary.majjix.com", 0, width);
        centered(&c, 744, &font_label, "Scan to manage your frame", 0, width);
    } else {
        field(&c, margin + column + 24, bottom_y, column, "Next scheduled wake", s->next_wake,
              sizeof(s->next_wake), "Not scheduled");
        char fw[64];
        snprintf(fw, sizeof(fw), "Firmware  %.*s", 31, s->firmware);
        text(&c, margin, 392, left_width, &font_label, fw);
        for (int y = 118; y < 420; y += 2)
            dot(&c, 514, y);
        centered(&c, 120, &font_value, "Open Emviary", 528, width - 556);
        int scale = 6;
        qr(&c, 528 + (width - 556 - 37 * scale) / 2, 156, scale);
        centered(&c, 384, &font_label, "emviary.majjix.com", 528, width - 556);
        centered(&c, 409, &font_label, "Scan to manage your frame", 528, width - 556);
    }
    divider(&c, margin, height - 37, width - 56);
    text(&c, margin, height - 30, width - 56, &font_label, "Tap a white or green button to return");
}
