// Bluegiga BGLib Arduino interface library slave device stub sketch
// 2014-02-12 by Jeff Rowberg <jeff@rowberg.net>
// Updates should (hopefully) always be available at https://github.com/jrowberg/bglib

// Changelog:
//      2014-02-12 - Fixed compile problem from missing constants
//      2013-03-17 - Initial release

/* ============================================
   !!!!!!!!!!!!!!!!!
   !!! IMPORTANT !!!
   !!!!!!!!!!!!!!!!!

   THIS SCRIPT WILL NOT COMMUNICATE PROPERLY IF YOU DO NOT ENSURE ONE OF THE
   FOLLOWING IS TRUE:

   1. You enable the <wakeup_pin> functionality in your firmware

   2. You COMMENT OUT the two lines 128 and 129 below which depend on wake-up
      funcitonality to work properly (they will BLOCK otherwise):

          ble112.onBeforeTXCommand = onBeforeTXCommand;
          ble112.onTXCommandComplete = onTXCommandComplete;

/* ============================================
BGLib Arduino interface library code is placed under the MIT license
Copyright (c) 2014 Jeff Rowberg

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
===============================================
*/


/* COMMANDS
Single LED Control
[0xXX]	[0xXX] 	[0xXX] 	[0xXX]
LED CH	RED	GREEN	BLUE
0-127	0-255	0-255	0-255

Stop All LEDs
[0x80]
CMD
128

Disable All LEDs
[0x81]
CMD
129

All LED Control
[0x82]	[0xXX] 	[0xXX] 	[0xXX]
CMD	RED	GREEN	BLUE
130	0-255	0-255	0-255

Rainbow Cycle
[0x83]
CMD
131

Rainbow Chase Cycle
[0x84]
CMD
132

Brightness Control
[0x85]	[0xXX]
CMD	AMT
133	0-255

Timing Control
[0x86]	[0xXX]
CMD	MULTIP
134	0-255
*/

#include "BGLib.h"
#include <Adafruit_NeoPixel.h>
#include <TimerOne.h>
#include <EEPROM.h>

// uncomment the following line for debug serial output
#define DEBUG

// ================================================================
// BLE STATE TRACKING (UNIVERSAL TO JUST ABOUT ANY BLE PROJECT)
// ================================================================

// BLE state machine definitions
#define BLE_STATE_STANDBY           0
#define BLE_STATE_SCANNING          1
#define BLE_STATE_ADVERTISING       2
#define BLE_STATE_CONNECTING        3
#define BLE_STATE_CONNECTED_MASTER  4
#define BLE_STATE_CONNECTED_SLAVE   5

// BLE state/link status tracker
uint8_t ble_state = BLE_STATE_STANDBY;
uint8_t ble_encrypted = 0;  // 0 = not encrypted, otherwise = encrypted
uint8_t ble_bonding = 0xFF; // 0xFF = no bonding, otherwise = bonding handle

// ================================================================
// HARDWARE CONNECTIONS AND GATT STRUCTURE SETUP
// ================================================================

// NOTE: this assumes you are using one of the following firmwares:
//  - BGLib_U1A1P_38400_noflow
//  - BGLib_U1A1P_38400_noflow_wake16
//  - BGLib_U1A1P_38400_noflow_wake16_hwake15
// If not, then you may need to change the pin assignments and/or
// GATT handles to match your firmware.

#define LED_PIN         14  // Arduino Uno LED pin
#define BLE_WAKEUP_PIN  12   // BLE wake-up pin
#define BLE_RESET_PIN   15   // BLE reset pin (active-low)

#define GATT_HANDLE_C_RX_DATA   17  // 0x11, supports "write" operation
#define GATT_HANDLE_C_TX_DATA   20  // 0x14, supports "read" and "indicate" operations

