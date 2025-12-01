//  ||||| TO FIX |||||
//  Add stop button
//  Fix water level sensor being able to change for 5 seconds after reset is pressed (once only)
//  Add stepper motor change threshold
//  General revisions and corrections

#include <LiquidCrystal.h>
#include <Stepper.h>
#include <DHT.h>
#include <RTClib.h>

// LCD Pins
const int RS = 37;
const int EN = 36;
const int D4 = 35;
const int D5 = 34;
const int D6 = 33;
const int D7 = 32;
LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);

// Stepper Motor Pins
const int STEP_IN1 = 13;
const int STEP_IN2 = 12;
const int STEP_IN3 = 11;
const int STEP_IN4 = 10;
const int STEPS_PER_REVOLUTION = 2038;
Stepper ventMove(STEPS_PER_REVOLUTION, STEP_IN1, STEP_IN3, STEP_IN2, STEP_IN4);

// DC Motor Pins
const int FAN_ENABLE_PIN = 8;
const int FAN_IN1_PIN    = 7;
const int FAN_IN2_PIN    = 6;

// Sensor Pins
const int DHT_PIN = 5;
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

const int WATER_SENSOR_PIN  = A0; 
const int POTENTIOMETER_PIN = A1; 

// Input Pins
const int BUTTON_START_PIN = 2;
const int BUTTON_RESET_PIN = 3;

// LED Pins
const int LED_YELLOW = 22;
const int LED_GREEN  = 23;
const int LED_RED    = 24;
const int LED_BLUE   = 25;

//GPIO addresses
volatile unsigned char* pin_h = (unsigned char*) 0x100;
volatile unsigned char* ddr_h = (unsigned char*) 0x101;
volatile unsigned char* port_h = (unsigned char*) 0x102;

volatile unsigned char* pin_a = (unsigned char*) 0x20;
volatile unsigned char* ddr_a = (unsigned char*) 0x21;
volatile unsigned char* port_a = (unsigned char*) 0x22;

volatile unsigned char* pin_e = (unsigned char*) 0x2C;
volatile unsigned char* ddr_e = (unsigned char*) 0x2D;
volatile unsigned char* port_e = (unsigned char*) 0x2E;

// RTC
RTC_DS1307 rtc;

// States
enum State{
  DISABLED,
  IDLE,
  ERROR,
  RUNNING
};

// Global Variables
volatile State currentState = DISABLED;
volatile bool startButtonPressed = false;
volatile bool resetButtonPressed = false;

unsigned long lastLCDUpdate = 0;

int currentStepperPosition = 0;
int previousPotValue = 0;

