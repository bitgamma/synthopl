#include "opl_srv.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OPL_SRV_STACK_SIZE 512

static TaskHandle_t opl_srv_task;

void opl_srv_run(void *param) {
  while(1) {

  }
}

void opl_srv_start() {
  xTaskCreatePinnedToCore(opl_srv_run, "opl_srv", OPL_SRV_STACK_SIZE, NULL, 0, &opl_srv_task, 1);
}