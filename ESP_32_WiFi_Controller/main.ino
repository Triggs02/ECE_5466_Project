/*
 * WebSocketServer.ino
 *
 *  Created on: 22.05.2015
 *  Modified by Cedric Le Denmat in April 2024
 */

#include <Arduino.h>

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>

#include <WebSocketsServer.h>


#include "uart_task.hpp"
#include <ArduinoJson.h>

WiFiMulti WiFiMulti;
WebSocketsServer webSocket = WebSocketsServer(81);

bool listenerValid = false;
uint8_t listener;

JsonDocument history;
char msgBuf[256];

JsonDocument ble_gatt_message;

#define USE_SERIAL Serial

void hexdump(const void *mem, uint32_t len, uint8_t cols = 16) {
	const uint8_t* src = (const uint8_t*) mem;
	USE_SERIAL.printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
	for(uint32_t i = 0; i < len; i++) {
		if(i % cols == 0) {
			USE_SERIAL.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
		}
		USE_SERIAL.printf("%02X ", *src);
		src++;
	}
	USE_SERIAL.printf("\n");
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

    switch(type) {
        case WStype_DISCONNECTED:
            USE_SERIAL.printf("[%u] Disconnected!\n", num);
            listenerValid = false;
            break;
        case WStype_CONNECTED:
            {
                listener = num;
                listenerValid = true;
                IPAddress ip = webSocket.remoteIP(num);
                USE_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

                // send message to client
                webSocket.sendTXT(listener, "Connected");
                // for (uint8_t i = 0; i < history.size(); i++) {
                //   serializeJson(history[i], msgBuf, sizeof(msgBuf));
                //   webSocket.sendTXT(listener, msgBuf);
                // }
                // history.clear()
            }
            break;
        case WStype_TEXT:
            USE_SERIAL.printf("[%u] get Text: %s\n", num, payload);

            // send message to client
            // webSocket.sendTXT(num, "message here");

            // send data to all connected clients
            // webSocket.broadcastTXT("message here");
            break;
        case WStype_BIN:
            USE_SERIAL.printf("[%u] get binary length: %u\n", num, length);
            hexdump(payload, length);

            // send message to client
            // webSocket.sendBIN(num, payload, length);
            break;
		case WStype_ERROR:			
		case WStype_FRAGMENT_TEXT_START:
		case WStype_FRAGMENT_BIN_START:
		case WStype_FRAGMENT:
		case WStype_FRAGMENT_FIN:
			break;
    }

}

void setup() {
    // USE_SERIAL.begin(921600);
    USE_SERIAL.begin(115200);
    comm_task_init();

    //Serial.setDebugOutput(true);
    USE_SERIAL.setDebugOutput(true);
    USE_SERIAL.println("hello world");

    USE_SERIAL.println();
    USE_SERIAL.println();
    USE_SERIAL.println();

    for(uint8_t t = 4; t > 0; t--) {
        USE_SERIAL.printf("[SETUP] BOOT WAIT %d...\n", t);
        USE_SERIAL.flush();
        delay(1000);
    }

    WiFiMulti.addAP("fridge-voc", "fortnite");

    while(WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
    }

    USE_SERIAL.println("USE THIS IP FOR THE APP");
    USE_SERIAL.println(WiFi.localIP());

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
}

void loop() {
    webSocket.loop();
    if (uxQueueMessagesWaiting(comm_msg_queue) > 0) {
      uint8_t rxBuf[COMM_MSG_SIZE];
      if (xQueueReceive(comm_msg_queue, rxBuf, 0) == pdPASS) {
        float voc = ((rxBuf[0] << 24) | (rxBuf[1] << 16) | (rxBuf[2] << 8) | rxBuf[3]) / 10000.0;
        float temp = ((rxBuf[4] << 8) | rxBuf[5]) / 10.0;
        int battLvl = rxBuf[6];
        int client = rxBuf[7];

        ble_gatt_message["voc"] = voc;
        ble_gatt_message["temp"] = temp;
        ble_gatt_message["battLvl"] = battLvl;
        ble_gatt_message["client"] = client;

        // serializeJson(ble_gatt_message, msgBuf, sizeof(msgBuf));


        snprintf(msgBuf, sizeof(msgBuf), "Data%.4f;%.1f;%d;%d", voc, temp, battLvl, client);
        USE_SERIAL.printf("Forwarding a new message from 0x%02X: (VOC: %.4f, Temp: %.1f, Batt: %d - %s)\n", client, voc, temp, battLvl, msgBuf);

        if (listenerValid) {

          webSocket.sendTXT(listener, msgBuf);
        }
        else {
          history.add(ble_gatt_message);
          USE_SERIAL.println("Message stored (no websocket connected)");
        }
      }
    }
}
