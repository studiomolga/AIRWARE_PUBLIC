#include <Wire.h>
#include "led.h"

//uint8_t no2 = 0;
//uint8_t o3 = 0;
//uint8_t pm10 = 0;
//uint8_t pm25 = 0;
unsigned long pauseTime = 0;
Led led[] = {(12), (11), (10), (9)};

void setup() {
  Serial.begin(9600);
  Serial.println("starting!");

  Wire.begin(9);
  Wire.onReceive(receiveEvent);
}

void loop() {
  //Serial.println("test");
  for(int i = 0; i < 4; i++){
    led[i].update();
  }

  //delay(5);
}

void receiveEvent(int bytes){
  // we do some check whether we are getting data in the form we expect
  Serial.println("received bytes");
  if(bytes == 2){
    for(int b = 0; b < bytes; b++){
      uint8_t vals = Wire.read();
      for(int i = 0; i < 2; i++)
      {
          uint8_t index = i + (b*bytes);
          uint8_t val = (vals >> (index * 4)) & 15;
          led[index].setQuality(val);
          Serial.print("id: ");
          Serial.print(index);
          Serial.print(", val: ");
          Serial.print(val);
          Serial.print(" ");
      }
      Serial.println();  
    }
  }
}
