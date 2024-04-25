/*
 * sendData.c
 *
 *  Created on: Mar 2, 2024
 *      Author: rjp5t
 */

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <pthread.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "ti_ble_config.h"
#include <ti/drivers/BatteryMonitor.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/Temperature.h>
#include <ti/bleapp/ble_app_util/inc/bleapputil_api.h>
#include <ti/ble5stack_flash/inc/gatt.h>
#include <app_main.h>
#include <ti/bleapp/menu_module/menu_module.h>

// Service UUID: 1974e0a6-a490-4869-84b7-5f03cf47ac9d
const static uint8_t serviceUuid[] = {0x9D, 0xAC, 0x47, 0xCF, 0x03, 0x5F, 0xB7, 0x84, 0x69, 0x48, 0x90, 0xA4, 0xA6, 0xE0, 0x74, 0x19};

// Characteristic UUID: 1974e0a7-a490-4869-84b7-5f03cf47ac9d
const static uint8_t characteristicUuid[] = {0x9D, 0xAC, 0x47, 0xCF, 0x03, 0x5F, 0xB7, 0x84, 0x69, 0x48, 0x90, 0xA4, 0xA7, 0xE0, 0x74, 0x19};

// Note MAC address must be in reverse
// TODO: Make this param dynamic
//const static uint8_t target_addr[B_ADDR_LEN] = {0x56, 0x62, 0xD5, 0x7E, 0xB9, 0x94};  // My thing
const static uint8_t target_addr[B_ADDR_LEN] = {0x3E, 0x91, 0xA8, 0xFC, 0x8A, 0xD4};

enum async_task_phase {
    ASYNC_PHASE_IDLE = 0,
    ASYNC_PHASE_CONNECT,
    ASYNC_PHASE_SRV_DISCOVER,
    ASYNC_PHASE_CHR_DISCOVER,
    ASYNC_PHASE_WRITE_VALUE,

    ASYNC_PHASE_DISCONNECT
};

#define ErrorSrcOS 0
#define ErrorSrcQueue 1
#define ErrorSrcInvoke 2
#define ErrorSrcQueueFetch 3
#define ErrorSrcInvalidQueueOpcode 4

enum notify_error_src {
    NOTIFY_ERRSRC_BLE_RETCODE = 0,
    NOTIFY_ERRSRC_CONN_CANCELLED = 1,
    NOTIFY_ERRSRC_GATT_ERROR_REPORT = 2,
    NOTIFY_ERRSRC_GATT_ERROR_STATUS = 3,
    NOTIFY_ERRSRC_BLE_STACK_ERROR = 4,
    NOTIFY_ERRSRC_NO_ENTRIES_FOUND = 5,
};


static QueueHandle_t connHandleQueue;
static QueueHandle_t readingEventQueue;
static pthread_t sendDataThread;
static uint16_t connHandleCached = 0xFFFF;
static uint16_t attHandleCached = 0;

#define REPORT_OPCODE_ERROR 0
#define REPORT_OPCODE_CONNECTED 1
#define REPORT_OPCODE_SRV_DISCOVERY 2
#define REPORT_OPCODE_CHR_DISCOVERY 3
#define REPORT_OPCODE_WRITE_DONE 4
#define REPORT_OPCODE_DISCONNECT 5

typedef struct async_task_report_t {
    uint8_t opcode;
    union val {
        uint32_t errorCode;
        uint16_t connHandle;
        struct service_discovery {
            uint16_t startHdl;
            uint16_t endHdl;
        } service_discovery;
        uint16_t chrHandle;
    } data;
} async_task_report_t;

static enum async_task_phase current_phase = ASYNC_PHASE_IDLE;


static void SendNotifyReport(async_task_report_t* report) {
    assert(uxQueueMessagesWaiting(connHandleQueue) == 0);
    assert(xQueueSendToBack(connHandleQueue, report, 0) == pdPASS);
}

