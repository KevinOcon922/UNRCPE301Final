#include <LiquidCrystal.h>
#include <Stepper.h>
#include <DHT.h>
#include <RTClib.h>

// PINS
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

  volatile unsigned char* pin_g = (unsigned char*) 0x32;
  volatile unsigned char* ddr_g = (unsigned char*) 0x33;
  volatile unsigned char* port_g = (unsigned char*) 0x34;

  #define RDA 0x80
  #define TBE 0x20  

  //Serial addresses
  volatile unsigned char *myUCSR0A = (unsigned char *)0x00C0;
  volatile unsigned char *myUCSR0B = (unsigned char *)0x00C1;
  volatile unsigned char *myUCSR0C = (unsigned char *)0x00C2;
  volatile unsigned int  *myUBRR0  = (unsigned int *) 0x00C4;
  volatile unsigned char *myUDR0   = (unsigned char *)0x00C6;
 
  //Analog addresses
  volatile unsigned char* my_ADMUX = (unsigned char*) 0x7C;
  volatile unsigned char* my_ADCSRB = (unsigned char*) 0x7B;
  volatile unsigned char* my_ADCSRA = (unsigned char*) 0x7A;
  volatile unsigned int* my_ADC_DATA = (unsigned int*) 0x78;

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

  int currentStepperPosition = 0;
  int previousPotValue = 0;

  // Millis Variables
  unsigned long currentMillis = 0;
  unsigned long lastLCDUpdate = 0;
  unsigned long lastStart = 0;
  unsigned long lastReset = 0;
  unsigned long lastStop = 0;

void setup(){
  // setup the UART
  U0init(9600);
  // setup the ADC
  adc_init();

  // Output Pins
    //Setup digital pin 8 (FAN_ENABLE_PIN) for output
  *ddr_h |= 0x20;
    //Setup digial pin 7 (FAN_IN1_PIN) for output
  *ddr_h |= 0x10;
    //Setup digial pin 6 (FAN_IN2_PIN) for output
  *ddr_h |= 0x08;

    //Setup digital pin 22 (LED_YELLOW) for output
  *ddr_a |= 0x01;
    //Setup digital pin 23 (LED_GREEN) for output
  *ddr_a |= 0x02;
    //Setup digital pin 24 (LED_RED) for output
  *ddr_a |= 0x04;
    //Setup digital pin 25 (LED_BLUE) for output
  *ddr_a |= 0x08;

  // Input Pins
    //Setup digital pin 2 (BUTTON_START_PIN) for input (without pullup)
  *ddr_e &= ~(0x10);
  *port_e &= ~(0x10);
    //Setup digital pin 3 (BUTTON_RESET_PIN) for input (without pullup)
  *ddr_e &= ~(0x20);
  *port_e &= ~(0x20);
    //Setup digital pin 4 (BUTTON_STOP_PIN) for input (without pullup)
  *ddr_g &= ~(0x20);
  *port_g &= ~(0x20);

  // Component Initialization
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
  //Read analog pin 1 (Potentiometer pin)
  previousPotValue = adc_read(1);
  transitionTo(DISABLED);
}