// create BGLib object:
//  - use SoftwareSerial por for module comms
//  - use nothing for passthrough comms (0 = null pointer)
//  - enable packet mode on API protocol since flow control is unavailable
BGLib ble112((HardwareSerial *)&Serial1, 0, 1);

#define BGAPI_GET_RESPONSE(v, dType) dType *v = (dType *)ble112.getLastRXPayload()


#define LEDPIN 6
#define NUMBEROFLEDS 24

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUMBEROFLEDS, LEDPIN, NEO_GRB + NEO_KHZ800);

boolean RainbowFlag = false;
boolean RainbowCycleFlag = false;

uint16_t rainbowJ;
uint16_t rainbowCycleI, rainbowCycleJ;

int settingsPermFlag; // 64
int rainbowPermFlag; // 65
int rainbowCyclePermFlag; // 66
int solidPermColor; // 67

byte brightness = 0xff;
byte timingMultiplier = 0x01;
byte timerCount;

// ================================================================
// ARDUINO APPLICATION SETUP AND LOOP FUNCTIONS
// ================================================================

// initialization sequence
void setup() {
  
  RainbowFlag = true;
  // initialize status LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // initialize BLE reset pin (active-low)
  pinMode(BLE_RESET_PIN, OUTPUT);
  digitalWrite(BLE_RESET_PIN, HIGH);

  // initialize BLE wake-up pin to allow (not force) sleep mode (assumes active-high)
  pinMode(BLE_WAKEUP_PIN, OUTPUT);
  digitalWrite(BLE_WAKEUP_PIN, LOW);

  strip.begin();
  strip.show();

  // set up internal status handlers (these are technically optional)
  ble112.onBusy = onBusy;
  ble112.onIdle = onIdle;
  ble112.onTimeout = onTimeout;

  // ONLY enable these if you are using the <wakeup_pin> parameter in your firmware's hardware.xml file
  // BLE module must be woken up before sending any UART data
  //ble112.onBeforeTXCommand = onBeforeTXCommand;
  //ble112.onTXCommandComplete = onTXCommandComplete;

  // set up BGLib event handlers
  ble112.ble_evt_system_boot = my_ble_evt_system_boot;
  ble112.ble_evt_connection_status = my_ble_evt_connection_status;
  ble112.ble_evt_connection_disconnected = my_ble_evt_connection_disconnect;
  ble112.ble_evt_attributes_value = my_ble_evt_attributes_value;

  // open Arduino USB serial (and wait, if we're using Leonardo)
  // use 38400 since it works at 8MHz as well as 16MHz
  Serial.begin(57600);
  while (!Serial);

  // open BLE serial port
  Serial1.begin(38400);

  // reset module (maybe not necessary for your application)
  digitalWrite(BLE_RESET_PIN, LOW);
  delay(5); // wait 5ms
  digitalWrite(BLE_RESET_PIN, HIGH);

  /*
  settingsPermFlag = EEPROM.read(64);

  if (settingsPermFlag == 1) {
    rainbowPermFlag = EEPROM.read(65);
    rainbowCyclePermFlag = EEPROM.read(66);
    solidPermColor = EEPROM.read(67);
  }
  */

  Timer1.initialize(); // set a timer of length 20000 microseconds (or 0.02 sec - or 50Hz => the led will blink 5 times, 5 cycles of on-and-off, per second)
  Timer1.attachInterrupt( timerIsr , 10000);
}

void timerIsr()
{

  timerCount++;
  if (timerCount > timingMultiplier) {
    timerCount = 0;

    if (RainbowFlag == true) {
      //rainbow(20);

      if (rainbowJ < 256) {

        uint16_t i;

        for (i = 0; i < strip.numPixels(); i++) {
          strip.setPixelColor(i, Wheel((i + rainbowJ) & 255));
        }
        strip.setBrightness(brightness);
        strip.show();

        rainbowJ++;
        if (rainbowJ >= 256) {
          rainbowJ = 0;
        }
      }
    } else if (RainbowCycleFlag == true) {

      uint16_t i;

      if (rainbowCycleJ < 256 * 5) {

        for (i = 0; i < strip.numPixels(); i++) {
          strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + rainbowCycleJ) & 255));
        }
        strip.setBrightness(brightness);
        strip.show();

        rainbowCycleJ++;
        if (rainbowCycleJ >= 256 * 5) {
          rainbowCycleJ = 0;
        }
      }


    }
  }

}


