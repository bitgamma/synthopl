#ifndef __OPL_BUS__
#define __OPL_BUS__

#include "esp_err.h"

esp_err_t opl_bus_init();
esp_err_t opl_bus_reset();
esp_err_t opl_bus_write(uint16_t addr, uint8_t data);

#endif