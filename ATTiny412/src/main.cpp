#include <Arduino.h>
#include <megaTinyCore.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>

const uint8_t updiPin = PIN_PA0;

const uint8_t heartbeatPin = PIN_PA6;
const uint8_t buttonPin = PIN_PA7;
const uint8_t outEnPin = PIN_PA2;

const uint8_t auxPin1 = PIN_PA1;                     //AUX1 pin
const uint8_t sensePin = PIN_PA3;                     //AUX2 pin

int ADCSettleDelay = 1;                           //Time in ms to wait before performing conversion to allow VRef to settle (minimum is around 50ns, 1ms should be more than enough)

int flashDelay = 20;
int shortBlinkDelay = 200;
int longBlinkDelay = 500;

int sleepDuration = 5;                                            //sleep duration in seconds between checks at low power
int dayCount = 0;

unsigned long timeIdle;
unsigned long timeReset;

// volatile bool trigger = false;
volatile unsigned long lastInteruptTrigger;

int configValuesArray[5] = {0, 0, 0, 0, 0};
int minimumVoltageArray[5] = {3000, 2900, 3100, 2800, 3200}; //minimum voltage to trigger cuttoff
int resumeVoltageArray[5] = {3600, 3300, 3700, 3900, 4100}; //minimum voltage to resume providing power to the node
int resetTriggerPeriodArray[5] = {7, 0, 14, 28, 3}; //reset trigger period in days. 0 = OFF. aux1 pin output
int maxTempCutoffArrray[5] = {60, 65, 50, 70, 0}; //min internal temp sensor value in Celsius to trigger cutoff. 0 = OFF
int IdleResetArray[5] = {0, 30, 60, -600, -1800}; //duration between pin status change on aux2 in seconds. reset positive, cutoff negative. 0 = OFF. aux1 pin output

int minimumVoltage = minimumVoltageArray[0];                        //the target voltage to charge cap bank
int resumeVoltage = resumeVoltageArray[0];                         //the target voltage to charge cap bank
int resetTriggerPeriod = resetTriggerPeriodArray[0];              //reset trigger period in days. 0 = OFF. aux1 pin output
int maxTempCutoff = maxTempCutoffArrray[0];                       //min internal temp sensor value in Celsius to trigger cutoff. 0 = OFF
int IdleReset = IdleResetArray[0];                                 //duration between pin status change on aux2 in seconds. reset positive, cutoff negative. 0 = OFF. aux1 pin output

ISR(RTC_CNT_vect)
{
  // Clear flag by writing '1' 
  RTC.INTFLAGS = RTC_OVF_bm;
  // trigger = true ;
}

void buttonISR() {
  if(millis() - lastInteruptTrigger >= 300){
    detachInterrupt(digitalPinToInterrupt(buttonPin));
  }
  lastInteruptTrigger = millis();
}

void setSleep(){
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
  reading = analogRead(ADC_INTREF);
  uint32_t intermediate = 1023UL * 1500;
  reading = intermediate / reading;
  return reading;
}

int readTemp() {
  // based on the datasheet, in section 30.3.2.5 Temperature Measurement
  int8_t sigrow_offset = SIGROW.TEMPSENSE1; // Read signed value from signature row
  uint8_t sigrow_gain = SIGROW.TEMPSENSE0; // Read unsigned value from signature row
  analogReference(INTERNAL1V1);
  ADC0.SAMPCTRL = 0x1F; // maximum length sampling
  ADC0.CTRLD &= ~(ADC_INITDLY_gm);
  ADC0.CTRLD |= ADC_INITDLY_DLY32_gc; // wait 32 ADC clocks before reading new reference
  uint16_t adc_reading = analogRead(ADC_TEMPERATURE); // ADC conversion result with 1.1 V internal reference
  analogReference(VDD);
  ADC0.SAMPCTRL = 0x0E; // 14, what we now set it to automatically on startup so we can run the ADC while keeping the same sampling time
  ADC0.CTRLD &= ~(ADC_INITDLY_gm);
  ADC0.CTRLD |= ADC_INITDLY_DLY16_gc;
  uint32_t temp = adc_reading - sigrow_offset;
  temp *= sigrow_gain; // Result might overflow 16 bit variable (10bit+8bit)
  temp += 0x80; // Add 1/2 to get correct rounding on division below
  temp >>= 8; // Divide result to get Kelvin
  temp -= 273; // subtract 273 for Celsius
  return temp;
}

