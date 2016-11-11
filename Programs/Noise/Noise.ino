/*
//********************
//*    miniMO Noise  *
//* 2016 by enveloop *
//********************

  Uses the xorshift pseudorandom number generator,
        as described Here:
http://www.arklyffe.com/main/2010/08/29/xorshift-pseudorandom-number-generator/

//
   http://www.envelooponline.com/minimo
   CC BY 4.0
   Licensed under a Creative Commons Attribution 4.0 International license:
   http://creativecommons.org/licenses/by/4.0/
//

I/O
  Outputs: noise/grains
  Input 1: frequency/grain density modulation
  Input 2: amplitude modulation
  
OPERATION
  Knob: change frequency (default) or grain density
    -miniMO waits until you reach the value it has currently stored 
  Click: toggle between frequency and density control 
    -The LED blinks once 
        
BATTERY CHECK
  When you switch the module ON,
    -If the LED blinks once, the battery is OK
    -If the LED blinks fast several times, the battery is running low
*/


//#include <avr/eeprom.h>
#include <avr/io.h>
#include <util/delay.h>

/*
//calibration
int sensorValue = 0;
int sensorMax;
int sensorMin;
bool calibrating = false;
*/

//button interrupt
volatile bool inputButtonValue;
bool control = 1;

//random number generator
static unsigned long y32 = 1; //pattern length: 32 bit 

//freq control reference
bool coarseFreqChange = false;  
byte potPosFreqRef = 255;      //max

//grain control reference
bool coarseGrainChange = false;
byte potPosGrainRef = 255;          //max

//volume input smoothing
const int numReadings = 4;
int readings[numReadings];      // the readings from the analog input
int readIndex = 0;              // the index of the current reading
int total = 0;                  // the running total
byte volumeModulation = 255;

void setup() {
  //disable USI to save power as we are not using it
  PRR = 1<<PRUSI;
  
  //set LED pin and check the battery level
  pinMode(0, OUTPUT); //LED
  checkVoltage();
  ADMUX = 0;                      //reset multiplexer settings
  
  /*
  //read calibrated values for freq input
  sensorMin = eeprom_read_word((uint16_t*)1);
  sensorMax = eeprom_read_word((uint16_t*)3);
  if (sensorMax == 0) sensorMax = 255; //if there was no data in memory, give it the default value
  */
  
  //set the rest of the pins
  pinMode(4, OUTPUT); //timer 1 in digital output 4 - outs 1 and 2
  pinMode(3, INPUT);  //analog- freq input (knob plus external input 1)
  pinMode(2, INPUT);  //analog- amplitude input (external input 2)
  pinMode(1, INPUT);  //digital input (push button)

  //disable digital input in pins that do analog conversion
  DIDR0 = (1 << ADC1D) | (1 << ADC3D); //PB2,PB3
  
  //set clock source for PWM -datasheet p94
  PLLCSR |= (1 << PLLE);               // Enable PLL (64 MHz)
  _delay_us(100);                      // Wait for a steady state
  while (!(PLLCSR & (1 << PLOCK)));    // Ensure PLL lock
  PLLCSR |= (1 << PCKE);               // Enable PLL as clock source for timer 1

  TIMSK  = 0;                          // Timer interrupts OFF

  //PWM Generation -timer 1
  GTCCR  = (1 << PWM1B) | (1 << COM1B1); // PWM, output on pb1, compare with OCR1B (see interrupt below), reset on match with OCR1C
  OCR1C  = 0xff;
  TCCR1  = (1 << CS10);                // no prescale
  
  //Timer Interrupt Generation -timer 0                                                          
  TCCR0A = (1<<WGM01);                 //Clear Timer on Compare (CTC) with OCR0A
  TCCR0B = (1<<CS01) ;                 // prescale by 8
  OCR0A = 0;                           //frequencies down to 3921hz for a value of 255 https://www.easycalculation.com/engineering/electrical/avr-timer-calculator.php
  TIMSK = (1 << OCIE0A);               // Enable Interrupt on compare with OCR0A

  //Pin interrupt Generation
  GIMSK |= (1 << PCIE);                // Enable Pin Change Interrupt
  PCMSK |= (1 << PCINT1);              // on pin 1

  sei();                               // Timer interrupts ON

  //go for it!
  digitalWrite(0, HIGH);               // turn LED ON
  
  //calibrate();

}

ISR(TIMER0_COMPA_vect) {               //Timer 0 interruption - changes the width of timer 1's pulse to generate waves

  OCR1B = (xorshift32() * volumeModulation) >> 8 ;
  
}

