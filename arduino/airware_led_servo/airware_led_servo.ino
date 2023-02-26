#include <Wire.h>
#include <Servo.h>

//#include <Adafruit_PWMServoDriver.h>

Servo servo;
uint8_t ledPins[] = {10, 9, 6};

//Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x44);

void setup() {
  Serial.begin(9600);
  Serial.println("starting!");

  Wire.begin(9);
  Wire.onReceive(receiveEvent);

  servo.attach(11);

//  pwm.begin();
//  pwm.setOscillatorFrequency(27000000);
//  pwm.setPWMFreq(50);  // Analog servos run at ~50 Hz updates

  for(uint8_t i = 0; i < 3; i++){
    pinMode(ledPins[i], OUTPUT);   
    
  }

  digitalWrite(10, LOW);
  digitalWrite(9, LOW);
  digitalWrite(6, LOW);

  servo.write(0);
//  for (uint16_t pulselen = 150; pulselen < 600; pulselen++) {
//    pwm.setPWM(0, 0, pulselen);
//  }
}

void loop() {
//  delay(1);
}

void receiveEvent(int bytes){
  Serial.print("received bytes: ");
  Serial.println(bytes);
  uint8_t bIndex = 0;
  uint8_t maxVal = 0;
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
        uint8_t val = (b >> bitIndex) & 15;
        if(val > maxVal) {
          maxVal = val;  
        }
        Serial.print(" | bit index: ");
        Serial.print(bitIndex);
        Serial.print(" | value: ");
        Serial.print(val);
        Serial.print(" ");
      }
      bIndex++;
      Serial.println();
  }
  servo.write(maxVal * 18);
  Serial.println();
}