void menuDisplay(int menuOptionTemp) {
  delay(500);
  for ( int i = 0; i <= menuOptionTemp; i++ ) {
    digitalWrite(heartbeatPin, LOW);
    delay(shortBlinkDelay);
    digitalWrite(heartbeatPin, HIGH);
    delay(shortBlinkDelay);
  }
  delay(shortBlinkDelay);
  for ( int i = 0; i <= configValuesArray[menuOptionTemp]; i++ ) {
    digitalWrite(heartbeatPin, LOW);
    delay(flashDelay);
    digitalWrite(heartbeatPin, HIGH);
    delay(shortBlinkDelay);
  }
}

void configMenu() {
  while(digitalRead(buttonPin) == false){digitalWrite(heartbeatPin, LOW);}
  digitalWrite(heartbeatPin, HIGH);
  unsigned long configStart = millis();
  unsigned long buttonTime = 0;
  int buttonPressed = 0;
  int menuOption = 0;
  int configValuesArrayCount = sizeof(configValuesArray) / sizeof(configValuesArray[0]);
  menuDisplay(menuOption);
  while(menuOption < configValuesArrayCount && millis() - configStart < 30000){
    buttonTime = 0;
    buttonPressed = 0;
    while(buttonPressed == 0){
      if(digitalRead(buttonPin) == false){
        unsigned long buttonStart = millis();
        digitalWrite(heartbeatPin, LOW);
        while(digitalRead(buttonPin) == false){}
        digitalWrite(heartbeatPin, HIGH);
        buttonTime = millis() - buttonStart;
        if(buttonTime >= 100){
          buttonPressed = 1;
        }else{
          buttonTime = 0;
        }
      }
    }
    if(buttonPressed == 1){
      configStart = millis();
      if(buttonTime < 1000){
        //increment menu option value
        configValuesArray[menuOption]++;
        if(configValuesArray[menuOption] > 4){configValuesArray[menuOption] = 0;}
        menuDisplay(menuOption);
      }else{
        if(buttonTime >= 1000 && buttonTime <= 5000){
          //cycle to next menu option
          menuOption++;
          menuDisplay(menuOption);
        }else{
          break;
        }
      }
    }
  }

  if(configValuesArray[6] == 4){
    for(int i = 0; i < configValuesArrayCount; i++){
      configValuesArray[i] = 0;
    }
    minimumVoltage = minimumVoltageArray[0];                        //the target voltage to charge cap bank
    resumeVoltage = resumeVoltageArray[0];                         //the target voltage to charge cap bank
    resetTriggerPeriod = resetTriggerPeriodArray[0];              //reset trigger period in days. 0 = OFF. aux1 pin output
    maxTempCutoff = maxTempCutoffArrray[0];                       //min internal temp sensor value in Celsius to trigger cutoff. 0 = OFF
    IdleReset = IdleResetArray[0];                                 //duration between pin status change on aux2 in seconds. reset positive, cutoff negative. 0 = OFF. aux1 pin output

  }else{
    minimumVoltage = minimumVoltageArray[configValuesArray[0]];                        //the target voltage to charge cap bank
    resumeVoltage = resumeVoltageArray[configValuesArray[1]];                         //the target voltage to charge cap bank
    resetTriggerPeriod = resetTriggerPeriodArray[configValuesArray[2]];              //reset trigger period in days. 0 = OFF. aux1 pin output
    maxTempCutoff = maxTempCutoffArrray[configValuesArray[3]];                       //min internal temp sensor value in Celsius to trigger cutoff. 0 = OFF
    IdleReset = IdleResetArray[configValuesArray[4]];                                 //duration between pin status change on aux2 in seconds. reset positive, cutoff negative. 0 = OFF. aux1 pin output
  }
}

