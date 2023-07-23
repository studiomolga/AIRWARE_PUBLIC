/*******************************************************************************
   Copyright (c) 2015 Thomas Telkamp and Matthijs Kooijman
   Copyright (c) 2018 Terry Moore, MCCI

   Permission is hereby granted, free of charge, to anyone
   obtaining a copy of this document and accompanying files,
   to do whatever they want with them without any restriction,
   including, but not limited to, copying, modification and redistribution.
   NO WARRANTY OF ANY KIND IS PROVIDED.

   This example sends a valid LoRaWAN packet with payload "Hello,
   world!", using frequency and encryption settings matching those of
   the The Things Network. It's pre-configured for the Adafruit
   Feather M0 LoRa.

   This uses OTAA (Over-the-air activation), where where a DevEUI and
   application key is configured, which are used in an over-the-air
   activation procedure where a DevAddr and session keys are
   assigned/generated for use with all further communication.

   Note: LoRaWAN per sub-band duty-cycle limitation is enforced (1% in
   g1, 0.1% in g2), but not the TTN fair usage policy (which is probably
   violated by this sketch when left running for longer)!

   To use this sketch, first register your application and device with
   the things network, to set or generate an AppEUI, DevEUI and AppKey.
   Multiple devices can use the same AppEUI, but each device has its own
   DevEUI and AppKey.

   Do not forget to define the radio type correctly in
   arduino-lmic/project_config/lmic_project_config.h or from your BOARDS.txt.

 *******************************************************************************/

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Adafruit_SleepyDog.h>
#include <Arduino.h>
#include "RTClib.h"
#include <Servo.h>
#include "statemachine.h"

#define LED_NOW_A 5
#define LED_NOW_B 9
#define LED_NOW_C 10
#define LED_NOW_D 12
#define LED_NOW_E 13
#define LED_NOW_AMT 13

#define LED_FUTURE_A 14
#define LED_FUTURE_B 15
#define LED_FUTURE_C 16
#define LED_FUTURE_AMT 4

#define SERVO_PIN 11
#define SERVO_ADJUST_PERIOD 20

#define PIN_CONFIG 0
#define PIN_STATE 1
#define VBATPIN A4
#define BUFFER_SIZE 1
#define MAX_TRIES 3
#define NUM_PARTS 4

// This EUI must be in little-endian format, so least-significant-byte
// first. When copying an EUI from ttnctl output, this means to reverse
// the bytes. For TTN issued EUIs the last bytes should be 0xD5, 0xB3,
// 0x70.

static const u1_t PROGMEM APPEUI[8] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getArtEui (u1_t* buf) {
  memcpy_P(buf, APPEUI, 8);
}

// This should also be in little endian format, see above.
static const u1_t PROGMEM DEVEUI[8] = { 0xD1, 0x4B, 0x05, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 };
void os_getDevEui (u1_t* buf) {
  memcpy_P(buf, DEVEUI, 8);
}

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from the TTN console can be copied as-is.
static const u1_t PROGMEM APPKEY[16] = { 0xFE, 0xA7, 0xB1, 0xA7, 0x50, 0x96, 0x15, 0x0E, 0xB9, 0xCB, 0x0F, 0x0E, 0x54, 0x47, 0xBC, 0xC1 };
void os_getDevKey (u1_t* buf) {
  memcpy_P(buf, APPKEY, 16);
}