void setup(){
  Serial.begin(9600);

  // Output Pins
    //Setup digital pin 8 (FAN_ENABLE_PIN) for output
  ddr_h |= 0x20;
    //Setup digial pin 7 (FAN_IN1_PIN) for output
  ddr_h |= 0x10;
    //Setup digial pin 6 (FAN_IN2_PIN) for output
  ddr_h |= 0x08;
  
    //Setup digital pin 22 (LED_YELLOW) for output
  ddr_a |= 0x01;
    //Setup digital pin 23 (LED_GREEN) for output
  ddr_a |= 0x02;
    //Setup digital pin 24 (LED_RED) for output
  ddr_a |= 0x04;
    //Setup digital pin 25 (LED_BLUE) for output
  ddr_a |= 0x08;

  // Input Pins
    //Setup digital pin 2 (BUTTON_START_PIN) for input (without pullup)
  ddr_e &= ~(0x10);
  port_e &= ~(0x10);
    //Setup digital pin 3 (BUTTON_RESET_PIN) for input (without pullup)
  ddr_e &= ~(0x20);
  port_e &= ~(0x20);

  lcd.begin(16, 2);
  dht.begin();
  ventMove.setSpeed(10);

  rtc.begin();

  if(!rtc.isrunning()){
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Interrupt for Start Button
  attachInterrupt(digitalPinToInterrupt(BUTTON_START_PIN), startInterrupt, RISING);

  // Initialize System
  previousPotValue = analogRead(POTENTIOMETER_PIN);
  transitionTo(DISABLED);
}

void loop(){
  // Check ISR Flag for Start Button
  if(startButtonPressed){
    startButtonPressed = false;
    if(currentState == DISABLED){
      transitionTo(IDLE);
    }
  }

  //Read digial pin 3 (BUTTON_RESET_PIN)
  if(pin_e & 0x20 == HIGH){
    resetButtonPressed = true;
  }

  // Vent Control
  if(currentState != DISABLED){
    ventControl();
  }

  // State Changes
  switch(currentState){
    case DISABLED:
      break;

    case IDLE:
      idleMonitoring();
      break;

    case ERROR:
      errorMonitoring();
      break;

    case RUNNING:
      runningMonitoring();
      break;
  }

// LCD Update (Change back to 60000 after DEBUG)
  if(currentState != DISABLED && currentState != ERROR){
    unsigned long currentMillis = millis();
    if(currentMillis - lastLCDUpdate >= 600){
      lastLCDUpdate = currentMillis;
      float t = dht.readTemperature();
      float h = dht.readHumidity();
      updateLCD(t, h);
    }
  }
}


// State Functions
void idleMonitoring() {
  if(analogRead(WATER_SENSOR_PIN) < 25){
    transitionTo(ERROR);
    return;
  }

  float t = dht.readTemperature();
  if(t > 24.0){
    transitionTo(RUNNING);
  }
}

void errorMonitoring(){
  if(resetButtonPressed){
    resetButtonPressed = false;
    if(analogRead(WATER_SENSOR_PIN) > 25){
      transitionTo(IDLE);
    }
  }
}

void runningMonitoring(){
  if(analogRead(WATER_SENSOR_PIN) < 25){
    transitionTo(ERROR);
    return;
  }

  float t = dht.readTemperature();
  if (t <= 24.0){
    transitionTo(IDLE);
  }
}

void transitionTo(State newState){
  currentState = newState;
  
  //Write digial pin 22 (LED_YELLOW) LOW
  port_a &= ~(0x01);
  //Write digial pin 23 (LED_GREEN) LOW
  port_a &= ~(0x02);
  //Write digial pin 24 (LED_RED) LOW
  port_a &= ~(0x04);
  //Write digial pin 25 (LED_BLUE) LOW
  port_a &= ~(0x08);

  logEvent(newState);

  switch(newState){
    case DISABLED:
      //Write digial pin 22 (LED_YELLOW) HIGH
      port_a |= 0x01;
      setFanMotor(false);
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("DISABLED");
      break;

    case IDLE:
      //Write digial pin 23 (LED_GREEN) HIGH
      port_a |= 0x02;
      setFanMotor(false);
      updateLCD(dht.readTemperature(), dht.readHumidity());
      break;

    case ERROR:
      //Write digial pin 24 (LED_RED) HIGH
      port_a |= 0x04;
      setFanMotor(false);
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("ERROR: Low Water");
      break;

    case RUNNING:
      //Write digial pin 25 (LED_BLUE) HIGH
      port_a |= 0x08;
      setFanMotor(true);
      break;
  }
}

void setFanMotor(bool on){
  if(on){
    //Write digial pin 8 (FAN_ENABLE_PIN) HIGH
    port_h |= 0x20;
    //Write digial pin 7 (FAN_IN1_PIN) HIGH
    port_h |= 0x10;
    //Write digial pin 6 (FAN_IN2_PIN) LOW
    port_h &= ~(0x08);
  }
  else{
    //Write digial pin 8 (FAN_ENABLE_PIN) LOW
    port_h &= ~(0x20);
    //Write digial pin 7 (FAN_IN1_PIN) LOW
    port_h &= ~(0x10);
    //Write digial pin 6 (FAN_IN2_PIN) LOW
    port_h &= ~(0x08);
  }
}

void ventControl(){
  int potVal = analogRead(POTENTIOMETER_PIN);
  
  if(abs(potVal - previousPotValue) > 50){
    int targetPos = map(potVal, 0, 1023, 0, 1278);
    int stepsToMove = targetPos - currentStepperPosition;
    
    if(stepsToMove != 0) {
      ventMove.step(stepsToMove);
      currentStepperPosition = targetPos;
    }
    previousPotValue = potVal;
  }
}

void updateLCD(float t, float h) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Temp:   "); lcd.print(t); lcd.print("C");
  lcd.setCursor(0, 1);
  lcd.print("Humid:  "); lcd.print(h); lcd.print("%");
}

void logEvent(State state) {
  Serial.print("Transition to ");
  switch(state){
    case DISABLED: 
      Serial.print("DISABLED"); 
      break;

    case IDLE: 
      Serial.print("IDLE"); 
      break;

    case ERROR: 
      Serial.print("ERROR"); 
      break;

    case RUNNING: 
      Serial.print("RUNNING"); 
      break;
  }
  Serial.print(" at ");
  DateTime now = rtc.now();
  Serial.println(now.timestamp());
}

void startInterrupt(){
  startButtonPressed = true;
}