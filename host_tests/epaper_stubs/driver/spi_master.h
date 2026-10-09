#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
typedef int spi_host_device_t;
typedef void *spi_device_handle_t;
typedef struct { size_t length, rxlength; const void *tx_buffer; unsigned flags; uint16_t cmd; } spi_transaction_t;
typedef struct { unsigned command_bits; spi_transaction_t base; } spi_transaction_ext_t;
typedef struct { int clock_speed_hz,mode,spics_io_num,queue_size; unsigned flags; } spi_device_interface_config_t;
#define SPI_TRANS_VARIABLE_CMD 1
#define SPI_DEVICE_HALFDUPLEX 2
#define SPI_DEVICE_NO_DUMMY 4
#define portMAX_DELAY 0x7fffffff
#define ESP_ERROR_CHECK(err) ((void)(err))
#define ESP_ERROR_CHECK_WITHOUT_ABORT(err) ((void)(err))
esp_err_t spi_device_acquire_bus(spi_device_handle_t, TickType_t);
void spi_device_release_bus(spi_device_handle_t);
esp_err_t spi_device_polling_start(spi_device_handle_t, spi_transaction_t *, TickType_t);
esp_err_t spi_device_polling_end(spi_device_handle_t, TickType_t);
esp_err_t spi_bus_add_device(spi_host_device_t, const spi_device_interface_config_t *, spi_device_handle_t *);
void vTaskDelay(TickType_t ticks);