static void SendDataNotifyConnect(uint16_t connHandle) {
    async_task_report_t report;
    report.opcode = REPORT_OPCODE_CONNECTED;
    report.data.connHandle = connHandle;
    SendNotifyReport(&report);
}

static void SendDataNotifyServiceDiscover(uint16_t svcStartHdl, uint16_t svcEndHdl) {
    async_task_report_t report;
    report.opcode = REPORT_OPCODE_SRV_DISCOVERY;
    report.data.service_discovery.startHdl = svcStartHdl;
    report.data.service_discovery.endHdl = svcEndHdl;
    SendNotifyReport(&report);
}

static void SendDataNotifyChrDiscover(uint16_t chrHandle) {
    async_task_report_t report;
    report.opcode = REPORT_OPCODE_CHR_DISCOVERY;
    report.data.chrHandle = chrHandle;
    SendNotifyReport(&report);
}

static void SendDataNotifyWriteDone() {
    async_task_report_t report;
    report.opcode = REPORT_OPCODE_WRITE_DONE;
    SendNotifyReport(&report);
}

void SendDataNotifyDisconnect() {
    async_task_report_t report;
    report.opcode = REPORT_OPCODE_DISCONNECT;
    SendNotifyReport(&report);
}

void SendDataNotifyError(enum notify_error_src src, uint16_t errorCode) {
    async_task_report_t report;
    report.opcode = REPORT_OPCODE_ERROR;
    report.data.errorCode = (((uint32_t) src) << 16) | ((uint32_t)errorCode);
    SendNotifyReport(&report);
}


static void SendData_GattHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData);
static void SendData_ConnectionHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData);

BLEAppUtil_EventHandler_t gattEventHandler =
{
    .handlerType    = BLEAPPUTIL_GATT_TYPE,
    .pEventHandler  = SendData_GattHandler,
    .eventMask      = BLEAPPUTIL_ATT_ERROR_RSP |
                      BLEAPPUTIL_ATT_FIND_BY_TYPE_VALUE_RSP |
                      BLEAPPUTIL_ATT_READ_BY_TYPE_RSP |
                      BLEAPPUTIL_ATT_WRITE_RSP
};

BLEAppUtil_EventHandler_t sendDataConnHandler =
{
    .handlerType    = BLEAPPUTIL_GAP_CONN_TYPE,
    .pEventHandler  = SendData_ConnectionHandler,
    .eventMask      = BLEAPPUTIL_LINK_ESTABLISHED_EVENT |
                      BLEAPPUTIL_LINK_TERMINATED_EVENT |
                      BLEAPPUTIL_CONNECTING_CANCELLED_EVENT
};

static void SendData_ConnectionHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData) {
    switch(event)
        {
            case BLEAPPUTIL_LINK_ESTABLISHED_EVENT:
            {
                if (current_phase == ASYNC_PHASE_CONNECT) {
                    gapEstLinkReqEvent_t *gapEstMsg = (gapEstLinkReqEvent_t *)pMsgData;
                    SendDataNotifyConnect(gapEstMsg->connectionHandle);
                }

                break;
            }

            case BLEAPPUTIL_LINK_TERMINATED_EVENT:
            {
                if (current_phase == ASYNC_PHASE_DISCONNECT) {
                    SendDataNotifyDisconnect();
                }
            }

            case BLEAPPUTIL_CONNECTING_CANCELLED_EVENT:
            {
                if (current_phase == ASYNC_PHASE_CONNECT) {
                    gapConnCancelledEvent_t *gapCancelledMsg = (gapConnCancelledEvent_t *)pMsgData;
                    SendDataNotifyError(NOTIFY_ERRSRC_CONN_CANCELLED, gapCancelledMsg->opcode);
                }

            }

            default:
            {
                break;
            }
        }
}

