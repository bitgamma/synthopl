#include "midi_srv.h"
#include "freertos/FreeRTOSConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "soc/uart_channel.h"
#include "esp_log.h"

#define MIDI_SRV_STACK_SIZE 8096
#define MIDI_UART UART_NUM_1
#define MIDI_UART_RX_PIN UART_NUM_1_RXD_DIRECT_GPIO_NUM
#define RECV_BUF_SIZE 512
#define RECV_TIMEOUT 1

#define MIDI_NOTE_OFF 0x80
#define MIDI_NOTE_ON 0x90
#define MIDI_POLY_PRESSURE 0xa0
#define MIDI_CTRL_CHANGE 0xb0
#define MIDI_PRG_CHANGE 0xc0
#define MIDI_CHAN_PRESSURE 0xd0
#define MIDI_PITCH_BEND 0xe0
#define MIDI_SYSTEM 0xf0

static const char *TAG = "midi_srv";
static TaskHandle_t midi_srv_task;

void midi_srv_run(void *param) {
  ESP_LOGI(TAG, "ready");

  while(1) {
    uint8_t status;
    uint8_t data[2];

    if (uart_read_bytes(MIDI_UART, &status, 1, portMAX_DELAY) < 1) {
      continue;
    }

    switch(status & 0xf0) {
      case MIDI_NOTE_OFF:
        uart_read_bytes(MIDI_UART, &data, 2, pdTICKS_TO_MS(RECV_TIMEOUT));
        ESP_LOGI(TAG, "Note Off: %d velocity: %d, ch: %d", data[0], data[1], (status & 0xf));
        break;
      case MIDI_NOTE_ON:
        uart_read_bytes(MIDI_UART, &data, 2, pdTICKS_TO_MS(RECV_TIMEOUT));
        ESP_LOGI(TAG, "Note On: %d velocity: %d, ch: %d", data[0], data[1], (status & 0xf));
        break;
      case MIDI_POLY_PRESSURE:
        uart_read_bytes(MIDI_UART, &data, 2, pdTICKS_TO_MS(RECV_TIMEOUT));
        ESP_LOGI(TAG, "Polyacustic Pressure: %d pressure: %d, ch: %d", data[0], data[1], (status & 0xf));
        break;        
      case MIDI_CTRL_CHANGE:
        uart_read_bytes(MIDI_UART, &data, 2, pdTICKS_TO_MS(RECV_TIMEOUT));
        ESP_LOGI(TAG, "Control Change: %d value: %d, ch: %d", data[0], data[1], (status & 0xf));
        break;
      case MIDI_PRG_CHANGE:
        uart_read_bytes(MIDI_UART, &data, 1, pdTICKS_TO_MS(RECV_TIMEOUT));
        ESP_LOGI(TAG, "Program Change: %d ch: %d", data[0], (status & 0xf));
        break;
      case MIDI_CHAN_PRESSURE:
        uart_read_bytes(MIDI_UART, &data, 1, pdTICKS_TO_MS(RECV_TIMEOUT));
        ESP_LOGI(TAG, "Channel Pressure: %d ch: %d", data[0], (status & 0xf));
        break;
      case MIDI_PITCH_BEND:
        uart_read_bytes(MIDI_UART, &data, 2, pdTICKS_TO_MS(RECV_TIMEOUT));
        ESP_LOGI(TAG, "Pitch Bend: %d ch: %d", (data[0] | (data[1] << 7)), (status & 0xf));
        break;
      case MIDI_SYSTEM:
        ESP_LOGI(TAG, "System Message: %x", status);
        break;   
      default:
        ESP_LOGI(TAG, "Skipped data byte: %x ", status);
        break;     
    }
  }
}

void midi_srv_start() {
  esp_log_level_set(TAG, ESP_LOG_INFO);

  uart_config_t uart_config = {
      .baud_rate = 31250,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  uart_driver_install(MIDI_UART, RECV_BUF_SIZE, 0, 0, NULL, 0);
  uart_param_config(MIDI_UART, &uart_config);

  uart_set_pin(MIDI_UART, UART_PIN_NO_CHANGE, MIDI_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  xTaskCreatePinnedToCore(midi_srv_run, "midi_srv", MIDI_SRV_STACK_SIZE, NULL, 12, &midi_srv_task, 1);
}