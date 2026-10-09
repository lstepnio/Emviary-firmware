#include <assert.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "epaper.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#ifdef CONFIG_PM_ENABLE
#include "esp_pm.h"
#endif

static const char *TAG = "epaper_ed2208_gca";

static epaper_config_t g_cfg;
static spi_device_handle_t spi;

#ifdef CONFIG_PM_ENABLE
static esp_pm_lock_handle_t pm_lock = NULL;
#endif

#define EPD_WIDTH 800
#define EPD_HEIGHT 480
// Packed pixel buffer size: 2 pixels per byte (4-bit color depth)
#define EPD_BUF_SIZE (EPD_WIDTH / 2 * EPD_HEIGHT)

// SPI max transfer size per transaction
#define SPI_MAX_CHUNK 4092
// Data transfer chunk size (per CS window)
#define DATA_CHUNK_SIZE 128

// --- Low-level SPI helpers ---

static esp_err_t spi_begin(void)
{
    // ESP-IDF 5.4 and 6.0 reject finite acquisition waits. Commands and pixel
    // chunks must use the supported contract; a finite value fails every SPI
    // operation with ESP_ERR_INVALID_ARG. Controller BUSY waits remain bounded
    // below, but the SDK's bus/polling waits have no application-level deadline.
    return spi_device_acquire_bus(spi, portMAX_DELAY);
}

static void spi_end(void)
{
    spi_device_release_bus(spi);
}

static esp_err_t spi_write(const uint8_t *data, size_t len)
{
    spi_transaction_t t = {};
    t.rxlength = 0;
    while (len > 0) {
        size_t chunk = (len > SPI_MAX_CHUNK) ? SPI_MAX_CHUNK : len;
        t.length = chunk * 8;
        t.tx_buffer = data;
        esp_err_t ret = spi_device_polling_start(spi, &t, portMAX_DELAY);
        if (ret == ESP_OK) {
            ret = spi_device_polling_end(spi, portMAX_DELAY);
        }
        if (ret != ESP_OK) return ret;
        data += chunk;
        len -= chunk;
    }
    return ESP_OK;
}

// --- Display protocol helpers ---

// Send a command with optional data bytes in a single CS window.
// CS stays LOW for the entire command+data sequence.
static esp_err_t cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    gpio_set_level(g_cfg.pin_dc, 0);  // DC low = command
    esp_err_t ret = spi_begin();
    if (ret != ESP_OK) return ret;
    gpio_set_level(g_cfg.pin_cs, 0);  // CS low

    // Send command byte via SPI command register
    spi_transaction_ext_t cmd_t = {
        .command_bits = 8,
        .base =
            {
                .flags = SPI_TRANS_VARIABLE_CMD,
                .cmd = cmd,
            },
    };
    ret = spi_device_polling_start(spi, &cmd_t.base, portMAX_DELAY);
    if (ret == ESP_OK) {
        ret = spi_device_polling_end(spi, portMAX_DELAY);
    }

    if (ret == ESP_OK && len > 0) {
        gpio_set_level(g_cfg.pin_dc, 1);  // DC high = data
        // Copy to stack buffer to avoid PSRAM DMA issues
        uint8_t buf[16];
        if (len > sizeof(buf)) ret = ESP_ERR_INVALID_SIZE;
        else {
            memcpy(buf, data, len);
            ret = spi_write(buf, len);
        }
    }

    gpio_set_level(g_cfg.pin_cs, 1);  // CS high
    spi_end();
    return ret;
}

// Send a standalone command (no data bytes)
static esp_err_t send_command(uint8_t cmd)
{
    return cmd_data(cmd, NULL, 0);
}

// Send image buffer in DATA_CHUNK_SIZE-byte chunks, each in its own CS window,
// copied to a stack-local buffer to avoid PSRAM DMA issues.
static esp_err_t send_buffer(uint8_t *data, int len)
{
    uint8_t buf[DATA_CHUNK_SIZE];
    uint8_t *ptr = data;
    int remaining = len;

    ESP_LOGI(TAG, "Sending %d bytes in %d-byte chunks", len, DATA_CHUNK_SIZE);

    while (remaining > 0) {
        int chunk = (remaining > DATA_CHUNK_SIZE) ? DATA_CHUNK_SIZE : remaining;

        // Copy to stack buffer (internal RAM) for reliable DMA
        memcpy(buf, ptr, chunk);

        gpio_set_level(g_cfg.pin_dc, 1);  // DC high = data
        esp_err_t ret = spi_begin();
        if (ret != ESP_OK) return ret;
        gpio_set_level(g_cfg.pin_cs, 0);  // CS low
        ret = spi_write(buf, chunk);
        gpio_set_level(g_cfg.pin_cs, 1);  // CS high
        spi_end();
        if (ret != ESP_OK) return ret;

        ptr += chunk;
        remaining -= chunk;
    }

    ESP_LOGI(TAG, "Buffer send complete");
    return ESP_OK;
}

static bool is_busy(void)
{
    int level = gpio_get_level(g_cfg.pin_busy);
    return level == 0;
}

