/*
  UNR CPE 301 Final Project: Evaporation Cooling System
  Kevin O'Connell and Vincent Toney
*/

#include <LiquidCrystal.h>
#include <Stepper.h>
#include <DHT.h>
#include <RTClib.h>

// Library Initializations
LiquidCrystal lcd(37, 36, 35, 34, 33, 32);

Stepper ventMove(2038, 13, 11, 12, 10);

#define DHTTYPE DHT11
DHT dht(5, DHTTYPE);

RTC_DS1307 rtc;

// GPIO addresses
#define RDA 0x80
#define TBE 0x20
volatile unsigned char* pin_h  = (unsigned char*) 0x100;
volatile unsigned char* ddr_h  = (unsigned char*) 0x101;
volatile unsigned char* port_h = (unsigned char*) 0x102;

volatile unsigned char* pin_a  = (unsigned char*) 0x20;
volatile unsigned char* ddr_a  = (unsigned char*) 0x21;
volatile unsigned char* port_a = (unsigned char*) 0x22;

volatile unsigned char* pin_e  = (unsigned char*) 0x2C;
volatile unsigned char* ddr_e  = (unsigned char*) 0x2D;
volatile unsigned char* port_e = (unsigned char*) 0x2E;

volatile unsigned char* pin_g  = (unsigned char*) 0x32;
volatile unsigned char* ddr_g  = (unsigned char*) 0x33;
volatile unsigned char* port_g = (unsigned char*) 0x34;

// Serial addresses
volatile unsigned char *myUCSR0A = (unsigned char*) 0x00C0;
volatile unsigned char *myUCSR0B = (unsigned char*) 0x00C1;
volatile unsigned char *myUCSR0C = (unsigned char*) 0x00C2;
volatile unsigned int  *myUBRR0  = (unsigned int* ) 0x00C4;
volatile unsigned char *myUDR0   = (unsigned char*) 0x00C6;

// Analog addresses
volatile unsigned char* my_ADMUX    = (unsigned char*) 0x7C;
volatile unsigned char* my_ADCSRB   = (unsigned char*) 0x7B;
volatile unsigned char* my_ADCSRA   = (unsigned char*) 0x7A;
volatile unsigned int*  my_ADC_DATA = (unsigned int* ) 0x78;

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

int currentStepperPosition = 0;
int previousPotValue = 0;

// Millis Variables
unsigned long currentMillis = 0;
unsigned long lastLCDUpdate = 0;
unsigned long lastStart = 0;
unsigned long lastReset = 0;
unsigned long lastStop = 0;

