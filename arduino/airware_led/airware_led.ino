#include <Wire.h>
#include "led.h"
#include "RTClib.h"
#include <avr/sleep.h>

Led testLed(4);
Led led[] = {(12), (11), (9), (5)};

#define CLOCK_INTERRUPT_PIN 10
#define ON_HOUR 7
#define OFF_HOUR 23
#define ON 0
#define OFF 1
#define MAX_MESSAGE_LENGTH 2
#define SLEEPING_PIN 6

RTC_DS3231 rtc;
volatile uint8_t interruptState;
volatile uint8_t sqwState = true;
volatile bool doHandleInterrupt = false;

void setup() {
  Serial.begin(9600);
  Serial.println("starting!");

  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1) delay(10);
  }

  Wire.begin(9);
  Wire.onReceive(receiveEvent);

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, let's set the time!");
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  pinMode(CLOCK_INTERRUPT_PIN, INPUT_PULLUP);
  PCICR |= B00000001;
  PCMSK0 |= B01000000;

  rtc.clearAlarm(1);
  rtc.clearAlarm(2);

  rtc.writeSqwPinMode(DS3231_OFF);

  rtc.disableAlarm(2);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  rtc.setAlarm1(DateTime(2020, 6, 25, OFF_HOUR, 0, 0), DS3231_A1_Hour);
  interruptState = OFF;

  pinMode(SLEEPING_PIN, OUTPUT);
  digitalWrite(SLEEPING_PIN, HIGH);
}

void loop() {
  if (doHandleInterrupt) {
    if (interruptState == OFF) {
      interruptState = ON;
      onSleep();
    }
    doHandleInterrupt = false;
  }

  for (int i = 0; i < 4; i++) {
    led[i].update();
  }
}

void receiveEvent(int bytes) {
  Serial.print("received bytes: ");
  Serial.println(bytes);
  uint8_t bIndex = 0;
  while (Wire.available())
  {
    byte b = Wire.read();
    Serial.print("byte index: ");
    Serial.print(bIndex);
    Serial.print(" | hex: ");
    Serial.print(b, HEX);
    Serial.print(" | binary: ");
    Serial.print(b, BIN);
    Serial.println();
    for (int i = 0; i < 2; i++) {
      uint8_t bitIndex = i * 4;
      uint8_t ledIndex = i + (bIndex * bytes);
      uint8_t val = (b >> bitIndex) & 15;
      led[ledIndex].setQuality(val);
      Serial.print(" | bit index: ");
      Serial.print(bitIndex);
      Serial.print(" | led index: ");
      Serial.print(ledIndex);
      Serial.print(" | value: ");
      Serial.print(val);
      Serial.print(" ");
    }
    bIndex++;
    Serial.println();
  }
  Serial.println();
}

ISR (PCINT0_vect) {
  Serial.println("interrupt callback called");
  if (digitalRead(CLOCK_INTERRUPT_PIN) != sqwState) {
    if (sqwState) {
      Serial.println("falling edge");
      if (interruptState == ON) {
        sleep_disable();
      }
      doHandleInterrupt = true;
    }
    sqwState = !sqwState;
  }
}

void onSleep() {
  Serial.println("sleep occured!");
//  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(SLEEPING_PIN, LOW);
  rtc.setAlarm1(DateTime(2020, 6, 25, ON_HOUR, 0, 0), DS3231_A1_Hour);
  rtc.clearAlarm(1);

  Wire.end();
//  Wire.onReceive(receiveEvent);

  for (int i = 0; i < 4; i++) {
    led[i].turnOff();
  }

  sleep_enable();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  delay(1000);
  sleep_cpu();
  Serial.println("woke up");

  Wire.begin();
  Wire.onReceive(receiveEvent);
  interruptState = OFF;
  digitalWrite(SLEEPING_PIN, HIGH);
//  digitalWrite(LED_BUILTIN, HIGH);
  rtc.setAlarm1(DateTime(2020, 6, 25, OFF_HOUR, 0, 0), DS3231_A1_Hour);
  rtc.clearAlarm(1);
}