ISR(PCINT0_vect) {                       //PIN Interruption - has priority over COMPA; this ensures that the switch will work
  inputButtonValue = digitalRead(1);
  digitalWrite(0, !inputButtonValue);    //Turn LED off while pressing the button
  if (inputButtonValue) {
    control = !control;
  }
}

void loop() {
  
  if (control == true)setFrequency(3) ;
  else setGrainDensity(3);
  
  readExtInput(1);                                 //read analog input 1 (attiny PB2)

}

byte xorshift32(void) {
    y32 ^= (y32 << 7);
    y32 ^= (y32 >> 5);
    return y32 ^= (y32 << 3);  //pattern values: 8 bit
}

//Parameters don't change until we return to the value they had last time we changed them.
//we store the knob's position in a variable and check the current position against it;
//when we reach it, we start controlling the parameter again.

void setGrainDensity(int pin) { 
  coarseFreqChange = false;                         //reset the control condition for frequency
  if (coarseGrainChange == false) {
    byte coarseGrainRead = analogRead(pin) >> 2; 
    if (coarseGrainRead == potPosGrainRef) {
      coarseGrainChange = true;
    }
  }
  if (coarseGrainChange == true) {
    int tempRead = analogRead(pin);
    byte densityRead = tempRead >> 2;                     //right shifting by 2 to get values between 0 and 255 (0-1023/2^2)
    potPosGrainRef =  densityRead;                    //save the knob´s position for reference.
    OCR1C = densityRead;
    //OCR1C = map(densityRead, sensorMin, sensorMax, 0, 255);
  }
}

void setFrequency(int pin) {
  coarseGrainChange = false;                            //reset the control condition for density
  if (coarseFreqChange == false) {
    byte coarsefreqRead = analogRead(pin) >> 2;
    if (coarsefreqRead == potPosFreqRef) {
      coarseFreqChange = true;
    }
  }
  if (coarseFreqChange == true) {
    int tempRead = analogRead(pin);
    byte freqRead = tempRead >> 2;
    potPosFreqRef = freqRead;
    OCR0A = 255 - freqRead;                          //reversing values so that the knob affects it in the same way as the other parameters
    //OCR0A = 255 -  (map(freqRead, sensorMin, sensorMax, 0, 255));                            
  }
}

int readExtInput(const byte pin) {  //with averaging
  total = total - readings[readIndex];
  readings[readIndex] = analogRead(pin) >> 2; //right shifting by 2 to get values between 0 and 255 (0-1023/2^2)
  total = total + readings[readIndex];
  readIndex = readIndex + 1;
  if (readIndex >= numReadings) readIndex = 0;
  volumeModulation = total / numReadings;
}

void checkVoltage() { //voltage from 255 to 0; 46 is (approx)5v, 94 is 2.8, 104-106 is 2.5

  ADMUX |= (1 << ADLAR);                //Left adjust result (8 bit conversion stored in ADCH)
  ADMUX |= (1 << MUX3) | (1 << MUX2);   //1.1v input
  delay(250);                           // Wait for Vref to settle
  ADCSRA |= (1 << ADSC);                // Start conversion
  while (bit_is_set(ADCSRA, ADSC));     // wait while measuring
  if (ADCH > 103)                       //approx 2.6
    flashLED(8, 100);
  else
    flashLED(1, 250);
}

void flashLED (int times, int gap) {     //for voltage check only (uses regular delay)
  for (int i = 0; i < times; i++)
  {
    digitalWrite(0, HIGH);
    delay(gap);
    digitalWrite(0, LOW);
    delay(gap);
  }
}

/*
void calibrate() {
  digitalWrite(0, LOW); 
  calibrating = true;
  TIMSK = (0 << OCIE0A);
  int firstRead = analogRead(3);
  int delta = 0;
  int sensorValue = 0;
  sensorMax = 0;                              //reset the max value
  sensorMin = 1023;                           //reset the min value
  int noNewMinOrMax = 0;
  while ( delta < 5) {                       //do nothing until there is "movement"
    sensorValue = analogRead(3);
    delta = abs(sensorValue - firstRead);
  }
  while (noNewMinOrMax < 200) {
    sensorValue = analogRead(3);
    if (sensorValue > sensorMax)sensorMax = sensorValue;       //input bigger than last - set new max
    else if (sensorValue < sensorMin)sensorMin = sensorValue;  //input smaller than last - set new min
    else noNewMinOrMax ++;                                  //input between min and max - nothing new
    _delay_ms(10);
  }
  eeprom_update_word((uint16_t*)1, sensorMin);
  eeprom_update_word((uint16_t*)3, sensorMax);
  calibrating = false;
  digitalWrite(0, HIGH); 
  TIMSK = (1 << OCIE0A);
}*/