// main application loop
void loop() {
  // keep polling for new data from BLE
  ble112.checkActivity();

  // blink Arduino LED based on state:
  //  - solid = STANDBY
  //  - 1 pulse per second = ADVERTISING
  //  - 2 pulses per second = CONNECTED_SLAVE
  //  - 3 pulses per second = CONNECTED_SLAVE with encryption
  uint16_t slice = millis() % 1000;
  if (ble_state == BLE_STATE_STANDBY) {
    digitalWrite(LED_PIN, HIGH);
  } else if (ble_state == BLE_STATE_ADVERTISING) {
    digitalWrite(LED_PIN, slice < 100);
  } else if (ble_state == BLE_STATE_CONNECTED_SLAVE) {
    digitalWrite(LED_PIN, LOW);
    /*
    if (!ble_encrypted) {
      digitalWrite(LED_PIN, slice < 100 || (slice > 200 && slice < 300));
    } else {
      digitalWrite(LED_PIN, slice < 100 || (slice > 200 && slice < 300) || (slice > 400 && slice < 500));
    }
    */
  }




}



// ================================================================
// INTERNAL BGLIB CLASS CALLBACK FUNCTIONS
// ================================================================

// called when the module begins sending a command
void onBusy() {
  // turn LED on when we're busy
  //digitalWrite(LED_PIN, HIGH);
}

// called when the module receives a complete response or "system_boot" event
void onIdle() {
  // turn LED off when we're no longer busy
  //digitalWrite(LED_PIN, LOW);
}

// called when the parser does not read the expected response in the specified time limit
void onTimeout() {
  // reset module (might be a bit drastic for a timeout condition though)
  digitalWrite(BLE_RESET_PIN, LOW);
  delay(5); // wait 5ms
  digitalWrite(BLE_RESET_PIN, HIGH);
}

// called immediately before beginning UART TX of a command
void onBeforeTXCommand() {
  // wake module up (assuming here that digital pin 5 is connected to the BLE wake-up pin)
  digitalWrite(BLE_WAKEUP_PIN, HIGH);

  // wait for "hardware_io_port_status" event to come through, and parse it (and otherwise ignore it)
  uint8_t *last;
  while (1) {
    ble112.checkActivity();
    last = ble112.getLastEvent();
    if (last[0] == 0x07 && last[1] == 0x00) break;
  }

  // give a bit of a gap between parsing the wake-up event and allowing the command to go out
  delayMicroseconds(1000);
}

// called immediately after finishing UART TX
void onTXCommandComplete() {
  // allow module to return to sleep (assuming here that digital pin 5 is connected to the BLE wake-up pin)
  digitalWrite(BLE_WAKEUP_PIN, LOW);
}



// ================================================================
// APPLICATION EVENT HANDLER FUNCTIONS
// ================================================================