static void SendData_GattHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsg) {
    gattMsgEvent_t * pMsgData = (gattMsgEvent_t *)pMsg;

    switch (event) {
        case BLEAPPUTIL_ATT_ERROR_RSP:
        {
            if (pMsgData->hdr.status == SUCCESS) {
                SendDataNotifyError(NOTIFY_ERRSRC_GATT_ERROR_REPORT, pMsgData->msg.errorRsp.errCode);
            }
            else {
                SendDataNotifyError(NOTIFY_ERRSRC_GATT_ERROR_STATUS, pMsgData->hdr.status);
            }
            break;
        }
        case BLEAPPUTIL_ATT_FIND_BY_TYPE_VALUE_RSP:
        {
            if (current_phase == ASYNC_PHASE_SRV_DISCOVER) {
                // Needs to be SUCCESS to work?? IDK since the docs say bleProcedureComplete, just accept both
                if (pMsgData->hdr.status == bleProcedureComplete || pMsgData->hdr.status == SUCCESS) {
                    if (pMsgData->msg.findByTypeValueRsp.numInfo == 1) {
                        uint16_t svcStartHdl = ATT_ATTR_HANDLE(pMsgData->msg.findByTypeValueRsp.pHandlesInfo, 0);
                        uint16_t svcEndHdl = ATT_GRP_END_HANDLE(pMsgData->msg.findByTypeValueRsp.pHandlesInfo, 0);
                        SendDataNotifyServiceDiscover(svcStartHdl, svcEndHdl);
                    }
                    else {
                        SendDataNotifyError(NOTIFY_ERRSRC_NO_ENTRIES_FOUND, pMsgData->msg.findByTypeValueRsp.numInfo);
                    }
                }
                else {
                    SendDataNotifyError(NOTIFY_ERRSRC_BLE_STACK_ERROR, pMsgData->hdr.status);
                }
            }
            break;
        }
        case BLEAPPUTIL_ATT_READ_BY_TYPE_RSP:
        {
            if (current_phase == ASYNC_PHASE_CHR_DISCOVER) {
                // Again, same as above, it reports SUCCESS rather than bleProcedureComplete, just accept both again
                if (pMsgData->hdr.status == bleProcedureComplete || pMsgData->hdr.status == SUCCESS) {
                    if (pMsgData->msg.readByTypeRsp.numPairs == 1) {
                        uint16_t chrHandle = ATT_PAIR_HANDLE(pMsgData->msg.readByTypeRsp.pDataList, 0);

                        SendDataNotifyChrDiscover(chrHandle);
                    }
                    else {
                        SendDataNotifyError(NOTIFY_ERRSRC_NO_ENTRIES_FOUND, pMsgData->msg.readByTypeRsp.numPairs );
                    }
                }
                else {
                    SendDataNotifyError(NOTIFY_ERRSRC_BLE_STACK_ERROR, pMsgData->hdr.status);
                }
            }
        }

        case BLEAPPUTIL_ATT_WRITE_RSP:
        {
            if (current_phase == ASYNC_PHASE_WRITE_VALUE) {
                // Again, same as above, it reports SUCCESS rather than bleProcedureComplete, just accept both again
                if (pMsgData->hdr.status == SUCCESS) {
                    SendDataNotifyWriteDone();
                }
                else {
                    SendDataNotifyError(NOTIFY_ERRSRC_BLE_STACK_ERROR, pMsgData->hdr.status);
                }
            }
        }

        default:
            break;
    }
}

#define CheckBleCallFromAsync(func)  { \
        bStatus_t status = (func); \
        if (status != SUCCESS) { \
            SendDataNotifyError(NOTIFY_ERRSRC_BLE_RETCODE, status); \
        } \
    }

static void SendData_Connect(char *pData) {
    CheckBleCallFromAsync(BLEAppUtil_connect((BLEAppUtil_ConnectParams_t*)pData));
}

static void SendData_DiscoverServce(char *pData) {
    CheckBleCallFromAsync(GATT_DiscPrimaryServiceByUUID(connHandleCached, serviceUuid, sizeof(serviceUuid), BLEAppUtil_getSelfEntity()));
}

