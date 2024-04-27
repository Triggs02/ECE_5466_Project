#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"

#include "uart_task.hpp"

#define COMM_MSG_QUEUE_DEPTH  10
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
#define COMM_TASK_STACK_SIZE    2048
QueueHandle_t comm_msg_queue = NULL;

static const char *TAG = "UART COMM";

static void uart_comm_task(void *arg)
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

    // Configure a temporary buffer for the incoming data
    const size_t rx_buf_size = 32;
    uint8_t *rx_buf = (uint8_t *) malloc(rx_buf_size);
    uint8_t *partial_buf = (uint8_t *) malloc(COMM_MSG_RAW_SIZE);
    size_t partial_idx = 0;
    bool needs_sync = true;
    bool is_escaped = false;

    while (1) {
        // Read data from the UART
        int len = uart_read_bytes(COMM_UART_PORT_NUM, rx_buf, rx_buf_size, 50 / portTICK_PERIOD_MS);
        
        uint8_t* cur_byte_ptr = rx_buf;
        while (len--) {
          uint8_t cur_byte = *cur_byte_ptr++;

          // Handle framing characters
          if (cur_byte == COMM_SYNC_CHAR) {
            needs_sync = false;
            is_escaped = false;
            partial_idx = 0;
            continue;
          }
          else if (needs_sync) {
            // Don't process any characters until we get a sync char
            continue;
          }
          else if (is_escaped) {
            if (cur_byte == COMM_ESCAPE_ESCAPE) {
              cur_byte = COMM_ESCAPE_CHAR;
            }
            else if (cur_byte == COMM_ESCAPE_SYNC) {
              cur_byte = COMM_SYNC_CHAR;
            }
            else {
              needs_sync = true;
              ESP_LOGW(TAG, "Unexpected escaped token: %d - requiring resync", cur_byte);
              continue;
            }
          }
          else if (cur_byte == COMM_ESCAPE_CHAR) {
            is_escaped = true;
            continue;
          }

          // Anything else is a decode success, add to buffer
          partial_buf[partial_idx++] = cur_byte;
          if (partial_idx == COMM_MSG_RAW_SIZE) {
            // Clear the state to begin receiving a new messgae when we finish
            needs_sync = true;
            partial_idx = 0;

            // We got a full message, process it

            // Compute message checksum
            uint8_t checksum = 0x32;
            for (int i = 0; i < COMM_MSG_RAW_SIZE - 1; i++) {
              checksum += partial_buf[i];
            }
            checksum ^= 0x5A;

            // Make sure the checksum matches
            if (partial_buf[COMM_MSG_RAW_SIZE - 1] != checksum) {
              ESP_LOGW(TAG, "Invalid Checksum: %d computed, %d received", checksum, partial_buf[COMM_MSG_RAW_SIZE - 1]);
            }
            else {
              // We received it, push it to the queue
              BaseType_t ret = xQueueSendToBack(comm_msg_queue, partial_buf, 50 / portTICK_PERIOD_MS);
              if (ret != pdPASS) {
                ESP_LOGW(TAG, "Message lost, could not push to queue (error %d)", ret);
              }
            }

          }
        }
    }
}

void comm_task_init()
{
    comm_msg_queue = xQueueCreate(COMM_MSG_QUEUE_DEPTH, COMM_MSG_SIZE);
    assert(comm_msg_queue != NULL);
    BaseType_t ret = xTaskCreate(uart_comm_task, "uart_comm_task", COMM_TASK_STACK_SIZE, NULL, 10, NULL);
    assert(ret == pdPASS);
}