void my_ble_evt_system_boot(const ble_msg_system_boot_evt_t *msg) {
#ifdef DEBUG
  Serial.print("###\tsystem_boot: { ");
  Serial.print("major: "); Serial.print(msg -> major, HEX);
  Serial.print(", minor: "); Serial.print(msg -> minor, HEX);
  Serial.print(", patch: "); Serial.print(msg -> patch, HEX);
  Serial.print(", build: "); Serial.print(msg -> build, HEX);
  Serial.print(", ll_version: "); Serial.print(msg -> ll_version, HEX);
  Serial.print(", protocol_version: "); Serial.print(msg -> protocol_version, HEX);
  Serial.print(", hw: "); Serial.print(msg -> hw, HEX);
  Serial.println(" }");
#endif

  // system boot means module is in standby state
  //ble_state = BLE_STATE_STANDBY;
  // ^^^ skip above since we're going right back into advertising below

  // set advertisement interval to 200-300ms, use all advertisement channels
  // (note min/max parameters are in units of 625 uSec)
  ble112.ble_cmd_gap_set_adv_parameters(320, 480, 7);
  while (ble112.checkActivity(1000));

  // USE THE FOLLOWING TO LET THE BLE STACK HANDLE YOUR ADVERTISEMENT PACKETS
  // ========================================================================
  // start advertising general discoverable / undirected connectable
  //ble112.ble_cmd_gap_set_mode(BGLIB_GAP_GENERAL_DISCOVERABLE, BGLIB_GAP_UNDIRECTED_CONNECTABLE);
  //while (ble112.checkActivity(1000));

  // USE THE FOLLOWING TO HANDLE YOUR OWN CUSTOM ADVERTISEMENT PACKETS
  // =================================================================

  // build custom advertisement data
  // default BLE stack value: 0201061107e4ba94c3c9b7cdb09b487a438ae55a19
  uint8 adv_data[] = {
    0x02, // field length
    BGLIB_GAP_AD_TYPE_FLAGS, // field type (0x01)
    0x06, // data (0x02 | 0x04 = 0x06, general discoverable + BLE only, no BR+EDR)
    0x11, // field length
    BGLIB_GAP_AD_TYPE_SERVICES_128BIT_ALL, // field type (0x07)
    0xe4, 0xba, 0x94, 0xc3, 0xc9, 0xb7, 0xcd, 0xb0, 0x9b, 0x48, 0x7a, 0x43, 0x8a, 0xe5, 0x5a, 0x19
  };

  // set custom advertisement data
  ble112.ble_cmd_gap_set_adv_data(0, 0x15, adv_data);
  while (ble112.checkActivity(1000));

  // build custom scan response data (i.e. the Device Name value)
  // default BLE stack value: 140942474c69622055314131502033382e344e4657
  uint8 sr_data[] = {
    0x07, // field length
    BGLIB_GAP_AD_TYPE_LOCALNAME_COMPLETE, // field type
    'B', 'L', 'E', '1', '0', '0', '0'
  };

  // get BLE MAC address
  ble112.ble_cmd_system_address_get();
  while (ble112.checkActivity(1000));
  BGAPI_GET_RESPONSE(r0, ble_msg_system_address_get_rsp_t);

  // assign last three bytes of MAC address to ad packet friendly name (instead of 00:00:00 above)
  sr_data[13] = (r0 -> address.addr[2] / 0x10) + 48 + ((r0 -> address.addr[2] / 0x10) / 10 * 7); // MAC byte 4 10's digit
  sr_data[14] = (r0 -> address.addr[2] & 0xF)  + 48 + ((r0 -> address.addr[2] & 0xF ) / 10 * 7); // MAC byte 4 1's digit
  sr_data[16] = (r0 -> address.addr[1] / 0x10) + 48 + ((r0 -> address.addr[1] / 0x10) / 10 * 7); // MAC byte 5 10's digit
  sr_data[17] = (r0 -> address.addr[1] & 0xF)  + 48 + ((r0 -> address.addr[1] & 0xF ) / 10 * 7); // MAC byte 5 1's digit
  sr_data[19] = (r0 -> address.addr[0] / 0x10) + 48 + ((r0 -> address.addr[0] / 0x10) / 10 * 7); // MAC byte 6 10's digit
  sr_data[20] = (r0 -> address.addr[0] & 0xF)  + 48 + ((r0 -> address.addr[0] & 0xF ) / 10 * 7); // MAC byte 6 1's digit

  // set custom scan response data (i.e. the Device Name value)
  ble112.ble_cmd_gap_set_adv_data(1, 0x15, sr_data);
  while (ble112.checkActivity(1000));

  // put module into discoverable/connectable mode (with user-defined advertisement data)
  ble112.ble_cmd_gap_set_mode(BGLIB_GAP_USER_DATA, BGLIB_GAP_UNDIRECTED_CONNECTABLE);
  while (ble112.checkActivity(1000));

  // set state to ADVERTISING
  ble_state = BLE_STATE_ADVERTISING;
}