static esp_err_t wait_busy(const char *label)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    int wait_count = 0;
    while (is_busy()) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (++wait_count > 4000) {  // 40s timeout
            ESP_LOGW(TAG, "[%s] BUSY timeout after 40s", label);
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

// --- Hardware setup ---

static void gpio_init(void)
{
    // Release any pad holds latched by a previous deep-sleep cycle
    // (see epaper_enter_deepsleep) so gpio_config + gpio_set_level
    // below can re-drive these pins.
    gpio_hold_dis(g_cfg.pin_cs);
    gpio_hold_dis(g_cfg.pin_dc);
    gpio_hold_dis(g_cfg.pin_rst);

    // Set desired output levels BEFORE enabling output drivers to avoid glitches
    gpio_set_level(g_cfg.pin_cs, 1);   // CS HIGH = deselected
    gpio_set_level(g_cfg.pin_dc, 0);   // DC LOW = command mode
    gpio_set_level(g_cfg.pin_rst, 1);  // RST HIGH = not in reset

    gpio_config_t out_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << g_cfg.pin_rst) | (1ULL << g_cfg.pin_dc) | (1ULL << g_cfg.pin_cs),
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&out_conf));

    gpio_config_t in_conf = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << g_cfg.pin_busy),
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&in_conf));

    if (g_cfg.pin_enable >= 0) {
        // Release any pad hold latched by a previous deep-sleep cycle
        // (see epaper_enter_deepsleep) so gpio_config + gpio_set_level
        // below can re-drive the enable pin.
        gpio_hold_dis(g_cfg.pin_enable);
        gpio_config_t en_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << g_cfg.pin_enable),
        };
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&en_conf));
        gpio_set_level(g_cfg.pin_enable, 1);
        vTaskDelay(pdMS_TO_TICKS(100));  // allow display power to stabilize
    }
}

static void spi_add_device(void)
{
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = -1,  // CS is manually controlled
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(g_cfg.spi_host, &devcfg, &spi));
}

