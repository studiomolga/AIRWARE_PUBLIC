#include <Wire.h>
#include "led.h"

Led led[] = {(12), (11), (10), (9)};

void setup() {
  Serial.begin(9600);
  Serial.println("starting!");

  Wire.begin(9);
  Wire.onReceive(receiveEvent);
}

void loop() {
  for(int i = 0; i < 4; i++){
    led[i].update();
  }
}

void receiveEvent(int bytes){
  Serial.print("received bytes: ");
  Serial.println(bytes);
  uint8_t bIndex = 0;
  while(Wire.available())
  {
      byte b = Wire.read();
      Serial.print("byte index: ");
      Serial.print(bIndex);
      Serial.print(" | hex: ");
      Serial.print(b, HEX);
      Serial.print(" | binary: ");
      Serial.print(b, BIN);
      Serial.println();
      for(int i = 0; i < 2; i++){
        uint8_t bitIndex = i*4;
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
