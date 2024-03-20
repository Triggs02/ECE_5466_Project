#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"

#include "uart_task.hpp"

#define COMM_MSG_SIZE         8
#define COMM_MSG_RAW_SIZE     COMM_MSG_SIZE + 1
#define COMM_SYNC_CHAR        0xA5
#define COMM_ESCAPE_CHAR      0x5A
#define COMM_ESCAPE_ESCAPE    0x23
#define COMM_ESCAPE_SYNC      0xB4

#define COMM_LINK_TXD 4
#define COMM_LINK_RXD 5
#define COMM_LINK_RTS UART_PIN_NO_CHANGE
#define COMM_LINK_CTS UART_PIN_NO_CHANGE

#define COMM_UART_PORT_NUM      1
#define COMM_UART_BAUD_RATE     115200

static const char *TAG = "UART TX";

void comm_tx_init()
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = COMM_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(COMM_UART_PORT_NUM, 1024, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(COMM_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(COMM_UART_PORT_NUM, COMM_LINK_TXD, COMM_LINK_RXD, COMM_LINK_RTS, COMM_LINK_CTS));
}

void comm_tx_msg(uint8_t id, uint8_t* msg, size_t len) {
    // Generate message
    uint8_t msg_buf_local[COMM_MSG_RAW_SIZE] = {0};
    memcpy(msg_buf_local, msg, (len > COMM_MSG_SIZE - 1 ? COMM_MSG_SIZE - 1 : len));
    msg_buf_local[COMM_MSG_SIZE - 1] = id;

    // Compute Checksum
    uint8_t checksum = 0x32;
    for (int i = 0; i < COMM_MSG_SIZE; i++) {
      checksum += msg_buf_local[i];
    }
    checksum ^= 0x5A;
    msg_buf_local[COMM_MSG_SIZE] = checksum;

    // Escape the proper characters
    uint8_t msg_buf_encoded[1 + COMM_MSG_RAW_SIZE * 2];
    msg_buf_encoded[0] = COMM_SYNC_CHAR;
    size_t encoded_len = 1;
    for (int i = 0; i < COMM_MSG_RAW_SIZE; i++) {
      uint8_t msg_data = msg_buf_local[i];
      if (msg_data == COMM_SYNC_CHAR) {
        msg_buf_encoded[encoded_len++] = COMM_ESCAPE_CHAR;
        msg_buf_encoded[encoded_len++] = COMM_ESCAPE_SYNC;
      }
      else if (msg_data == COMM_ESCAPE_CHAR) {
        msg_buf_encoded[encoded_len++] = COMM_ESCAPE_CHAR;
        msg_buf_encoded[encoded_len++] = COMM_ESCAPE_ESCAPE;
      }
      else {
        msg_buf_encoded[encoded_len++] = msg_data;
      }
    }

    uart_write_bytes(COMM_UART_PORT_NUM, msg_buf_encoded, encoded_len);
}