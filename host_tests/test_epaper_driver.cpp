#include <gtest/gtest.h>
#include <vector>
extern "C" {
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "epaper.h"
static int busy_command, last_command, starts, fail_start, fail_end, acquire_fail;
static int acquired, released, cs_level;
static unsigned delayed;
static std::vector<int> commands;
const char *esp_err_to_name(esp_err_t) { return "mock_error"; }
esp_err_t gpio_set_level(int pin,int level) { if(pin==10)cs_level=level; return ESP_OK; }
int gpio_get_level(int) { return busy_command==-2 || last_command==busy_command ? 0 : 1; }
esp_err_t gpio_config(const gpio_config_t*) { return ESP_OK; }
esp_err_t gpio_hold_dis(int) { return ESP_OK; }
esp_err_t gpio_hold_en(int) { return ESP_OK; }
void gpio_deep_sleep_hold_en(void) {}
esp_err_t spi_bus_add_device(spi_host_device_t,const spi_device_interface_config_t*,spi_device_handle_t *spi) { *spi=(void*)1;return ESP_OK; }
esp_err_t spi_device_acquire_bus(spi_device_handle_t,TickType_t ticks) { EXPECT_NE(ticks,portMAX_DELAY); if(acquire_fail)return ESP_ERR_TIMEOUT; ++acquired;return ESP_OK; }
void spi_device_release_bus(spi_device_handle_t) { ++released; }
esp_err_t spi_device_polling_start(spi_device_handle_t,spi_transaction_t *t,TickType_t ticks) {
    EXPECT_EQ(ticks,portMAX_DELAY); ++starts;
    if(starts==fail_start)return ESP_FAIL;
    if(t->flags&SPI_TRANS_VARIABLE_CMD){ last_command=t->cmd;commands.push_back(last_command); }
    return ESP_OK;
}
esp_err_t spi_device_polling_end(spi_device_handle_t,TickType_t) { return starts==fail_end ? ESP_FAIL : ESP_OK; }
void vTaskDelay(TickType_t ticks) { delayed+=ticks; }
}
class EpaperDriverTest : public ::testing::Test {
protected:
    std::vector<uint8_t> image;
    void SetUp() override {
        busy_command=-1; last_command=-3; starts=fail_start=fail_end=acquire_fail=0;
        acquired=released=0; delayed=0; cs_level=1;commands.clear();image.assign(192000,0x11);
        epaper_config_t cfg={};cfg.pin_cs=10;cfg.pin_busy=13;cfg.pin_enable=-1;epaper_init(&cfg);
    }
    bool Sent(int cmd){return std::find(commands.begin(),commands.end(),cmd)!=commands.end();}
};
TEST_F(EpaperDriverTest, SuccessfulCycleReturnsSuccessAndSleeps) {
    EXPECT_EQ(epaper_display_checked(image.data()),ESP_OK);EXPECT_TRUE(Sent(0x12));EXPECT_EQ(last_command,0x07);
    EXPECT_EQ(acquired,released);EXPECT_EQ(cs_level,1);
}
TEST_F(EpaperDriverTest, ResetBusyTimeoutNeverRefreshes) {
    busy_command=-2;EXPECT_EQ(epaper_display_checked(image.data()),ESP_ERR_TIMEOUT);EXPECT_FALSE(Sent(0x12));
    EXPECT_LT(delayed,41000u);EXPECT_EQ(acquired,released);
}
TEST_F(EpaperDriverTest, RefreshTimeoutReturnsErrorAndPowersOff) {
    busy_command=0x12;EXPECT_EQ(epaper_display_checked(image.data()),ESP_ERR_TIMEOUT);
    EXPECT_TRUE(Sent(0x12));EXPECT_EQ(last_command,0x07);EXPECT_LT(delayed,41000u);
}
TEST_F(EpaperDriverTest, SpiStartFailureDoesNotRefreshAndReleasesBus) {
    fail_start=1;EXPECT_EQ(epaper_display_checked(image.data()),ESP_FAIL);EXPECT_FALSE(Sent(0x12));
    EXPECT_EQ(acquired,released);EXPECT_EQ(cs_level,1);
}
TEST_F(EpaperDriverTest, SpiEndFailurePropagates) {
    fail_end=1;EXPECT_EQ(epaper_display_checked(image.data()),ESP_FAIL);EXPECT_FALSE(Sent(0x12));
    EXPECT_EQ(acquired,released);EXPECT_EQ(cs_level,1);
}
TEST_F(EpaperDriverTest, BusAcquireTimeoutDoesNotTransmit) {
    acquire_fail=1;EXPECT_EQ(epaper_display_checked(image.data()),ESP_ERR_TIMEOUT);EXPECT_EQ(starts,0);
    EXPECT_EQ(acquired,released);
}
TEST_F(EpaperDriverTest, NullFrameRejected) { EXPECT_EQ(epaper_display_checked(nullptr),ESP_ERR_INVALID_ARG);EXPECT_EQ(starts,0); }