static void SendData_DiscoverCharacteristic(char *pData) {
    CheckBleCallFromAsync(GATT_DiscCharsByUUID(connHandleCached, (attReadByTypeReq_t*)pData, BLEAppUtil_getSelfEntity()));
}

static void SendData_WriteCharacteristic(char *pData) {
    bStatus_t status = GATT_WriteCharValue(connHandleCached, (attWriteReq_t*)pData, BLEAppUtil_getSelfEntity());
    if (status != SUCCESS) {
        GATT_bm_free((gattMsg_t*) pData, ATT_WRITE_REQ);
        SendDataNotifyError(NOTIFY_ERRSRC_BLE_RETCODE, status);
    }
}

static void SendData_Disconnect(char *pData) {
    CheckBleCallFromAsync(BLEAppUtil_disconnect(connHandleCached));
    connHandleCached = 0xFFFF;
}

static void resetWithBigHammer() {
    // These are private undocumented functions in the internals of the SimpleLink SDK
    // However, they won't make it work nicely, so I'll have to do this I guess
    extern void gattResetClientInfo(void *pClient);
    extern void* gattFindClientInfo(uint16 connHandle);

    void* pClient = gattFindClientInfo(connHandleCached);
    if (pClient) {
        gattResetClientInfo(pClient);
    }
}

#define CheckOSError(cond, unique_id) \
    if (!(cond)) { \
        enum async_task_phase last_phase = current_phase; \
        current_phase = ASYNC_PHASE_IDLE; \
        rc = (last_phase << 28) | (ErrorSrcOS << 24) | (unique_id & 0xFFFFFF); \
        goto disconnect; \
    }

#define CheckInvokeStatus(func) { \
        bStatus_t status = (func); \
        if (status != SUCCESS) { \
            enum async_task_phase last_phase = current_phase; \
            current_phase = ASYNC_PHASE_IDLE; \
            rc = (last_phase << 28) | (ErrorSrcInvoke << 24) | (status & 0xFFFFFF); \
            goto disconnect; \
        } \
    }

#define QueueGetResult(expected_opcode) { \
        BaseType_t rcTmp = xQueueReceive(connHandleQueue, &report, portMAX_DELAY); \
        if (rcTmp != pdPASS) { \
            enum async_task_phase last_phase = current_phase; \
            current_phase = ASYNC_PHASE_IDLE; \
            rc = (last_phase << 28) | (ErrorSrcQueueFetch << 24) | (rc & 0xFFFFFF); \
            goto disconnect; \
        } \
        if (report.opcode == REPORT_OPCODE_ERROR) { \
            enum async_task_phase last_phase = current_phase; \
            current_phase = ASYNC_PHASE_IDLE; \
            rc = (last_phase << 28) | (ErrorSrcQueue << 24) | (report.data.errorCode & 0xFFFFFF); \
            goto disconnect; \
        } \
        else if (report.opcode != expected_opcode) { \
            enum async_task_phase last_phase = current_phase; \
            current_phase = ASYNC_PHASE_IDLE; \
            rc = (last_phase << 28) | (ErrorSrcInvalidQueueOpcode << 24); \
            goto disconnect; \
        } \
    }