void my_ble_evt_connection_status(const ble_msg_connection_status_evt_t *msg) {
#ifdef DEBUG
  Serial.print("###\tconnection_status: { ");
  Serial.print("connection: "); Serial.print(msg -> connection, HEX);
  Serial.print(", flags: "); Serial.print(msg -> flags, HEX);
  Serial.print(", address: ");
  // this is a "bd_addr" data type, which is a 6-byte uint8_t array
  for (uint8_t i = 0; i < 6; i++) {
    if (msg -> address.addr[i] < 16) Serial.write('0');
    Serial.print(msg -> address.addr[i], HEX);
  }
  Serial.print(", address_type: "); Serial.print(msg -> address_type, HEX);
  Serial.print(", conn_interval: "); Serial.print(msg -> conn_interval, HEX);
  Serial.print(", timeout: "); Serial.print(msg -> timeout, HEX);
  Serial.print(", latency: "); Serial.print(msg -> latency, HEX);
  Serial.print(", bonding: "); Serial.print(msg -> bonding, HEX);
  Serial.println(" }");
#endif

  // "flags" bit description:
  //  - bit 0: connection_connected
  //           Indicates the connection exists to a remote device.
  //  - bit 1: connection_encrypted
  //           Indicates the connection is encrypted.
  //  - bit 2: connection_completed
  //           Indicates that a new connection has been created.
  //  - bit 3; connection_parameters_change
  //           Indicates that connection parameters have changed, and is set
  //           when parameters change due to a link layer operation.

  // check for new connection established
  if ((msg -> flags & 0x05) == 0x05) {
    // track state change based on last known state, since we can connect two ways
    if (ble_state == BLE_STATE_ADVERTISING) {
      ble_state = BLE_STATE_CONNECTED_SLAVE;
    } else {
      ble_state = BLE_STATE_CONNECTED_MASTER;
    }
  }

  // update "encrypted" status
  ble_encrypted = msg -> flags & 0x02;

  // update "bonded" status
  ble_bonding = msg -> bonding;
}

void my_ble_evt_connection_disconnect(const struct ble_msg_connection_disconnected_evt_t *msg) {
#ifdef DEBUG
  Serial.print("###\tconnection_disconnect: { ");
  Serial.print("connection: "); Serial.print(msg -> connection, HEX);
  Serial.print(", reason: "); Serial.print(msg -> reason, HEX);
  Serial.println(" }");
#endif

  // set state to DISCONNECTED
  //ble_state = BLE_STATE_DISCONNECTED;
  // ^^^ skip above since we're going right back into advertising below

  // after disconnection, resume advertising as discoverable/connectable
  //ble112.ble_cmd_gap_set_mode(BGLIB_GAP_GENERAL_DISCOVERABLE, BGLIB_GAP_UNDIRECTED_CONNECTABLE);
  //while (ble112.checkActivity(1000));

  // after disconnection, resume advertising as discoverable/connectable (with user-defined advertisement data)
  ble112.ble_cmd_gap_set_mode(BGLIB_GAP_USER_DATA, BGLIB_GAP_UNDIRECTED_CONNECTABLE);
  while (ble112.checkActivity(1000));

  // set state to ADVERTISING
  ble_state = BLE_STATE_ADVERTISING;

  // clear "encrypted" and "bonding" info
  ble_encrypted = 0;
  ble_bonding = 0xFF;
}

