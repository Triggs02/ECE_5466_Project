# ECE 5466 Team 1: Fridge VOC Monitoring System

## Software Setup Guide

Guide on how to load the various projects in for the assorted microcontrollers.


### CC2340R5 Firmware

Required Software: TI Code Composer Studio

 * Create new project based on `basic_ble` example for the CC2340R5, using FreeRTOS and the TI Clang compiler
 * Overwrite the .syscfg file in the root directory with the modified syscfg provided
 * Delete all files in the `app/` directory
 * Copy in all other files (other than the syscfg) into the app directory
 * Ensure that Code Composer Studio has recognized the files added to the project
 * Go to the [Renesas ZMOD4410 Software Downloads](https://www.renesas.com/us/en/products/sensor-products/environmental-sensors/metal-oxide-gas-sensors/zmod4410-firmware-configurable-indoor-air-quality-iaq-sensor-embedded-artificial-intelligence-ai#design_development) and request access for **ZMOD4410 – IAQ and TVOC Firmware – 2nd generation algorithms (IAQ 2nd Gen)**
    * Note that this step is required as the license agreement forbids distribution of these files, as they are proprietary
    * This process takes time as it requires a Renesas employee to approve the request (For me I had to wait around three days for access to be approved)
 * After you download the zip, navigate to the `gas-algorithm-libraries/iaq_2nd_gen/Arm Cortex-M/M0plus/armclang` directory. Extract these files
 * Also navigate to the `zmod4xxx_evk_example/src` directory. Extract all files except the main.c
 * Add these files to the project, ensuring that the header files are in a folder that is included by the compiler (other than that it does not matter where they go).
 * Modify the CCS project settings to add the .lib files to the linker. Note that the exact steps to do this depend on the CCS version. Refer to the builtin help documents installed by your CCS version for exact steps.
 * Now, the project should be able to compile
 * The easiest way to flash the project is to press the `Debug` (bug icon) toolbar button with the TI CCS2340R5 launchpad connected to the computer using the XDS110 debug adapter board

### ESP32 BLE Client

Required Software: Arduino IDE with ESP32 Board Support Installed, and the [CCS811 Arduino Library](https://github.com/maarten-pennings/CCS811) installed.

* Open the ESP32_BLE_Client Project in Arduino
* Configure the project to use the ESP32-S3 board (required to use internal temperature sensor)
* Press the Upload button with the ESP32-S3 connected

### ESP32 Bluetooth Server

* Required Software: ESP IDF installed on target development system
                      - A great tool to use is VisualStudio Code's ESP-IDF extension (the instructions below assume you have this installed)
* Required Files: All files contained within the ESP-32-Server on the main branch
* Required Hardware: A development board with ESP32/ESP32-C3/ESP32-C2/ESP32-H2/ESP-S3 SoC
                   and a USB cable to provide power to the device and allow for the programming of the board.

Steps to Properly Build Example:
* Ensure ESP-IDF and its corresponding build tools are installed in Visual Studio Code
      - A helpful tutorial on how to do this properly can be found here: https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/tutorial/install.md
* Place all files contained within the ESP-32-Server folder into the directory that development will be taking place.
* Ensure that the correct chip target has been selected bby issuing the following command (Where chip name refers to one of the previously mentioned chips in the "Required Hardware" section):

```bash
idf.py set-target <chip_name>
```

* Finally, once the development environment and chip have been selected, issue the following command to initiate the building and flashing procedures (make sure to hold down the 'Boot' button on the dev-board device after the build process is done and until the flash process appears to start):

```bash
idf.py flash
```

### ESP32 Websocket Server

Required Software: Arduino IDE with ESP32 Board Support Installed

* TODO: Cedric, make sure that if there's any other libraries required list them in the required software
* Open the ESP32 Websocket Server project in arduino
* TODO: Cedric explain if any settings need to be changed on Arduino IDE
* TODO: Mention changing the wifi credentials?
* Press the Upload button with the ESP32 connected to the computer

### Android App

Required Software: Android Studio

* Create a new Blank Views example project
* TODO: Cedric explain what files to copy in
* Press the deploy button with your Android phone plugged into the computer

## Hardware Wiring Guide

Once all of the microcontrollers have been programmed, you will need to wire them together correctly before the project will work. Please do these steps with the devices powered off.

### TI CC2340R5 Wiring

Required Hardware: [TI CC2340R5 Launchpad](https://www.ti.com/tool/LP-EM-CC2340R5), [TI XDS110 LaunchPad Debugger](https://www.ti.com/tool/LP-XDS110), [ZMOD4410-EVK-DB](https://www.digikey.com/en/products/detail/renesas-electronics-corporation/ZMOD4410-EVK-DB/14010939)

* Plug the XDS110 into the debug port of the CCS2340R5
* Connect the VCC pin of the ZMOD4410-EVK into the 3V3 pin of the LaunchPad
* Connect the GND pin of the ZMOD4410-EVK into the GND pin of the LaunchPad
* Connect the SDA pin of the ZMOD4410-EVK into the DIO0 pin of the LaunchPad
* Connect the SCL pin of the ZMOD4410-EVK into the DIO24 pin of the LaunchPad
* Plug the XDS110 USB cable into power

### ESP32 Client Wiring

Required Hardware: ESP32-S3, Inland CCS811 Air Quality Module

* Connect the VDD pin of the CCS811 to the 3V3 pin of the ESP32-S3
* Connect the GND pin of the CCS811 to the GND pin of the ESP32-S3
* Connect the SDA pin of the CCS811 to the D6 pin of the ESP32-S3
* Connect the SCL pin of the CCS811 to the D7 pin of the ESP32-S3
* Connect the Wake pin of the CCS811 to the D5 pin of the ESP32-S3
* Ensure the ESP32 is powered

### ESP32 Bluetooth/Websocket Server Wiring

Required Hardware: 2x ESP32

* Connect pin 4 of the Bluetooth ESP32 to Pin 5 of the Websocket ESP32
* Connect the ground pins together between the ESP32s
* Ensure both ESP32s are powered

### Android App Setup

* Determine the IP address of the ESP32 Websocket (This can often be done via your router's webpage, listing the attached devices)
* Enter the IP address for the ESP32 into the App
* TODO: Cedric maybe write the format for the connection, am I missing any other steps?
