#pragma once

#define COMM_MSG_SIZE         8
extern QueueHandle_t comm_msg_queue;

void comm_task_init();
