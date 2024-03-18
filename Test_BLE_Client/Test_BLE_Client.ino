#include "BLEDevice.h"

// The remote service we wish to connect to.
static BLEUUID serviceUUID("1974e0a6-a490-4869-84b7-5f03cf47ac9d");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("1974e0a7-a490-4869-84b7-5f03cf47ac9d");

BLEClient*  pClient;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAddress targetAddress("94:B9:7E:D5:62:56");  // TODO: Update to real client MAC address

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    Serial.println("onDisconnect");
  }
};

bool connectToServer() {
    Serial.print("Forming a connection to ");
    // Serial.println(myDevice->getAddress().toString().c_str());
    Serial.println(targetAddress.toString().c_str());
    
    pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    // pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    pClient->connect(targetAddress);
    Serial.println(" - Connected to server");
    pClient->setMTU(517); //set client to request maximum MTU from server (default is 23 otherwise)
  
    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service");


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our characteristic");

    return true;
}

void reportReading(uint32_t vocData, float temperature, uint8_t battLevel) {

  if (connectToServer()) {
    int16_t temperatureInt;
    if (temperature < (INT16_MIN / 10)) {
      temperatureInt = INT16_MIN;
    }
    else if (temperature > (INT16_MAX / 10)) {
      temperatureInt = INT16_MAX;
    }
    else {
      temperatureInt = temperature * 10;
    }

    uint8_t dataMsg[] = {vocData >> 24, (vocData >> 16) & 0xFF, (vocData >> 8) & 0xFF, vocData & 0xFF,
                        ((uint16_t)temperatureInt) >> 8, ((uint16_t)temperatureInt) & 0xFF, battLevel};
    pRemoteCharacteristic->writeValue(dataMsg, sizeof(dataMsg), true);
    Serial.println("Wrote characteristic");
    pClient->disconnect();
  }
  else {
    Serial.println("Failed to connect");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");

  delay(3000);

  reportReading(0xDEADBEEF, 27.3, 2);   
}

void loop() {}
