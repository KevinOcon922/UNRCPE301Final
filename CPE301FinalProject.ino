//Kevin O'Connell, Vincent Toney
//CPE 301, Bashira Akter Anima
//Final Project

/*
* An evaporation cooling system
*/

/*
* Notes:
* Working with ADC channel 0 for water sensor
* Digital Pins Used:
* Pin 2 = D4 LCD
* Pin 3 = D5 LCD
* Pin 4 = D6 LCD
* Pin 5 = D7 LCD
* Pin 6 = Error reset button
* Pin 7 = Green LED
* Pin 8 = Yellow LED
* Pin 9 = Red LED
* Pin 10 = Start/Stop button
* Pin 11 = RS LCD
* Pin 12 = EN LCD
*/

#include <LiquidCrystal.h>

#define RDA 0x80
#define TBE 0x20

//Threshold for when water level is considered too low
#define WATER_THRESHOLD 250
enum State{
  DISABLED,
  IDLE,
  ERROR,
  RUNNING
};

//Digital pin addresses
volatile unsigned char* pin_b = (unsigned char*) 0x23;
volatile unsigned char* ddr_b = (unsigned char*) 0x24;
volatile unsigned char* port_b = (unsigned char*) 0x25;

volatile unsigned char* pin_h = (unsigned char*) 0x100;
volatile unsigned char* ddr_h = (unsigned char*) 0x101;
volatile unsigned char* port_h = (unsigned char*) 0x102;

//Serial data registers
volatile unsigned char *myUCSR0A = (unsigned char *)0x00C0;
volatile unsigned char *myUCSR0B = (unsigned char *)0x00C1;
volatile unsigned char *myUCSR0C = (unsigned char *)0x00C2;
volatile unsigned int  *myUBRR0  = (unsigned int *) 0x00C4;
volatile unsigned char *myUDR0   = (unsigned char *)0x00C6;

//ADC registers
volatile unsigned char* my_ADMUX = (unsigned char*) 0x7C;
volatile unsigned char* my_ADCSRB = (unsigned char*) 0x7B;
volatile unsigned char* my_ADCSRA = (unsigned char*) 0x7A;
volatile unsigned int* my_ADC_DATA = (unsigned int*) 0x78;

// LCD pins <--> Arduino pins
const int RS = 11, EN = 12, D4 = 2, D5 = 3, D6 = 4, D7 = 5;
LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);

State currentState;

void setup(){
  //setup digital pin 10 for input with pullup
  *ddr_b &= 0xEF;
  *port_b |= 0x10;
  //setup digital pin 6 for input with pullup
  *ddr_h &= 0xF7;
  *port_h |= 0x08;
  //setup digital pin 7 for output
  *ddr_h |= 0x10;
  //setup digital pin 8 for output
  *ddr_h |= 0x20;
  //setup digital pin 9 for output
  *ddr_h |= 0x40;

  currentState = DISABLED;

  U0init(9600);
  adc_init();
  //lcd.begin(16, 2);

  //attach ISR to start/stop button
  attachInterrupt(digitalPinToInterrupt(10), START_STOP_ISR, RISING);
}

void loop(){
  //All states, make sure to report state transitions via serial as well as changed to stepper motor position

  if(currentState != DISABLED){
    //Monitor humidity and temp and report to LCD once per minute
    //Check vent position controls
  }

  if(currentState == IDLE){
    unsigned int value = adc_read(0);
    if(value < WATER_THRESHOLD){
      //Switch to error, make sure to report state transition to serial
      //turn off motor if on
      //Display error message on LCD
      //enable red LED
      //     ***    Maybe make a transition_to_error function since this has to be done here and during running state
    }
  } else if(currentState == ERROR){
    //Check if a reset button is pressed, and if so, check if water is above threshold and 
    //if yes, switch back to IDLE
  } else if(currentState == RUNNING){
    //make sure fan motor is on when state is transitioned
    //monitor temperature and switch to IDLE when below threshold
    //monitor water and transition to error if low
    //make sure blue LED is on at state transition
  }
}

void START_STOP_ISR(){
  //Also turn off fan if on
  if(currentState != DISABLED){
    currentState = DISABLED;
    //enable yellow LED
    //also make sure to disable all other LEDS
  } else {
    currentState = IDLE;
    //enable green LED
  }
}

//Initializes Arduino for ADC conversion
void adc_init(){
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

//Read ADC data
unsigned int adc_read(unsigned char adc_channel_num){
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

//Initializes Arduino for serial IO
void U0init(int U0baud){
  unsigned long FCPU = 16000000;
  unsigned int tbaud;
  tbaud = (FCPU / 16 / U0baud - 1);
  // Same as (FCPU / (16 * U0baud)) - 1;
  *myUCSR0A = 0x20;
  *myUCSR0B = 0x18;
  *myUCSR0C = 0x06;
  *myUBRR0  = tbaud;
}

//Returns nonzero value if data is available to read on serial channel 0
unsigned char U0kbhit(){
  return *myUCSR0A & RDA;
}

//Returns serial data
unsigned char U0getchar(){
  return *myUDR0;
}

//Send a character to serial monitor
void U0putchar(unsigned char U0pdata){
  while((*myUCSR0A & TBE)==0);
  *myUDR0 = U0pdata;
}

//Send a string to serial monitor
void U0putstring(unsigned char* str, int len){
  for(int i = 0; i < len; i++){
    U0putchar(str[i]);
  }
}