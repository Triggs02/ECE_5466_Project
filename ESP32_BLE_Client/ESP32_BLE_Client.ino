#include "BLEDevice.h"
#include <Wire.h>
#include "ccs811.h"

// === Wiring ===
// CCS811 -> ESP32
// VDD    ->  3V3
// GND    ->  GND
// SDA    ->  D21
// SCL    ->  D22
// Wake   ->  D23

CCS811 ccs811(23);

// The remote service we wish to connect to.
static BLEUUID serviceUUID("1974e0a6-a490-4869-84b7-5f03cf47ac9d");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("1974e0a7-a490-4869-84b7-5f03cf47ac9d");

BLEClient*  pClient;
static BLERemoteCharacteristic* pRemoteCharacteristic;
// static BLEAddress targetAddress("94:B9:7E:D5:62:56");
static BLEAddress targetAddress("D4:8A:FC:A8:91:3E");

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
    if (!pClient->connect(targetAddress)) {
      return false;
    }
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

void reportReading(float vocReadingPpm, float temperature, uint8_t battLevel) {

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

    int32_t vocRounded = (int32_t)(vocReadingPpm * 10000);
    uint32_t vocData = (uint32_t) vocRounded;

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

  // Enable I2C
  Wire.begin(); 
  
  // Enable CCS811
  ccs811.set_i2cdelay(50); // Needed for ESP8266 because it doesn't handle I2C clock stretch correctly
  bool ok= ccs811.begin();
  if( !ok ) Serial.println("setup: CCS811 begin FAILED");

  // Print CCS811 versions
  Serial.print("setup: hardware    version: "); Serial.println(ccs811.hardware_version(),HEX);
  Serial.print("setup: bootloader  version: "); Serial.println(ccs811.bootloader_version(),HEX);
  Serial.print("setup: application version: "); Serial.println(ccs811.application_version(),HEX);
  
  // Start measuring
  ok= ccs811.start(CCS811_MODE_1SEC);
  if( !ok ) Serial.println("setup: CCS811 start FAILED"); 
}

void loop() {
  // Read
  uint16_t eco2, etvoc, errstat, raw;
  ccs811.read(&eco2,&etvoc,&errstat,&raw); 
  
  // Print measurement results based on status
  if( errstat==CCS811_ERRSTAT_OK ) { 
    Serial.print("CCS811: ");
    Serial.print("eco2=");  Serial.print(eco2);     Serial.print(" ppm  ");
    Serial.print("etvoc="); Serial.print(etvoc);    Serial.print(" ppb  ");
    Serial.print("raw6=");  Serial.print(raw/1024); Serial.print(" uA  "); 
    Serial.print("raw10="); Serial.print(raw%1024); Serial.print(" ADC  ");
    Serial.print("R="); Serial.print((1650*1000L/1023)*(raw%1024)/(raw/1024)); Serial.print(" ohm");
    Serial.println();
    // Conversion from ppm to mg/m^3 src: http://niosh.dnacih.com/nioshdbs/calc.htm
    reportReading(etvoc / (1000.0 * 24.45), 0, 3);
  } else if( errstat==CCS811_ERRSTAT_OK_NODATA ) {
    Serial.println("CCS811: waiting for (new) data");
  } else if( errstat & CCS811_ERRSTAT_I2CFAIL ) { 
    Serial.println("CCS811: I2C error");
  } else {
    Serial.print("CCS811: errstat="); Serial.print(errstat,HEX); 
    Serial.print("="); Serial.println( ccs811.errstat_str(errstat) ); 
  }

  delay(3000);
}