void loop(){
  currentMillis = millis();

  // Start Button Handling
  if(startButtonPressed){
    startButtonPressed = false;
    if(currentState == DISABLED){
      transitionTo(IDLE);
    }
  }

  // Stop Button Handling - Read digital pin 4 (BUTTON_STOP_PIN)
  if(*pin_g & 0x20 == HIGH){
    if(currentMillis - lastStop >= 100){
      lastStop = currentMillis;
      if(currentState != DISABLED){
        transitionTo(DISABLED);
      }
    }
  }

  // Reset Button Handling - //Read digial pin 3 (BUTTON_RESET_PIN)
  if(*pin_e & 0x20 == HIGH){
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

  // LCD Update (Change back to 60000 after DEBUG)
  if(currentState != DISABLED && currentState != ERROR){
    if(currentMillis - lastLCDUpdate >= 1000){
      lastLCDUpdate = currentMillis;
      float t = dht.readTemperature();
      float h = dht.readHumidity();
      updateLCD(t, h);
    }
  }
}


// State Functions
  void idleHandling(){
    //Read ADC channel 0 (Water sensor)
    if(adc_read(0) < 60){
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
      //read ADC channel 0 (Water sensor pin)
      if(adc_read(0) > 60){
        transitionTo(IDLE);
      }
    }
  }

  void runningHandling(){
    //read ADC channel 0 (Water sensor pin)
    if(adc_read(0) < 60){
      transitionTo(ERROR);
      return;
    }

    float t = dht.readTemperature();
    if (t <= 24.0){
      transitionTo(IDLE);
    }
  }

// State Transition and Helper Functions
  void transitionTo(State newState){
    currentState = newState;
    
    //Write digial pin 22 (LED_YELLOW) LOW
    *port_a &= ~(0x01);
    //Write digial pin 23 (LED_GREEN) LOW
    *port_a &= ~(0x02);
    //Write digial pin 24 (LED_RED) LOW
    *port_a &= ~(0x04);
    //Write digial pin 25 (LED_BLUE) LOW
    *port_a &= ~(0x08);

    logEvent(newState);

    switch(newState){
      case DISABLED:
        //Write digial pin 22 (LED_YELLOW) HIGH
        *port_a |= 0x01;
        setFanMotor(false);
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Disabled");
        break;

      case IDLE:
        //Write digial pin 23 (LED_GREEN) HIGH
        *port_a |= 0x02;
        setFanMotor(false);
        updateLCD(dht.readTemperature(), dht.readHumidity());
        break;

      case ERROR:
        //Write digial pin 24 (LED_RED) HIGH
        *port_a |= 0x04;
        setFanMotor(false);
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Error: Low Water");
        break;

      case RUNNING:
        //Write digial pin 25 (LED_BLUE) HIGH
        *port_a |= 0x08;
        setFanMotor(true);
        break;
    }
  }

  void setFanMotor(bool on){
    if(on){
      //Write digial pin 8 (FAN_ENABLE_PIN) HIGH
      *port_h |= 0x20;
      //Write digial pin 7 (FAN_IN1_PIN) HIGH
      *port_h |= 0x10;
      //Write digial pin 6 (FAN_IN2_PIN) LOW
      *port_h &= ~(0x08);
    }
    else{
      //Write digial pin 8 (FAN_ENABLE_PIN) LOW
      *port_h &= ~(0x20);
      //Write digial pin 7 (FAN_IN1_PIN) LOW
      *port_h &= ~(0x10);
      //Write digial pin 6 (FAN_IN2_PIN) LOW
      *port_h &= ~(0x08);
    }
  }

  void ventControl(){
    //Read adc pin 1 (potentiometer pin)
    int potVal = adc_read(1);
    
    if(abs(potVal - previousPotValue) > 50){
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
  }

// Interrupt
  void startInterrupt(){
    startButtonPressed = true;
  }

  void U0init(int U0baud)
  {
    unsigned long FCPU = 16000000;
    unsigned int tbaud;
    tbaud = (FCPU / 16 / U0baud - 1);
    // Same as (FCPU / (16 * U0baud)) - 1;
    *myUCSR0A = 0x20;
    *myUCSR0B = 0x18;
    *myUCSR0C = 0x06;
    *myUBRR0  = tbaud;
  }

  //Serial IO functions
  unsigned char U0kbhit()
  {
    return *myUCSR0A & RDA;
  }

  unsigned char U0getchar()
  {
    return *myUDR0;
  }

  void U0putchar(unsigned char U0pdata)
  {
    while((*myUCSR0A & TBE)==0);
    *myUDR0 = U0pdata;
  }

  void U0putstring(unsigned char* str, int len){
    for(int i = 0; i < len; i++){
      U0putchar(str[i]);
    }
  }

  //Analog IO functions
  void adc_init()
  {
    // setup the A register
    // set bit 7 to 1 to enable the ADC
    *my_ADCSRA |= 0x80; 

    // clear bit 5 to 0 to disable the ADC trigger mode
    *my_ADCSRA &= 0xDF;

    // clear bit 3 to 0 to disable the ADC interrupt 
    *my_ADCSRA &= 0xF7;

    // clear bit 0-2 to 0 to set prescaler selection to slow reading
    *my_ADCSRA &= 0xF8;

    // setup the B register
    // clear bit 3 to 0 to reset the channel and gain bits
    *my_ADCSRB &= 0xF7;

    // clear bit 2-0 to 0 to set free running mode
    *my_ADCSRB &= 0xF8;

    // setup the MUX Register
    // clear bit 7 to 0 for AVCC analog reference
    *my_ADMUX &= 0x7F;
  

    // set bit 6 to 1 for AVCC analog reference
    *my_ADMUX |= 0x40;

    // clear bit 5 to 0 for right adjust result
    *my_ADMUX &= 0xDF;

    // clear bit 4-0 to 0 to reset the channel and gain bits
    *my_ADMUX &= 0xE0;
  }

  unsigned int adc_read(unsigned char adc_channel_num)
  {
    // clear the channel selection bits (MUX 4:0)
    *my_ADMUX &= 0xE0;

    // clear the channel selection bits (MUX 5) hint: it's not in the ADMUX register
    *my_ADCSRB &= 0xF7;

    // set the channel selection bits for channel 0
    *my_ADMUX |= (0x1F & adc_channel_num);
    *my_ADCSRB |= ((adc_channel_num & 0x20) >> 2);

    // set bit 6 of ADCSRA to 1 to start a conversion
    *my_ADCSRA |= 0x40;

    // wait for the conversion to complete
    while((*my_ADCSRA & 0x40) != 0);
    // return the result in the ADC data register and format the data based on right justification (check the lecture slide)

    unsigned int val = (*my_ADC_DATA & 0x03FF);
    return val;
}