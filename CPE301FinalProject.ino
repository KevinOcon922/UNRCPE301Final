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
* Pin 1 = Start Button
* Pin 2 = D4 LCD
* Pin 3 = D5 LCD
* Pin 4 = D6 LCD
* Pin 5 = D7 LCD
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

State currentState = DISABLED;

void setup() {
  U0init(9600);
  adc_init();
  //lcd.begin(16, 2);
}

void loop() {
  // put your main code here, to run repeatedly:

}

//Initializes Arduino for ADC conversion
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

//Read ADC data
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

//Initializes Arduino for serial IO
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

//Returns nonzero value if data is available to read on serial channel 0
unsigned char U0kbhit()
{
  return *myUCSR0A & RDA;
}

//Returns serial data
unsigned char U0getchar()
{
  return *myUDR0;
}

//Send a character to serial monitor
void U0putchar(unsigned char U0pdata)
{
  while((*myUCSR0A & TBE)==0);
  *myUDR0 = U0pdata;
}

//Send a string to serial monitor
void U0putstring(unsigned char* str, int len){
  for(int i = 0; i < len; i++){
    U0putchar(str[i]);
  }
}