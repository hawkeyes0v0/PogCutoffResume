#include <Arduino.h>
#include <megaTinyCore.h>
#include <avr/sleep.h>
#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#define RESULTCOUNT 4
int16_t results[RESULTCOUNT];
int32_t sum;
int16_t average;

const uint8_t updiPin = PIN_PA0;

const uint8_t heartbeatPin = PIN_PA6;
const uint8_t buttonPin = PIN_PA7;
const uint8_t outEnPin = PIN_PA2;

// const uint8_t auxPin0 = 0;                     //AUX pin
// const uint8_t auxPin1 = 1;                     //AUX pin

int ADCSettleDelay = 1;                           //Time in ms to wait before performing conversion to allow VRef to settle (minimum is around 50ns, 1ms should be more than enough)
int flashDelay = 10;
int tinyBlinkDelay = 50;
int shortBlinkDelay = 200;
int longBlinkDelay = 500;

volatile bool trigger = false;
volatile int buttonFlag = 0;
volatile unsigned long buttonStop;
volatile unsigned long buttonStart;
volatile unsigned long lastInteruptTrigger;
volatile unsigned long buttonStatus;
unsigned long  start = 0;
unsigned long  stop = 0;

int configValuesArray[7] = {0, 0, 0, 0, 0, 0, 0};
int minimumVoltageArray[5] = {3400, 2900, 3100, 2800, 3200}; //minimum voltage to trigger cuttoff
int resumeVoltageArray[5] = {3600, 3300, 3700, 3900, 4100}; //minimum voltage to resume providing power to the node
int resetTriggerPeriodArray[5] = {7, 0, 14, 28, 3}; //reset trigger period in days. 0 = OFF. aux1 pin output
int sleepDurationArray[5] = {3, 1, 10, 5, 20}; //sleep duration between sampling of VCC for voltage
int maxTempCutoffArrray[5] = {60, 55, 50, 45, 0}; //min internal temp sensor value in Celsius to trigger cutoff. 0 = OFF
int IdleResetArray[5] = {0, 30, 60, -600, -1800}; //duration between pin status change on aux2 in seconds. reset positive, cutoff negative. 0 = OFF. aux1 pin output



int minimumVoltage = minimumVoltageArray[0];                        //the target voltage to charge cap bank
int resumeVoltage = resumeVoltageArray[0];                         //the target voltage to charge cap bank
int resetTriggerPeriod = resetTriggerPeriodArray[0];              //reset trigger period in days. 0 = OFF. aux1 pin output
int sleepDuration = sleepDurationArray[0];                           //sleep duration in seconds between checks at low power
int maxTempCutoff = maxTempCutoffArrray[0];                       //min internal temp sensor value in Celsius to trigger cutoff. 0 = OFF
int IdleReset = IdleResetArray[0];                                 //duration between pin status change on aux2 in seconds. reset positive, cutoff negative. 0 = OFF. aux1 pin output


ISR(RTC_CNT_vect)
{
  // Clear flag by writing '1' 
  RTC.INTFLAGS = RTC_OVF_bm;
  // trigger = true ;
}

void buttonISR() {
  // detachInterrupt(digitalPinToInterrupt(buttonPin));
  if(millis() - lastInteruptTrigger >= 200){
    if(digitalRead(buttonPin)){
      buttonStop = millis();
    }else{
      buttonStart = millis();
    }
    buttonStatus = 1;
  }
  lastInteruptTrigger = millis();
}

uint16_t readSupplyVoltage() { // returns value in millivolts to avoid floating point
  analogReference(VDD);
  VREF.CTRLA = VREF_ADC0REFSEL_1V5_gc;
  // there is a settling time between when reference is turned on, and when it becomes valid.
  // since the reference is normally turned on only when it is requested, this virtually guarantees
  // that the first reading will be garbage; subsequent readings taken immediately after will be fine.
  // VREF.CTRLB|=VREF_ADC0REFEN_bm;
  // delay(10);
  uint16_t reading = analogRead(ADC_INTREF);
  delay(ADCSettleDelay);
  // Serial.print(reading);
  // Serial.println(" (discarded)");
  reading = analogRead(ADC_INTREF);
  // Serial.println(reading);
  uint32_t intermediate = 1023UL * 1500;
  reading = intermediate / reading;
  return reading;
}

