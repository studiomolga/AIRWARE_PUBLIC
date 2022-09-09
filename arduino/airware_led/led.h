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
      duration = BLINK_DURATION_SLOW;
      onDuration = BLINK_DURATION_SLOW;
      ledState = LED_ON;
    }

    void setQuality(uint8_t q){
      switch(q){
        case QUALITY_LOW:
          onDuration = BLINK_DURATION_SLOW;
          break;
        case QUALITY_MODERATE:
          onDuration = BLINK_DURATION_MEDIUM;
          break;
        case QUALITY_HIGH:
          onDuration = BLINK_DURATION_FAST;
          break;
        case QUALITY_VERY_HIGH:
          onDuration = BLINK_DURATION_VERY_FAST;
          break;
      }
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