// config and state matrix for the nowcast leds
uint8_t nowcastLEDMatrix[LED_NOW_AMT][2][5] = {
  //           PIN_CONFIG                  PIN_STATE
  //    A       B       C      D      E         A     B    C    D    E
  { { OUTPUT, OUTPUT, INPUT, INPUT, INPUT }, { HIGH, LOW, LOW, LOW, LOW } },  //  1
  { { OUTPUT, OUTPUT, INPUT, INPUT, INPUT }, { LOW, HIGH, LOW, LOW, LOW  } }, //  2
  { { INPUT, OUTPUT, OUTPUT, INPUT, INPUT }, { LOW, HIGH, LOW, LOW, LOW  } }, //  3
  { { INPUT, OUTPUT, OUTPUT, INPUT, INPUT }, { LOW, LOW, HIGH, LOW, LOW  } }, //  4
  { { INPUT, INPUT, OUTPUT, OUTPUT, INPUT }, { LOW, LOW, HIGH, LOW, LOW  } }, //  5
  { { INPUT, INPUT, OUTPUT, OUTPUT, INPUT }, { LOW, LOW, LOW, HIGH, LOW  } }, //  6
  { { INPUT, INPUT, INPUT, OUTPUT, OUTPUT }, { LOW, LOW, LOW, HIGH, LOW  } }, //  7
  { { INPUT, INPUT, INPUT, OUTPUT, OUTPUT }, { LOW, LOW, LOW, LOW, HIGH  } }, //  8
  { { OUTPUT, INPUT, OUTPUT, INPUT, INPUT }, { HIGH, LOW, LOW, LOW, LOW  } }, //  9
  { { OUTPUT, INPUT, OUTPUT, INPUT, INPUT }, { LOW, LOW, HIGH, LOW, LOW  } }, // 10
  { { INPUT, INPUT, OUTPUT, INPUT, OUTPUT }, { LOW, LOW, HIGH, LOW, LOW  } }, // 11
  { { INPUT, INPUT, OUTPUT, INPUT, OUTPUT }, { LOW, LOW, LOW, LOW, HIGH  } }, // 12
  { { INPUT, OUTPUT, INPUT, OUTPUT, INPUT }, { LOW, HIGH, LOW, LOW, LOW  } }  // 13
};

//uint8_t futurecastLEDMatrix[LED_FUTURE_AMT][2][3] = {
//  //           PIN_CONFIG                  PIN_STATE
//  //    A       B       C         A     B    C  
//  { { OUTPUT, OUTPUT, INPUT }, { HIGH, LOW, LOW } }, //  1 
//  { { OUTPUT, OUTPUT, INPUT }, { LOW, HIGH, LOW } }, //  2
//  { { INPUT, OUTPUT, OUTPUT }, { LOW, HIGH, LOW } }, //  3
//  { { INPUT, OUTPUT, OUTPUT }, { LOW, LOW, HIGH } }  //  4
//};

uint8_t futurecastLEDMatrix[LED_FUTURE_AMT][2][3] = {
  //           PIN_CONFIG                  PIN_STATE
  //    A       B       C         A     B    C  
  { { INPUT, OUTPUT, OUTPUT }, { LOW, LOW, HIGH } }, //  1
  { { INPUT, OUTPUT, OUTPUT }, { LOW, HIGH, LOW } }, //  2
  { { OUTPUT, OUTPUT, INPUT }, { LOW, HIGH, LOW } }, //  3
  { { OUTPUT, OUTPUT, INPUT }, { HIGH, LOW, LOW } }  //  4
};


static uint8_t data[] = { 0 };    //we just send an ID every minute, this ID is registered in a database with all the things needed for making the api call and sending data back
static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const uint32_t TX_INTERVAL_SHORT = 200;
const uint32_t TX_INTERVAL_LONG = 3600;
bool isSleep = false;
bool hasSend = false;
bool hasReceived = false;
bool doServoAdjust = false;
uint16_t loops = 0;
byte buf[BUFFER_SIZE];

uint8_t tries = 0;
uint8_t nowcast = 0;
uint8_t futurecast = 0;
uint8_t currNowcast = 0;
uint8_t currFuturecast = 0;
uint8_t servoPos = 0;
uint8_t currServoPos = 0;
uint32_t lastServoEvent = 0;
uint32_t txInterval = TX_INTERVAL_SHORT;
DateTime nextSend;

RTC_DS3231 rtc;

Servo servo;

StateMachine stateMachine;

