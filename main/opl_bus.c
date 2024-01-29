#include "opl_bus.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"

#define OPL_NRESET_PIN 4
#define OPL_NWRITE_PIN 5
#define OPL_ADDR_DATA_PIN 6
#define OPL_ADDR_HIGH_PIN 7
#define OPL_DATA_LATCH_PIN 10
#define OPL_DATA_OUT_PIN 11
#define OPL_DATA_CLK_PIN 12

#define OPL_RESET_DELAY_US 30
#define OPL_ADDR_DATA_DELAY_US 3

#define OPL_SPI SPI2_HOST

static spi_device_handle_t spi;

esp_err_t opl_bus_init() {
  esp_err_t ret;

  gpio_config_t io_conf = {};
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1 << OPL_NRESET_PIN) | (1 << OPL_NWRITE_PIN) | (1 << OPL_ADDR_DATA_PIN) | (1 << OPL_ADDR_HIGH_PIN);
  ret = gpio_config(&io_conf);
  ESP_ERROR_CHECK(ret);

  gpio_set_level(OPL_NRESET_PIN, 0);
  gpio_set_level(OPL_NWRITE_PIN, 1);
  gpio_set_level(OPL_ADDR_DATA_PIN, 0);
  gpio_set_level(OPL_ADDR_HIGH_PIN, 0);
    
  spi_bus_config_t buscfg = {
    .miso_io_num = -1,
    .mosi_io_num = OPL_DATA_OUT_PIN,
    .sclk_io_num = OPL_DATA_CLK_PIN,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1
  };

  spi_device_interface_config_t devcfg = {
    .clock_speed_hz = 20 * 1000 * 1000,
    .mode = 0,
    .spics_io_num = OPL_DATA_LATCH_PIN,
    .queue_size = 1,
  };

  ret = spi_bus_initialize(OPL_SPI, &buscfg, SPI_DMA_DISABLED);
  ESP_ERROR_CHECK(ret);
  ret = spi_bus_add_device(OPL_SPI, &devcfg, &spi);
  ESP_ERROR_CHECK(ret);
  ret = spi_device_acquire_bus(spi, portMAX_DELAY);
  ESP_ERROR_CHECK(ret);

  ets_delay_us(OPL_RESET_DELAY_US);
  gpio_set_level(OPL_NRESET_PIN, 1);

  return ESP_OK;
}

esp_err_t opl_bus_write(uint16_t addr, uint8_t data) {
  esp_err_t ret;

  spi_transaction_t tx = {
    .length = 1,
    .flags = SPI_TRANS_USE_TXDATA
  };

  gpio_set_level(OPL_ADDR_DATA_PIN, 0);
  gpio_set_level(OPL_ADDR_HIGH_PIN, (addr >> 15)); 
  tx.tx_data[0] = (uint8_t)(addr & 0xff);
  ret = spi_device_transmit(spi, &tx);
  ESP_ERROR_CHECK(ret);
  
  gpio_set_level(OPL_NWRITE_PIN, 0);
  ets_delay_us(OPL_ADDR_DATA_DELAY_US);
  gpio_set_level(OPL_NWRITE_PIN, 1);

  gpio_set_level(OPL_ADDR_DATA_PIN, 1);
  tx.tx_data[0] = data;
  ret = spi_device_transmit(spi, &tx);
  ESP_ERROR_CHECK(ret);

  gpio_set_level(OPL_NWRITE_PIN, 0);
  ets_delay_us(OPL_ADDR_DATA_DELAY_US);
  gpio_set_level(OPL_NWRITE_PIN, 1);

  return ESP_OK;
}