uint16_t readTempExplained() {
  // based on the datasheet, in section 30.3.2.5 Temperature Measurement
  int8_t sigrow_offset = SIGROW.TEMPSENSE1; // Read signed value from signature row
  uint8_t sigrow_gain = SIGROW.TEMPSENSE0; // Read unsigned value from signature row
  analogReference(INTERNAL1V1);
  ADC0.SAMPCTRL = 0x1F; // maximum length sampling
  ADC0.CTRLD &= ~(ADC_INITDLY_gm);
  ADC0.CTRLD |= ADC_INITDLY_DLY32_gc; // wait 32 ADC clocks before reading new reference
  uint16_t adc_reading = analogRead(ADC_TEMPERATURE); // ADC conversion result with 1.1 V internal reference
  //Serial.println(adc_reading);
  analogReference(VDD);
  ADC0.SAMPCTRL = 0x0E; // 14, what we now set it to automatically on startup so we can run the ADC while keeping the same sampling time
  ADC0.CTRLD &= ~(ADC_INITDLY_gm);
  ADC0.CTRLD |= ADC_INITDLY_DLY16_gc;
  uint32_t temp = adc_reading - sigrow_offset;
  //Serial.println(temp);
  temp *= sigrow_gain; // Result might overflow 16 bit variable (10bit+8bit)
  //Serial.println(temp);
  temp += 0x80; // Add 1/2 to get correct rounding on division below
  temp >>= 8; // Divide result to get Kelvin
  return temp;
}

void configMenu() {
  unsigned long configStart = millis();
  int menuOption = 0;
  int configValuesArrayCount = sizeof(configValuesArray) / sizeof(configValuesArray[0]);
  stop = start;
  digitalWrite( heartbeatPin, LOW );
  while(buttonStatus == 0){}
  digitalWrite( heartbeatPin, HIGH );
  while(menuOption < configValuesArrayCount && millis() - configStart < 30000){
    while(digitalRead(buttonPin) == LOW){
      start = (int)buttonStart;
      stop = (int)buttonStop;
    }
    if(stop != (int)buttonStop && buttonStatus == 1){
      start = (int)buttonStart;
      stop = (int)buttonStop;
      buttonStatus = 0;
    }
    if(stop - start <= 1000 && stop - start >= 200){
      //increment menu option value
      configValuesArray[menuOption]++;
      if(configValuesArray[menuOption] > 4){configValuesArray[menuOption] = 0;}
      configStart = millis();
      stop = start;
    }
    if(stop - start < 6000 && stop - start > 1000){
      //cycle to next menu option
      configStart = millis();
      menuOption++;
      stop = start;
    }
    if(stop - start >= 10000){
      for ( int i = 0; i < 10; i++ ) {
        digitalWrite( heartbeatPin, LOW );
        delay(20);
        digitalWrite( heartbeatPin, HIGH );
        delay(50);
      }
      stop = start;
      break; //exit menu
    }
    
    delay(500);
    for ( int i = 0; i <= menuOption; i++ ) {
      digitalWrite( heartbeatPin, LOW );
      delay(200);
      digitalWrite( heartbeatPin, HIGH );
      delay(200);
    }
    delay(500);
    for ( int i = 0; i <= configValuesArray[menuOption]; i++ ) {
      digitalWrite( heartbeatPin, LOW );
      delay(50);
      digitalWrite( heartbeatPin, HIGH );
      delay(200);
    }
  }

  if(configValuesArray[6] == 4){
    for(int i = 0; i < configValuesArrayCount; i++){
      configValuesArray[i] = 0;
    }
    minimumVoltage = minimumVoltageArray[0];                        //the target voltage to charge cap bank
    resumeVoltage = resumeVoltageArray[0];                         //the target voltage to charge cap bank
    resetTriggerPeriod = resetTriggerPeriodArray[0];              //reset trigger period in days. 0 = OFF. aux1 pin output
    sleepDuration = sleepDurationArray[0];                           //sleep duration in seconds between checks at low power
    maxTempCutoff = maxTempCutoffArrray[0];                       //min internal temp sensor value in Celsius to trigger cutoff. 0 = OFF
    IdleReset = IdleResetArray[0];                                 //duration between pin status change on aux2 in seconds. reset positive, cutoff negative. 0 = OFF. aux1 pin output

  }else{
    minimumVoltage = minimumVoltageArray[configValuesArray[0]];                        //the target voltage to charge cap bank
    resumeVoltage = resumeVoltageArray[configValuesArray[1]];                         //the target voltage to charge cap bank
    resetTriggerPeriod = resetTriggerPeriodArray[configValuesArray[2]];              //reset trigger period in days. 0 = OFF. aux1 pin output
    sleepDuration = sleepDurationArray[configValuesArray[3]];                           //sleep duration in seconds between checks at low power
    maxTempCutoff = maxTempCutoffArrray[configValuesArray[4]];                       //min internal temp sensor value in Celsius to trigger cutoff. 0 = OFF
    IdleReset = IdleResetArray[configValuesArray[5]];                                 //duration between pin status change on aux2 in seconds. reset positive, cutoff negative. 0 = OFF. aux1 pin output

  }
  RTC.PER = sleepDuration;   // set period here in seconds based on 1024Hz RTC clock and prescaler /1024)
  stop = start;
}