// Pin mapping
//
// Adafruit BSPs are not consistent -- m0 express defs ARDUINO_SAMD_FEATHER_M0,
// m0 defs ADAFRUIT_FEATHER_M0
//
#if defined(ARDUINO_SAMD_FEATHER_M0) || defined(ADAFRUIT_FEATHER_M0)
// Pin mapping for Adafruit Feather M0 LoRa, etc.
const lmic_pinmap lmic_pins = {
  .nss = 8,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 4,
  .dio = {3, 6, LMIC_UNUSED_PIN},
  .rxtx_rx_active = 0,
  .rssi_cal = 8,              // LBT cal for the Adafruit Feather M0 LoRa, in dB
  .spi_freq = 8000000,
};
#elif defined(ARDUINO_AVR_FEATHER32U4)
// Pin mapping for Adafruit Feather 32u4 LoRa, etc.
// Just like Feather M0 LoRa, but uses SPI at 1MHz; and that's only
// because MCCI doesn't have a test board; probably higher frequencies
// will work.
// /!\ By default Feather 32u4's pin 6 and DIO1 are not connected. Please
// ensure they are connected.
const lmic_pinmap lmic_pins = {
  .nss = 8,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 4,
  .dio = {7, 6, LMIC_UNUSED_PIN},
  .rxtx_rx_active = 0,
  .rssi_cal = 8,              // LBT cal for the Adafruit Feather 32U4 LoRa, in dB
  .spi_freq = 1000000,
};
#elif defined(ARDUINO_CATENA_4551)
// Pin mapping for Murata module / Catena 4551
const lmic_pinmap lmic_pins = {
  .nss = 7,
  .rxtx = 29,
  .rst = 8,
  .dio = {
    25,    // DIO0 (IRQ) is D25
    26,    // DIO1 is D26
    27,    // DIO2 is D27
  },
  .rxtx_rx_active = 1,
  .rssi_cal = 10,
  .spi_freq = 8000000     // 8MHz
};
#else
# error "Unknown target"
#endif

void printHex2(unsigned v) {
  v &= 0xff;
  if (v < 16)
    Serial.print('0');
  Serial.print(v, HEX);
}

void onEvent (ev_t ev) {
  Serial.print(os_getTime());
  Serial.print(": ");
  switch (ev) {
    case EV_SCAN_TIMEOUT:
      Serial.println(F("EV_SCAN_TIMEOUT"));
      break;
    case EV_BEACON_FOUND:
      Serial.println(F("EV_BEACON_FOUND"));
      break;
    case EV_BEACON_MISSED:
      Serial.println(F("EV_BEACON_MISSED"));
      break;
    case EV_BEACON_TRACKED:
      Serial.println(F("EV_BEACON_TRACKED"));
      break;
    case EV_JOINING:
      Serial.println(F("EV_JOINING"));
      break;
    case EV_JOINED:
      Serial.println(F("EV_JOINED"));
      {
        u4_t netid = 0;
        devaddr_t devaddr = 0;
        u1_t nwkKey[16];
        u1_t artKey[16];
        LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
        Serial.print("netid: ");
        Serial.println(netid, DEC);
        Serial.print("devaddr: ");
        Serial.println(devaddr, HEX);
        Serial.print("AppSKey: ");
        for (size_t i = 0; i < sizeof(artKey); ++i) {
          if (i != 0)
            Serial.print("-");
          printHex2(artKey[i]);
        }
        Serial.println("");
        Serial.print("NwkSKey: ");
        for (size_t i = 0; i < sizeof(nwkKey); ++i) {
          if (i != 0)
            Serial.print("-");
          printHex2(nwkKey[i]);
        }
        Serial.println();
      }
      // Disable link check validation (automatically enabled
      // during join, but because slow data rates change max TX
      // size, we don't use it in this example.
      LMIC_setLinkCheckMode(0);
      break;
    /*
      || This event is defined but not used in the code. No
      || point in wasting codespace on it.
      ||
      || case EV_RFU1:
      ||     Serial.println(F("EV_RFU1"));
      ||     break;
    */
    case EV_JOIN_FAILED:
      Serial.println(F("EV_JOIN_FAILED"));
      break;
    case EV_REJOIN_FAILED:
      Serial.println(F("EV_REJOIN_FAILED"));
      break;
      break;
    case EV_TXCOMPLETE:
      Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
      if (LMIC.dataLen == BUFFER_SIZE) {
        Serial.println(F("Received "));
        Serial.println(LMIC.dataLen);
        Serial.println(F(" bytes of payload"));
        nowcast = LMIC.frame[LMIC.dataBeg] & 15;
        futurecast = (LMIC.frame[LMIC.dataBeg] >> 4) & 15;
        txInterval = TX_INTERVAL_LONG;
        stateMachine.setState(RECEIVED);
      } else {
        tries++;
        if(tries < MAX_TRIES){
          txInterval = TX_INTERVAL_SHORT;  
        } else {
          txInterval = TX_INTERVAL_LONG;
        }
        stateMachine.setState(SLEEP);
      }
      break;
    case EV_LOST_TSYNC:
      Serial.println(F("EV_LOST_TSYNC"));
      break;
    case EV_RESET:
      Serial.println(F("EV_RESET"));
      break;
    case EV_RXCOMPLETE:
      // data received in ping slot
      Serial.println(F("EV_RXCOMPLETE"));
      break;
    case EV_LINK_DEAD:
      Serial.println(F("EV_LINK_DEAD"));
      break;
    case EV_LINK_ALIVE:
      Serial.println(F("EV_LINK_ALIVE"));
      break;
    /*
      || This event is defined but not used in the code. No
      || point in wasting codespace on it.
      ||
      || case EV_SCAN_FOUND:
      ||    Serial.println(F("EV_SCAN_FOUND"));
      ||    break;
    */
    case EV_TXSTART:
      Serial.println(F("EV_TXSTART"));
      break;
    case EV_TXCANCELED:
      Serial.println(F("EV_TXCANCELED"));
      break;
    case EV_RXSTART:
      /* do not print anything -- it wrecks timing */
      break;
    case EV_JOIN_TXCOMPLETE:
      Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
      break;

    default:
      Serial.print(F("Unknown event: "));
      Serial.println((unsigned) ev);
      break;
  }
}