void my_ble_evt_attributes_value(const struct ble_msg_attributes_value_evt_t *msg) {
#ifdef DEBUG
  Serial.print("###\tattributes_value: { ");
  Serial.print("connection: "); Serial.print(msg -> connection, HEX);
  Serial.print(", reason: "); Serial.print(msg -> reason, HEX);
  Serial.print(", handle: "); Serial.print(msg -> handle, HEX);
  Serial.print(", offset: "); Serial.print(msg -> offset, HEX);
  Serial.print(", value_len: "); Serial.print(msg -> value.len, HEX);
  Serial.print(", value_data: ");
  // this is a "uint8array" data type, which is a length byte and a uint8_t* pointer
  for (uint8_t i = 0; i < msg -> value.len; i++) {
    if (msg -> value.data[i] < 16) Serial.write('0');
    Serial.print(msg -> value.data[i], HEX);
  }
  Serial.println(" }");
#endif

  byte command = msg -> value.data[0];

  if (command < 128) {

    clearPresetFlags();

    byte p = msg -> value.data[0];
    byte r = msg -> value.data[1];
    byte g = msg -> value.data[2];
    byte b = msg -> value.data[3];

    strip.setPixelColor(p, strip.Color(r, g, b));
  } else {

    if (command == 128) { // 0x80
      clearPresetFlags();
      strip.setBrightness(brightness);
      strip.show();
    } if (command == 129) { // 0x81
      clearPresetFlags();
      uint32_t color = strip.Color(0, 0, 0);

      for (int i = 0; i < NUMBEROFLEDS; i++) {
        strip.setPixelColor(i, color);
      }
      strip.setBrightness(brightness);
      strip.show();
    } else if (command == 130) { // 0x82 Solid Color
      clearPresetFlags();

      byte r = msg -> value.data[1];
      byte g = msg -> value.data[2];
      byte b = msg -> value.data[3];

      byte ro = map(r, 0, 255, 0, brightness);
      byte go = map(g, 0, 255, 0, brightness);
      byte bo = map(b, 0, 255, 0, brightness);

      uint32_t color = strip.Color((byte) ro, (byte) go, (byte) bo);

      for (int i = 0; i < NUMBEROFLEDS; i++) {
        strip.setPixelColor(i, color);
      }
      strip.setBrightness(brightness);
      strip.show();
    } else if (command == 131) { // 0x83 Rainbow Cycle
      clearPresetFlags();
      RainbowFlag = true;
    } else if (command == 132) { // 0x84 Rainbow Cycle
      clearPresetFlags();
      RainbowCycleFlag = true;
    } else if (command == 133) { // 0x85 Brightness control
      brightness = msg -> value.data[1];
    } else if (command == 134) { // 0x86 Timing control
      timingMultiplier = msg -> value.data[1];
    }
  }

  /*

  */



  /*
  // check for data written to "c_rx_data" handle
  if (msg -> handle == GATT_HANDLE_C_RX_DATA && msg -> value.len > 0) {
      // set ping 8, 9, and 10 to three lower-most bits of first byte of RX data
      // (nice for controlling RGB LED or something)
      digitalWrite(8, msg -> value.data[0] & 0x01);
      digitalWrite(9, msg -> value.data[0] & 0x02);
      digitalWrite(10, msg -> value.data[0] & 0x04);
  }
  */
}

void ble_command_parser(uint8array value) {

}

void clearPresetFlags() {
  RainbowFlag = false;
  RainbowCycleFlag = false;
}



// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {

  if (WheelPos < 85) {
    return strip.Color((byte)(WheelPos * 3) , (byte)(255 - WheelPos * 3) , 0);
  } else if (WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color((byte)(255 - WheelPos * 3) , 0, (byte)(WheelPos * 3) );
  } else {
    WheelPos -= 170;
    return strip.Color(0, (byte)(WheelPos * 3) , (byte)(255 - WheelPos * 3) );
  }
}
