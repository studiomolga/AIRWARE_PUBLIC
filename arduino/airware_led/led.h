#ifndef LED_H
#define LED_H

#include "constants.h"

class Led {

  unsigned long startTime;
  unsigned long duration;
  unsigned long onDuration;
  uint8_t ledState;
  uint8_t pin;
  
  public:
    Led(uint8_t p) {
      pin = p;
      pinMode(pin, OUTPUT);
      startTime = 0;
      duration = BLINK_DURATION_LOW_1;
      onDuration = BLINK_DURATION_LOW_1;
      ledState = LED_ON;
    }

    void setQuality(uint8_t q){
      switch(q){
        case QUALITY_LOW_1:
          onDuration = BLINK_DURATION_LOW_1;
          break;
        case QUALITY_LOW_2:
          onDuration = BLINK_DURATION_LOW_2;
          break;
        case QUALITY_LOW_3:
          onDuration = BLINK_DURATION_LOW_3;
          break;
        case QUALITY_MODERATE_4:
          onDuration = BLINK_DURATION_MODERATE_4;
          break;
        case QUALITY_MODERATE_5:
          onDuration = BLINK_DURATION_MODERATE_5;
          break;
        case QUALITY_MODERATE_6:
          onDuration = BLINK_DURATION_MODERATE_6;
          break;
        case QUALITY_HIGH_7:
          onDuration = BLINK_DURATION_HIGH_7;
          break;
        case QUALITY_HIGH_8:
          onDuration = BLINK_DURATION_HIGH_8;
          break;
        case QUALITY_HIGH_9:
          onDuration = BLINK_DURATION_HIGH_9;
          break;
        case QUALITY_VERY_HIGH_10:
          onDuration = BLINK_DURATION_VERY_HIGH_10;
          break;
      }
      startTime = millis();
      ledState = LED_ON;
    }

    void update(){
      unsigned long currTime = millis();

      if(currTime - startTime > duration){
        switch(ledState){
          case LED_ON:
            digitalWrite(pin, HIGH);
            duration = onDuration;
            ledState = LED_OFF;
            break;
          case LED_OFF:
            digitalWrite(pin, LOW);
            duration = BLINK_DURATION_OFF;
            ledState = LED_ON;
        }
        
        startTime = currTime;
      }
    }
};

#endif 