void setup() {
  // Serial.begin(57600);
  // force all pins to non-floating state
  for ( int i = 0; i <= 4; i++ ) {
    pinMode( i, INPUT_PULLUP ) ;
  }
  pinMode(outEnPin, OUTPUT);
  digitalWrite(outEnPin, HIGH);
  pinMode(auxPin1, OUTPUT);
  digitalWrite(auxPin1, HIGH);
  pinMode(heartbeatPin, OUTPUT);
  digitalWrite(heartbeatPin, HIGH);
  pinMode(buttonPin, INPUT_PULLUP);

  // // test internal temp sensor. its not super accurate, but good enough for our purposes.
  // int16_t tempReading = readTempExplained();
  // // Serial.print("System temperature is: ");
  // // Serial.print(tempReading - 273);
  // // Serial.print("C (");
  // int celsius = tempReading - 273;
  // for ( int i = 0; i < celsius; i++ ) {
  //   digitalWrite(heartbeatPin, LOW) ;
  //   delay(100) ;
  //   digitalWrite(heartbeatPin, HIGH) ;
  //   delay(200) ;
  // }

  setSleep();
}

void loop() {
  timeIdle = millis();
  timeReset = millis();
  while(readSupplyVoltage() >= minimumVoltage){
    if(readTemp() <= maxTempCutoff){
      digitalWrite(outEnPin, LOW);
    }else{
      digitalWrite(outEnPin, HIGH);
      while(readTemp() > maxTempCutoff){
        digitalWrite(heartbeatPin, LOW);
        delay(longBlinkDelay);
        digitalWrite(heartbeatPin, HIGH);
        delay(longBlinkDelay);
      }
    }

    if(digitalRead(buttonPin) == false){
      configMenu();
    }

    if(IdleReset != 0){
      if(digitalRead(sensePin)){
        timeIdle = millis();
      }else{
        if(IdleReset < 0){
          if(millis() - timeIdle <= (IdleReset * 1000)){
            pinMode(auxPin1, OUTPUT);
            digitalWrite(auxPin1, LOW);
            delay(500);
            pinMode(auxPin1, INPUT_PULLUP);
            timeIdle = millis();
          }
        }else{
          if(millis() - timeIdle >= (IdleReset * 1000)){
            pinMode(auxPin1, OUTPUT);
            digitalWrite(auxPin1, LOW);
            delay(500);
            pinMode(auxPin1, INPUT_PULLUP);
            timeIdle = millis();
          }
        }
      }
    }

    if(resetTriggerPeriod != 0){
      if(millis() - timeReset >= 86400000){ //increments every 24 hours
        dayCount++;
        timeReset = millis();
        if(dayCount >= resetTriggerPeriod){
          pinMode(auxPin1, OUTPUT);
          digitalWrite(auxPin1, LOW);
          delay(500);
          pinMode(auxPin1, INPUT_PULLUP);
          dayCount = 0;
        }
      }
    }
  }

  while(readSupplyVoltage() <= resumeVoltage){ //minimum voltage ahs triggered cutoff. waiting to hit resume voltage
    digitalWrite(heartbeatPin, LOW);
    delay(flashDelay);
    digitalWrite(heartbeatPin, HIGH);
    attachInterrupt(digitalPinToInterrupt(buttonPin), buttonISR, CHANGE); // Attach interrupt on falling edge (button press to GND)
    sleep_mode();
    if(digitalRead(buttonPin) == false){
      configMenu();
    }
  }
  detachInterrupt(digitalPinToInterrupt(buttonPin));
}