static void hw_reset(void)
{
    gpio_set_level(g_cfg.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(g_cfg.pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(g_cfg.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

// --- Display operations ---

static esp_err_t send_init_sequence(void)
{
    esp_err_t err;
    if ((err = cmd_data(0xAA, (uint8_t[]){0x49, 0x55, 0x20, 0x08, 0x09, 0x18}, 6)) != ESP_OK) return err;  // CMDH
    if ((err = cmd_data(0x01, (uint8_t[]){0x3F}, 1)) != ESP_OK) return err;                                // PWRR
    if ((err = cmd_data(0x00, (uint8_t[]){0x5F, 0x69}, 2)) != ESP_OK) return err;                          // PSR
    if ((err = cmd_data(0x03, (uint8_t[]){0x00, 0x54, 0x00, 0x44}, 4)) != ESP_OK) return err;              // POFS
    if ((err = cmd_data(0x05, (uint8_t[]){0x40, 0x1F, 0x1F, 0x2C}, 4)) != ESP_OK) return err;              // BTST1
    if ((err = cmd_data(0x06, (uint8_t[]){0x6F, 0x1F, 0x16, 0x25}, 4)) != ESP_OK) return err;              // BTST2 (Seeed_GFX tuned)
    if ((err = cmd_data(0x08, (uint8_t[]){0x6F, 0x1F, 0x1F, 0x22}, 4)) != ESP_OK) return err;              // BTST3
    if ((err = cmd_data(0x30, (uint8_t[]){0x03}, 1)) != ESP_OK) return err;                                // PLL
    if ((err = cmd_data(0x50, (uint8_t[]){0x3F}, 1)) != ESP_OK) return err;                                // CDI
    if ((err = cmd_data(0x60, (uint8_t[]){0x02, 0x00}, 2)) != ESP_OK) return err;                          // TCON
    if ((err = cmd_data(0x61, (uint8_t[]){0x03, 0x20, 0x01, 0xE0}, 4)) != ESP_OK) return err;              // TRES
    if ((err = cmd_data(0x84, (uint8_t[]){0x01}, 1)) != ESP_OK) return err;                                // T_VDCS
    if ((err = cmd_data(0xE3, (uint8_t[]){0x2F}, 1)) != ESP_OK) return err;                                // PWS
    return ESP_OK;
}

// Full display update cycle:
// RESET -> INIT -> wait -> DTM -> DATA -> PON -> wait -> DRF -> wait -> POF -> wait -> DSLP
static esp_err_t display_update_cycle(uint8_t *image)
{
    if (!image) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ESP_OK;
#ifdef CONFIG_PM_ENABLE
    if (pm_lock && (err = esp_pm_lock_acquire(pm_lock)) != ESP_OK) return err;
#endif
    hw_reset();
#define STEP(call) do { err = (call); if (err != ESP_OK) goto done; } while (0)
    STEP(wait_busy("reset"));
    STEP(send_init_sequence());
    STEP(wait_busy("init"));
    STEP(send_command(0x10));
    STEP(send_buffer(image, EPD_BUF_SIZE));
    STEP(wait_busy("data"));
    STEP(send_command(0x04));
    STEP(wait_busy("power_on"));
    STEP(cmd_data(0x12, (uint8_t[]){0x00}, 1));
    STEP(wait_busy("refresh"));
    STEP(cmd_data(0x02, (uint8_t[]){0x00}, 1));
    STEP(wait_busy("power_off"));
    STEP(cmd_data(0x07, (uint8_t[]){0xA5}, 1));
done:
#undef STEP
    if (err != ESP_OK) {
        // Stop at the first fault. Do not continue into a refresh after a failed
        // reset/data phase. Attempt to remove high-voltage power without another
        // BUSY wait; the next request starts with a fresh controller reset.
        cmd_data(0x02, (uint8_t[]){0x00}, 1);
        cmd_data(0x07, (uint8_t[]){0xA5}, 1);
        ESP_LOGE(TAG, "Display cycle failed: %s", esp_err_to_name(err));
    }
#ifdef CONFIG_PM_ENABLE
    if (pm_lock) esp_pm_lock_release(pm_lock);
#endif
    return err;
}

// --- Public API ---

uint16_t epaper_get_width(void)
{
    return EPD_WIDTH;
}

uint16_t epaper_get_height(void)
{
    return EPD_HEIGHT;
}

void epaper_init(const epaper_config_t *cfg)
{
    g_cfg = *cfg;

    ESP_LOGI(TAG, "Initializing ED2208-GCA (Spectra 6) E-Paper Driver");

    spi_add_device();
    gpio_init();

#ifdef CONFIG_PM_ENABLE
    esp_err_t ret = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "epd_update", &pm_lock);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PM lock: %s", esp_err_to_name(ret));
    }
#endif
}

void epaper_clear(uint8_t *image, uint8_t color)
{
    uint8_t packed = (color << 4) | color;
    memset(image, packed, EPD_BUF_SIZE);

    ESP_LOGI(TAG, "Clearing display with color 0x%02x", color);
    if (display_update_cycle(image) == ESP_OK) ESP_LOGI(TAG, "Clear complete");
}

esp_err_t epaper_display_checked(uint8_t *image)
{
    ESP_LOGI(TAG, "Starting display update: %d bytes", EPD_BUF_SIZE);
    esp_err_t result = display_update_cycle(image);
    if (result == ESP_OK) ESP_LOGI(TAG, "Display update complete");
    return result;
}

void epaper_display(uint8_t *image)
{
    (void) epaper_display_checked(image);
}

void epaper_enter_deepsleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep");

#ifdef CONFIG_PM_ENABLE
    if (pm_lock) {
        esp_pm_lock_acquire(pm_lock);
    }
#endif

    // display_update_cycle() already sends POF + DSLP after each update,
    // so the display should already be in deep sleep. Send again to be safe.
    esp_err_t sleep_result = cmd_data(0x02, (uint8_t[]){0x00}, 1);  // POWER_OFF
    if (sleep_result == ESP_OK) sleep_result = wait_busy("deepsleep_power_off");
    esp_err_t deep_sleep_result = cmd_data(0x07, (uint8_t[]){0xA5}, 1);
    if (sleep_result == ESP_OK) sleep_result = deep_sleep_result;
    if (sleep_result != ESP_OK) {
        // The public sleep hook remains void for board compatibility. Surface
        // the fault, then continue the existing rail/pad shutdown sequence.
        ESP_LOGE(TAG, "Panel sleep command failed: %s", esp_err_to_name(sleep_result));
    }

    if (g_cfg.pin_enable >= 0) {
        // Drive panel-facing GPIOs LOW before cutting VDD so they don't
        // back-feed through the panel's ESD diodes once its rail drops to
        // 0V. SPI peripheral pads (MOSI/SCLK) become Hi-Z in deep sleep
        // and don't need handling.
        gpio_set_level(g_cfg.pin_cs, 0);
        gpio_set_level(g_cfg.pin_dc, 0);
        gpio_set_level(g_cfg.pin_rst, 0);
        if (g_cfg.pin_cs1 >= 0) {
            gpio_set_level(g_cfg.pin_cs1, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(100));       // Ensure display enters sleep before cutting power
        gpio_set_level(g_cfg.pin_enable, 0);  // Cut power
        // Latch the pads low so the rail stays cut and panel-facing
        // signals stay grounded once the digital IO domain powers down
        // during deep sleep, and arm deep-sleep hold so the latch
        // survives into deep sleep. All calls are idempotent.
        gpio_hold_en(g_cfg.pin_enable);
        gpio_hold_en(g_cfg.pin_cs);
        gpio_hold_en(g_cfg.pin_dc);
        gpio_hold_en(g_cfg.pin_rst);
        if (g_cfg.pin_cs1 >= 0) {
            gpio_hold_en(g_cfg.pin_cs1);
        }
        gpio_deep_sleep_hold_en();
    }

#ifdef CONFIG_PM_ENABLE
    if (pm_lock) {
        esp_pm_lock_release(pm_lock);
    }
#endif
}