void setup() {
  // Serial.begin(57600);
  // force all pins to non-floating state
  for ( int i = 0; i <= 4; i++ ) {
    pinMode( i, INPUT_PULLUP ) ;
  }
  pinMode(outEnPin, OUTPUT);
  digitalWrite(outEnPin, HIGH);
  pinMode(heartbeatPin, OUTPUT);
  digitalWrite(heartbeatPin, HIGH);

  // Attach interrupt on falling edge (button press to GND)
  attachInterrupt(digitalPinToInterrupt(buttonPin), buttonISR, CHANGE);
  pinMode(buttonPin, INPUT_PULLUP);

  // // test internal temp sensor. its not super accurate, but good enough for our purposes.
  // int16_t tempReading = readTempExplained();
  // // Serial.print("System temperature is: ");
  // // Serial.print(tempReading - 273);
  // // Serial.print("C (");
  // int celsius = tempReading - 273;
  // for ( int i = 0; i < celsius; i++ ) {
  //   digitalWrite( heartbeatPin, LOW ) ;
  //   delay(100) ;
  //   digitalWrite( heartbeatPin, HIGH ) ;
  //   delay(200) ;
  // }
  
  pinMode(buttonPin, INPUT_PULLUP);

  cli();
  while (RTC.STATUS > 0) {} // Wait for all register to be synchronized
  // Set RTC period
  RTC.PER = sleepDuration;   // set period here in seconds based on 1024Hz RTC clock and prescaler /1024)
  RTC.CLKSEL = RTC_CLKSEL_INT1K_gc; // 32768 Hz divided by 32, i.e run at 1.024kHz
  //  Run in debug: enabled
  RTC.DBGCTRL |= RTC_DBGRUN_bm;
  RTC.CTRLA = RTC_PRESCALER_DIV512_gc /* 256 */ | RTC_RTCEN_bm /* Enable: enabled */ | RTC_RUNSTDBY_bm; /* Run In Standby: enabled */
  // Enable Overflow Interrupt
  RTC.INTCTRL |= RTC_OVF_bm;
  sei();
  set_sleep_mode(SLEEP_MODE_STANDBY);
}

void loop() {
  while(readSupplyVoltage() >= minimumVoltage){
    digitalWrite(outEnPin, LOW);
    while(digitalRead(buttonPin) == LOW){
      start = (int)buttonStart;
      stop = (int)buttonStop;
    }
    if(stop != (int)buttonStop && buttonStatus == 1){
      start = (int)buttonStart;
      stop = (int)buttonStop;
      buttonStatus = 0;
    }
    if(stop - start >= 4000){
      configMenu();
    }
  }
  while(readSupplyVoltage() <= resumeVoltage){
    digitalWrite( heartbeatPin, LOW );
    delay(flashDelay);
    digitalWrite( heartbeatPin, HIGH );
    while(digitalRead(buttonPin) == LOW){
      start = (int)buttonStart;
      stop = (int)buttonStop;
    }
    if(stop != (int)buttonStop && buttonStatus == 1){
      start = (int)buttonStart;
      stop = (int)buttonStop;
      buttonStatus = 0;
    }
    if(stop - start >= 500){
      configMenu();
    }
    // set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_mode();
  }
}