// Setup
void setup(){
  U0init(9600); // UART Initialization
  adc_init(); // ADC Initialization

  // Output Pins
  *ddr_h |= 0x20; // Fan Enable, Pin 8
  *ddr_h |= 0x10; // Fan In1, Pin 7
  *ddr_h |= 0x08; // Fan In2, Pin 6

  *ddr_a |= 0x01; // Yellow LED, Pin 22
  *ddr_a |= 0x02; // Green LED, Pin 23
  *ddr_a |= 0x04; // Red LED, Pin 24
  *ddr_a |= 0x08; // Blue LED, Pin 25

  // Input Pins
  *ddr_e &= ~(0x10);
  *port_e &= ~(0x10); // Start Button, Pin 2
  *ddr_e &= ~(0x20);
  *port_e &= ~(0x20); // Reset Button, Pin 3
  *ddr_g &= ~(0x20);
  *port_g &= ~(0x20); // Stop Button, Pin 4

  // Component Initialization
  lcd.begin(16, 2);
  ventMove.setSpeed(10);
  dht.begin();
  rtc.begin();

  if(!rtc.isrunning()){
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  attachInterrupt(digitalPinToInterrupt(2), startInterrupt, RISING); // Start ISR

  previousPotValue = adc_read(1); // Stepper Potentiometer, Pin A1
  transitionTo(DISABLED);
}

// Loop
void loop(){
  currentMillis = millis();

  // Start Button Handling
  if(startButtonPressed){
    startButtonPressed = false;
    if(currentState == DISABLED){
      transitionTo(IDLE);
    }
  }

  // Stop Button Handling
  if(*pin_g & 0x20){
    if(currentMillis - lastStop >= 100){
      lastStop = currentMillis;
      if(currentState != DISABLED){
        transitionTo(DISABLED);
      }
    }
  }

  // Reset Button Handling
  if(*pin_e & 0x20){
    if(currentMillis - lastReset >= 100){
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
      idleHandling();
      break;

    case ERROR:
      errorHandling();
      break;

    case RUNNING:
      runningHandling();
      break;
  }

  if(currentState != DISABLED && currentState != ERROR){
    if(currentMillis - lastLCDUpdate >= 60000){
      lastLCDUpdate = currentMillis;
      float t = dht.readTemperature();
      float h = dht.readHumidity();
      updateLCD(t, h);
    }
  }
}

// State Functions
void idleHandling(){
  if(adc_read(0) < 250){
    transitionTo(ERROR);
    return;
  }

  float t = dht.readTemperature();
  if(t > 24.0){
    transitionTo(RUNNING);
  }
}

void errorHandling(){
  if(resetButtonPressed){
    resetButtonPressed = false;
    if(adc_read(0) > 250){
      transitionTo(IDLE);
    }
  }
}

void runningHandling(){
  if(adc_read(0) < 250){
    transitionTo(ERROR);
    return;
  }

  float t = dht.readTemperature();
  if (t <= 24.0){
    transitionTo(IDLE);
  }
}

// State Transition Handling
void transitionTo(State newState){
  currentState = newState;
  
  // All LED LOW
  *port_a &= ~(0x01);
  *port_a &= ~(0x02);
  *port_a &= ~(0x04);
  *port_a &= ~(0x08);

  logEvent(newState);

  switch(newState){
    case DISABLED:
      *port_a |= 0x01; // Yellow HIGH
      setFanMotor(false);
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Disabled");
      break;

    case IDLE:
      *port_a |= 0x02; // Green HIGH
      setFanMotor(false);
      updateLCD(dht.readTemperature(), dht.readHumidity());
      break;

    case ERROR:
      *port_a |= 0x04; // Red HIGH
      setFanMotor(false);
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Error: Low Water");
      break;

    case RUNNING:
      *port_a |= 0x08; // Blue HIGH
      setFanMotor(true);
      break;
  }
}

// Fan Motor Control
void setFanMotor(bool on){
  if(on){
    *port_h |= 0x20;
    *port_h |= 0x10;
    *port_h &= ~(0x08);
  }
  else{
    *port_h &= ~(0x20);
    *port_h &= ~(0x10);
    *port_h &= ~(0x08);
  }
}

// Vent Control
  int buffer[10];
  int bufferIndex = 0;
void ventControl(){
  buffer[bufferIndex] = adc_read(1);
  bufferIndex++;

  if(bufferIndex < 9){
    return;
  }
  bufferIndex = 0;

  int potVal = 0;
  for(int i = 0; i < 10; i++){
    potVal += buffer[i];
  }
  potVal /= 10;

  if(abs(potVal - previousPotValue) > 80){
    int targetPos = map(potVal, 0, 1023, 0, 1278);
    int stepsToMove = targetPos - currentStepperPosition;
    
    if(stepsToMove != 0){
      ventMove.step(stepsToMove);
      currentStepperPosition = targetPos;
      
      U0putchar('\n');
      U0putstring("Vent position changed to ", 25);
      String currentStepperPositionString = String(currentStepperPosition);
      U0putstring((unsigned char*)currentStepperPositionString.c_str(), currentStepperPositionString.length());
      U0putstring(" at ", 4);
      DateTime now = rtc.now();
      int length = strlen(now.timestamp().c_str());
      U0putstring((unsigned char*)now.timestamp().c_str(), length);
      U0putchar('\n');

      previousPotValue = potVal;
    }
  }
}

// LCD Updater
void updateLCD(float t, float h){
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Temp:   "); lcd.print(t); lcd.print("C");
  lcd.setCursor(0, 1);
  lcd.print("Humid:  "); lcd.print(h); lcd.print("%");
}

// Serial Monitor Logger
void logEvent(State state){
  U0putstring("Transitioned to ", 16);

  switch(state){
    case DISABLED: 
      U0putstring("disabled", 8);
      break;

    case IDLE: 
      U0putstring("idle", 4); 
      break;

    case ERROR: 
      U0putstring("error", 5);
      break;

    case RUNNING: 
      U0putstring("running", 7);
      break;
  }

  U0putstring(" at ", 4);
  DateTime now = rtc.now();
  int length = strlen(now.timestamp().c_str());
  U0putstring((unsigned char*)now.timestamp().c_str(), length);
  U0putchar('\n');
}

// Start Interrupt
void startInterrupt(){
  startButtonPressed = true;
}

// Serial IO functions
void U0init(int U0baud){
  unsigned long FCPU = 16000000;
  unsigned int tbaud;
  tbaud = (FCPU / 16 / U0baud - 1);
  *myUCSR0A = 0x20;
  *myUCSR0B = 0x18;
  *myUCSR0C = 0x06;
  *myUBRR0  = tbaud;
}

unsigned char U0kbhit(){
  return *myUCSR0A & RDA;
}

unsigned char U0getchar(){
  return *myUDR0;
}

void U0putchar(unsigned char U0pdata){
  while((*myUCSR0A & TBE)==0);
  *myUDR0 = U0pdata;
}

void U0putstring(unsigned char* str, int len){
  for(int i = 0; i < len; i++){
    U0putchar(str[i]);
  }
}

// Analog IO functions
void adc_init(){
  // A Register Setup
  *my_ADCSRA |= 0x80; // Enable ADC
  *my_ADCSRA &= 0xDF; // Disable ADC trigger mode
  *my_ADCSRA &= 0xF7; // Disable ADC interrupt
  *my_ADCSRA &= 0xF8; // Set prescaler selection to slow reading

  // B Register Setup
  *my_ADCSRB &= 0xF7; // Reset channel
  *my_ADCSRB &= 0xF8; // Set free running mode

  // MUX Register Setup
  *my_ADMUX &= 0x7F; // AVCC analog reference
  *my_ADMUX |= 0x40; // AVCC analog reference
  *my_ADMUX &= 0xDF; // Right adjust result
  *my_ADMUX &= 0xE0; // Reset channel
}

unsigned int adc_read(unsigned char adc_channel_num){
  *my_ADMUX &= 0xE0;
  *my_ADCSRB &= 0xF7;

  *my_ADMUX |= (0x1F & adc_channel_num);
  *my_ADCSRB |= ((adc_channel_num & 0x20) >> 2);

  *my_ADCSRA |= 0x40;

  while((*my_ADCSRA & 0x40) != 0);

  unsigned int val = (*my_ADC_DATA & 0x03FF);
  return val;
}