void do_send(osjob_t* j) {
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("OP_TXRXPEND, not sending"));
  } else {
    // Prepare upstream data transmission at the next possible time.
    float measuredvbat = analogRead(VBATPIN);
    measuredvbat *= 2;    // we divided by 2, so multiply back
    measuredvbat *= 3.3;  // Multiply by 3.3V, our reference voltage
    measuredvbat /= 1024; // convert to voltage
    measuredvbat *= 10;
    uint8_t vbat = measuredvbat;
    data[0] = vbat;
    LMIC_setTxData2(1, data, sizeof(data), 0);
    Serial.println(F("Packet queued"));
  }
  // Next TX is scheduled after TX_COMPLETE event.
}

void setup() {
  delay(5000);
  //    while (! Serial)
  //        ;
  Serial.begin(9600);

  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1) delay(10);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, let's set the time!");
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  Serial.println(F("Starting"));

  clearNowcastLed();
  clearFuturecastLed();

  // setup servo
  servo.attach(SERVO_PIN);
  servo.write(179);
  delay(1000);
//  digitalWrite(SERVO_ENABLE_PIN, LOW);
  servo.detach();

  // LMIC init
  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  LMIC.dn2Dr = DR_SF9;        // TTN uses SF9 for its RX2 window.
  LMIC_setDrTxpow(DR_SF12, 14);

  // Start job (sending automatically starts OTAA too), so start FSM in SEND 
  stateMachine.addState(SEND, &sendLoop, &sendInit);
  stateMachine.addState(RECEIVED, &receivedLoop, &receivedInit);
  stateMachine.addState(SLEEP, &sleepLoop, &sleepInit);
  stateMachine.setState(SEND);
}

//--------------------------------------- state inits
void sendInit() {
  Serial.println(F("SEND"));
  loops = 0;
//  tries++;
  do_send(&sendjob);
}

void receivedInit() {
  Serial.println(F("RECEIVED"));
  tries = 0;

  // set leds
  setNowcastLed(nowcast);
  setFuturecastLed(futurecast);

  Serial.print(F("nowcast: "));
  Serial.println(nowcast);
  
  Serial.print(F("forcast: "));
  Serial.println(futurecast);

  // calculate new servo position
  servoPos = abs((((178 / LED_NOW_AMT) * nowcast) + 1) - 179);

  Serial.print(F("new servo pos to reach: "));
  Serial.println(servoPos);

  servo.attach(SERVO_PIN);
  lastServoEvent = millis();
}