uint32_t SendDataUpdate(uint32_t voc, uint16_t temp, uint8_t batteryLevel) {
    async_task_report_t report;
    uint32_t rc = 0;

    while(uxQueueMessagesWaiting(connHandleQueue) != 0) {
        xQueueReceive(connHandleQueue, &report, 0);
    }

    // Set the connection parameters to connect to the base station and send the connect request
    current_phase = ASYNC_PHASE_CONNECT;
    {
        BLEAppUtil_ConnectParams_t *connParams = ICall_malloc(sizeof(BLEAppUtil_ConnectParams_t));
        CheckOSError(connParams != NULL, 2);
        connParams->peerAddrType = ADDRTYPE_PUBLIC;
        connParams->phys = INIT_PHY_1M;
        connParams->timeout = 1000;
        memcpy(connParams->pPeerAddress, target_addr, B_ADDR_LEN);
        CheckInvokeStatus(BLEAppUtil_invokeFunction(SendData_Connect, (char*)connParams));
    }

    // Wait for the status to come back from the connect request
    QueueGetResult(REPORT_OPCODE_CONNECTED);
    connHandleCached = report.data.connHandle;

    if (attHandleCached == 0) {
        // Discover the service
        current_phase = ASYNC_PHASE_SRV_DISCOVER;
        CheckInvokeStatus(BLEAppUtil_invokeFunctionNoData(SendData_DiscoverServce));
        QueueGetResult(REPORT_OPCODE_SRV_DISCOVERY);
        uint16_t srvStartHdl = report.data.service_discovery.startHdl;
        uint16_t srvEndHdl = report.data.service_discovery.endHdl;

        resetWithBigHammer();
        vTaskDelay(configTICK_RATE_HZ);

        // Now that we know the service range, discover the characteristic that we want
        current_phase = ASYNC_PHASE_CHR_DISCOVER;
        {
            // Create the characteristic discovery message and send the message over
            attReadByTypeReq_t *charReadReq = ICall_malloc(sizeof(attReadByTypeReq_t));
            charReadReq->startHandle = srvStartHdl;
            charReadReq->endHandle = srvEndHdl;
            charReadReq->type.len = sizeof(characteristicUuid);
            static_assert(sizeof(characteristicUuid) <= sizeof(charReadReq->type.uuid), "UUID doesn't fit into UUID list");
            memcpy(charReadReq->type.uuid, characteristicUuid, sizeof(characteristicUuid));
            CheckInvokeStatus(BLEAppUtil_invokeFunction(SendData_DiscoverCharacteristic, (char*)charReadReq));
        }
        QueueGetResult(REPORT_OPCODE_CHR_DISCOVERY);
        uint16_t chrHandle = report.data.chrHandle;

        resetWithBigHammer();
        vTaskDelay(configTICK_RATE_HZ);

        attHandleCached = chrHandle + 1;
    }

    // Finally write the characteristic value to the known handle
    current_phase = ASYNC_PHASE_WRITE_VALUE;
    {
        size_t reportSize = 7;
        uint8_t* reportMsg = GATT_bm_alloc(connHandleCached, ATT_WRITE_REQ, reportSize, NULL);
        reportMsg[0] = voc >> 24;
        reportMsg[1] = (voc >> 16) & 0xFF;
        reportMsg[2] = (voc >> 8) & 0xFF;
        reportMsg[3] = voc & 0xFF;
        reportMsg[4] = temp >> 8;
        reportMsg[5] = temp & 0xFF;
        reportMsg[6] = batteryLevel;

        // Create the characteristic discovery message and send the message over
        attWriteReq_t *writeReq = ICall_malloc(sizeof(attWriteReq_t));
        writeReq->cmd = 0;                   // Bluetooth Request, not a Command (we want an ack)
        writeReq->handle = attHandleCached;  // Pass handle to characteristic;
        writeReq->pValue = reportMsg;        // This must be allocated with GATT_bm_alloc
        writeReq->len = reportSize;
        writeReq->sig = 0;                   // Not a signed write (see bluetooth spec)

        // Send request
        // We ned special logic so we don't leak memory
        {
            bStatus_t status = (BLEAppUtil_invokeFunction(SendData_WriteCharacteristic, (char*)writeReq));
            if (status != SUCCESS) {
                GATT_bm_free((gattMsg_t*) writeReq, ATT_WRITE_REQ);
                enum async_task_phase last_phase = current_phase;
                current_phase = ASYNC_PHASE_IDLE;
                rc = (last_phase << 28) | (ErrorSrcInvoke << 24) | (status & 0xFFFFFF);
                goto disconnect;
            }
        }
    }
    QueueGetResult(REPORT_OPCODE_WRITE_DONE);

disconnect:
    if (connHandleCached != 0xFFFF) {
        // Disconnect once we're done
        current_phase = ASYNC_PHASE_DISCONNECT;
        CheckInvokeStatus(BLEAppUtil_invokeFunctionNoData(SendData_Disconnect));
        QueueGetResult(REPORT_OPCODE_DISCONNECT);
    }

    current_phase = ASYNC_PHASE_IDLE;

    return rc;
}

