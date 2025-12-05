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
const int BUTTON_STOP_PIN  = 4;

// LED Pins
const int LED_YELLOW = 22;
const int LED_GREEN  = 23;
const int LED_RED    = 24;
const int LED_BLUE   = 25;

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
volatile bool stopButtonPressed  = false;

int currentStepperPosition = 0;
int previousPotValue = 0;

// Millis Variables
unsigned long currentMillis = 0;
unsigned long lastLCDUpdate = 0;
unsigned long lastStart = 0;
unsigned long lastReset = 0;
unsigned long lastStop = 0;

void setup(){
  Serial.begin(9600);

  // Output Pins
  pinMode(FAN_ENABLE_PIN, OUTPUT);
  pinMode(FAN_IN1_PIN, OUTPUT);
  pinMode(FAN_IN2_PIN, OUTPUT);
  
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  // Input Pins
  pinMode(BUTTON_START_PIN, INPUT);
  pinMode(BUTTON_RESET_PIN, INPUT);
  pinMode(BUTTON_STOP_PIN,  INPUT);

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
  currentMillis = millis();
  // Check ISR Flag for Start Button
  if(startButtonPressed){
    startButtonPressed = false;
    if(currentState == DISABLED){
      transitionTo(IDLE);
    }
  }

  if(digitalRead(BUTTON_STOP_PIN) == HIGH){
    if(currentMillis - lastStop >= 1000){
      lastStop = currentMillis;
      if(currentState != DISABLED){
        transitionTo(DISABLED);
      }
    }
  }

  if(digitalRead(BUTTON_RESET_PIN) == HIGH){
    if(currentMillis - lastReset >= 1000){
      lastReset = currentMillis;
      resetButtonPressed = true;
    }
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
  
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_BLUE, LOW);

  logEvent(newState);

  switch(newState){
    case DISABLED:
      digitalWrite(LED_YELLOW, HIGH);
      setFanMotor(false);
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("DISABLED");
      break;

    case IDLE:
      digitalWrite(LED_GREEN, HIGH);
      setFanMotor(false);
      updateLCD(dht.readTemperature(), dht.readHumidity());
      break;

    case ERROR:
      digitalWrite(LED_RED, HIGH);
      setFanMotor(false);
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("ERROR: Low Water");
      break;

    case RUNNING:
      digitalWrite(LED_BLUE, HIGH);
      setFanMotor(true);
      break;
  }
}

void setFanMotor(bool on){
  if(on){
    digitalWrite(FAN_ENABLE_PIN, HIGH);
    digitalWrite(FAN_IN1_PIN, HIGH);
    digitalWrite(FAN_IN2_PIN, LOW);
  }
  else{
    digitalWrite(FAN_ENABLE_PIN, LOW);
    digitalWrite(FAN_IN1_PIN, LOW);
    digitalWrite(FAN_IN2_PIN, LOW);
  }
}

void ventControl(){
  int potVal = analogRead(POTENTIOMETER_PIN);
  
  if(abs(potVal - previousPotValue) > 50){
    int targetPos = map(potVal, 0, 1023, 0, 1278);
    int stepsToMove = targetPos - currentStepperPosition;
    
    if(stepsToMove != 0){
      ventMove.step(stepsToMove);
      currentStepperPosition = targetPos;
      
      Serial.print("\nVent position changed to ");
      Serial.print(currentStepperPosition);
      Serial.print(" at ");
      DateTime now = rtc.now();
      Serial.println(now.timestamp());
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