void sleepInit() {
  Serial.println(F("SLEEP"));
  Serial.print(F("duration: "));
  Serial.println(txInterval);
  nextSend = DateTime(rtc.now() + TimeSpan(txInterval));

  Serial.print(F("next send: "));
  Serial.print(nextSend.year(), DEC);
  Serial.print(F("/"));
  Serial.print(nextSend.month(), DEC);
  Serial.print(F("/"));
  Serial.print(nextSend.day(), DEC);
  Serial.print(F(" "));
  Serial.print(nextSend.hour(), DEC);
  Serial.print(F(":"));
  Serial.print(nextSend.minute(), DEC);
  Serial.print(F(":"));
  Serial.println(nextSend.second(), DEC);
}

//--------------------------------------- state loops
void sendLoop() {
  // this does nothing but wait until the onEvent method gets called
}

void receivedLoop() {
  uint32_t currMillis = millis();
  
  if(servoPos == currServoPos){
    servo.detach();
    delay(25); // lets give it some time to adjust
    stateMachine.setState(SLEEP);
//    digitalWrite(SERVO_ENABLE_PIN, LOW);
    return;
  }

  if(servoPos < currServoPos && currMillis - lastServoEvent > SERVO_ADJUST_PERIOD){
    currServoPos = max(currServoPos - 1, 1);
    Serial.print("servo position: ");
    Serial.println(currServoPos);
    servo.write(currServoPos);
    lastServoEvent = currMillis;
  }

  if(servoPos > currServoPos && currMillis - lastServoEvent > SERVO_ADJUST_PERIOD){
    currServoPos = min(currServoPos + 1, 179);
    Serial.print("servo position: ");
    Serial.println(currServoPos);
    servo.write(currServoPos);
    lastServoEvent = currMillis;
  }
}

void sleepLoop() {
  loops++;
  doSleep();

  if (rtc.now() > nextSend) {
    stateMachine.setState(SEND);
  }
}

void loop() {
  os_runloop_once();
  stateMachine.update();
}

void doSleep() {
  // sleepydog sleeping
//  digitalWrite(LED_BUILTIN, LOW);
  int sleepMS = Watchdog.sleep(10000);
//  digitalWrite(LED_BUILTIN, HIGH);
}

void setNowcastLed(uint8_t led) {
  pinMode(LED_NOW_A, nowcastLEDMatrix[led][PIN_CONFIG][0]);
  pinMode(LED_NOW_B, nowcastLEDMatrix[led][PIN_CONFIG][1]);
  pinMode(LED_NOW_C, nowcastLEDMatrix[led][PIN_CONFIG][2]);
  pinMode(LED_NOW_D, nowcastLEDMatrix[led][PIN_CONFIG][3]);
  pinMode(LED_NOW_E, nowcastLEDMatrix[led][PIN_CONFIG][4]);
  digitalWrite(LED_NOW_A, nowcastLEDMatrix[led][PIN_STATE][0]);
  digitalWrite(LED_NOW_B, nowcastLEDMatrix[led][PIN_STATE][1]);
  digitalWrite(LED_NOW_C, nowcastLEDMatrix[led][PIN_STATE][2]);
  digitalWrite(LED_NOW_D, nowcastLEDMatrix[led][PIN_STATE][3]);
  digitalWrite(LED_NOW_E, nowcastLEDMatrix[led][PIN_STATE][4]);
}

void setFuturecastLed(int led) {
  pinMode(LED_FUTURE_A, futurecastLEDMatrix[led][PIN_CONFIG][0]);
  pinMode(LED_FUTURE_B, futurecastLEDMatrix[led][PIN_CONFIG][1]);
  pinMode(LED_FUTURE_C, futurecastLEDMatrix[led][PIN_CONFIG][2]);
  digitalWrite( LED_FUTURE_A, futurecastLEDMatrix[led][PIN_STATE][0]);
  digitalWrite( LED_FUTURE_B, futurecastLEDMatrix[led][PIN_STATE][1]);
  digitalWrite( LED_FUTURE_C, futurecastLEDMatrix[led][PIN_STATE][2]);
}

void clearNowcastLed() {
  pinMode(LED_NOW_A, INPUT);
  pinMode(LED_NOW_B, INPUT);
  pinMode(LED_NOW_C, INPUT);
  pinMode(LED_NOW_D, INPUT);
  pinMode(LED_NOW_E, INPUT);
}

void clearFuturecastLed(){
  pinMode(LED_FUTURE_A, INPUT);
  pinMode(LED_FUTURE_B, INPUT);
  pinMode(LED_FUTURE_C, INPUT);
}