void* SendDataTask(void * arg) {
    while (true) {
        float vocValue;
        BaseType_t rc = xQueueReceive(readingEventQueue, &vocValue, portMAX_DELAY); \
        if (rc != pdPASS) {
            MenuModule_printf(APP_MENU_GENERAL_STATUS_LINE, 0, "Failed to Receive VOC!"
                                      MENU_MODULE_COLOR_BOLD MENU_MODULE_COLOR_RED "0x%08x" MENU_MODULE_COLOR_RESET,
                                      rc);
            return NULL;
        }

        int32_t vocFixedPoint = (int32_t) (vocValue * 10000.0);

        // Fetch Temperature
        int16_t temperature = Temperature_getTemperature();
        int16_t temperatureScaled;
        if (INT16_MAX / 10 < temperature) {
            temperatureScaled = INT16_MAX;
        }
        else if (INT16_MIN / 10 > temperature) {
            temperatureScaled = INT16_MIN;
        }
        else {
            temperatureScaled = temperature * 10;
        }

        // Fetch battery voltage
        uint16_t currentVoltageMv = BatteryMonitor_getVoltage();

        // Convert to approximate battery level for CR2032 battery cell
        uint8_t batteryLevel = 3;
        if (currentVoltageMv < 2.8) {
            batteryLevel = 2;
        }
        else if (currentVoltageMv < 2.6) {
            batteryLevel = 1;
        }
        else if (currentVoltageMv < 2.2) {
            batteryLevel = 0;
        }

        MenuModule_printf(APP_MENU_GENERAL_STATUS_LINE, 0,
                          "Transmitting (TVOC: %6.3f, Temperature: %d C; Voltage: %d mV; Batt Level: %d)...",
                          vocValue, temperature, currentVoltageMv, batteryLevel);
        GPIO_write(CONFIG_GPIO_LED_GREEN, CONFIG_GPIO_LED_ON);

        uint32_t result;
        do {
            result = SendDataUpdate((uint32_t) vocFixedPoint, (uint16_t)temperatureScaled, batteryLevel);

            MenuModule_printf(APP_MENU_GENERAL_STATUS_LINE, 0, "Sent Data: Result = "
                              MENU_MODULE_COLOR_BOLD MENU_MODULE_COLOR_RED "0x%08x" MENU_MODULE_COLOR_RESET,
                              result);
        } while (result != 0);
        GPIO_write(CONFIG_GPIO_LED_GREEN, CONFIG_GPIO_LED_OFF);
    }
}

void SendUpdateValue(float vocReading) {
    assert(xQueueSendToBack(readingEventQueue, &vocReading, portMAX_DELAY) == pdPASS);
}

void SendUpdateInit() {
    app_zmod4xxx_init();
    Temperature_init();
    BatteryMonitor_init();

    bStatus_t status = BLEAppUtil_registerEventHandler(&gattEventHandler);
    assert(status == SUCCESS);
    status = BLEAppUtil_registerEventHandler(&sendDataConnHandler);
    assert(status == SUCCESS);

    connHandleQueue = xQueueCreate(1, sizeof(async_task_report_t));
    readingEventQueue = xQueueCreate(1, sizeof(int32_t));
    pthread_create(&sendDataThread, NULL, SendDataTask, NULL);
}
