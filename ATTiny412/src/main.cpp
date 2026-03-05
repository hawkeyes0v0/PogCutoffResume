#include <Arduino.h>
#include <megaTinyCore.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>

const uint8_t updiPin = PIN_PA0;

const uint8_t LEDPin = PIN_PA6;
const uint8_t buttonPin = PIN_PA7;
const uint8_t outEnPin = PIN_PA2;

const uint8_t auxPin1 = PIN_PA1;                     //AUX1 pin
const uint8_t sensePin = PIN_PA3;                     //AUX2 pin

int ADCSettleDelay = 1;                           //Time in ms to wait before performing conversion to allow VRef to settle (minimum is around 50ns, 1ms should be more than enough)

int flashDelay = 20;
int shortBlinkDelay = 200;
int longBlinkDelay = 500;

int sleepDuration = 10;                                            //sleep duration in seconds between checks at low power
int dayCount = 0;

unsigned long timeIdle;
unsigned long timeReset;

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
}

void buttonISR() {} //only used to wake from sleep

void setBlink(){
  cli();
  while (RTC.STATUS > 0) {} // Wait for all register to be synchronized
  // Set RTC period
  RTC.PER = sleepDuration;   // set period here in seconds based on 1024Hz RTC clock and prescaler /1024)
  RTC.CLKSEL = RTC_CLKSEL_INT1K_gc; // 32768 Hz divided by 32, i.e run at 1.024kHz
  //  Run in debug: enabled
  // RTC.DBGCTRL |= RTC_DBGRUN_bm;
  RTC.CTRLA = RTC_PRESCALER_DIV1024_gc /* 256 */ | RTC_RTCEN_bm /* Enable: enabled */ | RTC_RUNSTDBY_bm; /* Run In Standby: enabled */
  // Enable Overflow Interrupt
  RTC.INTCTRL |= RTC_OVF_bm;
  sei();
  set_sleep_mode(SLEEP_MODE_STANDBY);
}

uint16_t readSupplyVoltage() { // returns value in millivolts to avoid floating point
  analogReference(VDD);
  VREF.CTRLA = VREF_ADC0REFSEL_1V1_gc;
  // there is a settling time between when reference is turned on, and when it becomes valid.
  // since the reference is normally turned on only when it is requested, this virtually guarantees
  // that the first reading will be garbage; subsequent readings taken immediately after will be fine.
  // VREF.CTRLB|=VREF_ADC0REFEN_bm;
  // delay(10);
  uint16_t reading = analogRead(ADC_INTREF);
  delay(ADCSettleDelay);
  reading = analogRead(ADC_INTREF);
  uint32_t intermediate = 1023UL * 1100;
  reading = intermediate / reading;
  return reading;
}

int readTemp() {
  // based on the datasheet, in section 30.3.2.5 Temperature Measurement
  int8_t sigrow_offset = SIGROW.TEMPSENSE1;                           // Read signed value from signature row
  uint8_t sigrow_gain = SIGROW.TEMPSENSE0;                            // Read unsigned value from signature row
  analogReference(INTERNAL1V1);
  ADC0.SAMPCTRL = 0x1F;                                               // maximum length sampling
  ADC0.CTRLD &= ~(ADC_INITDLY_gm);
  ADC0.CTRLD |= ADC_INITDLY_DLY32_gc;                                 // wait 32 ADC clocks before reading new reference
  uint16_t adc_reading = analogRead(ADC_TEMPERATURE);                 // ADC conversion result with 1.1 V internal reference
  analogReference(VDD);
  ADC0.SAMPCTRL = 0x0E;                          // 14, what we now set it to automatically on startup so we can run the ADC while keeping the same sampling time
  ADC0.CTRLD &= ~(ADC_INITDLY_gm);
  ADC0.CTRLD |= ADC_INITDLY_DLY16_gc;
  uint32_t temp = adc_reading - sigrow_offset;
  temp *= sigrow_gain;                                                // Result might overflow 16 bit variable (10bit+8bit)
  temp += 0x80;                                                       // Add 1/2 to get correct rounding on division below
  temp >>= 8;                                                         // Divide result to get Kelvin
  temp -= 273;                                                        // subtract 273 for Celsius
  return temp;
}

void menuDisplay(int menuOptionTemp) {                                //shows the current menu selection and the value position in the array.
  delay(500);
  for ( int i = 0; i <= menuOptionTemp; i++ ) {
    digitalWrite(LEDPin, LOW);
    delay(shortBlinkDelay);
    digitalWrite(LEDPin, HIGH);
    delay(shortBlinkDelay);
  }
  delay(shortBlinkDelay);
  for ( int i = 0; i <= configValuesArray[menuOptionTemp]; i++ ) {
    digitalWrite(LEDPin, LOW);
    delay(flashDelay);
    digitalWrite(LEDPin, HIGH);
    delay(shortBlinkDelay);
  }
}

void configMenu() {
  while(digitalRead(buttonPin) == false){digitalWrite(LEDPin, LOW);}
  digitalWrite(LEDPin, HIGH);
  unsigned long configStart = millis();
  unsigned long buttonTime = 0;
  int buttonPressed = 0;
  int menuOption = 0;
  int configValuesArrayCount = sizeof(configValuesArray) / sizeof(configValuesArray[0]);
  menuDisplay(menuOption);
  while(menuOption < configValuesArrayCount && millis() - configStart < 30000){ //exit menu if 30s has passed without a button press or if cycled past the last menu option
    buttonTime = 0;
    buttonPressed = 0;
    while(buttonPressed == 0 && millis() - configStart < 30000){  //hold here until button is pressed or 30s passes. 
      if(digitalRead(buttonPin) == false){
        unsigned long buttonStart = millis();
        digitalWrite(LEDPin, LOW);
        while(digitalRead(buttonPin) == false){}                  //hold while pressed
        digitalWrite(LEDPin, HIGH);
        buttonTime = millis() - buttonStart;
        if(buttonTime >= 100){                                    //debounce check
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
        if(configValuesArray[menuOption] > 4){configValuesArray[menuOption] = 0;} //roll the option back to 1 if past the end of the values
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

  }else{                                                          //write new values to variables using menu selection
    minimumVoltage = minimumVoltageArray[configValuesArray[0]];                        //the target voltage to charge cap bank
    resumeVoltage = resumeVoltageArray[configValuesArray[1]];                         //the target voltage to charge cap bank
    resetTriggerPeriod = resetTriggerPeriodArray[configValuesArray[2]];              //reset trigger period in days. 0 = OFF. aux1 pin output
    maxTempCutoff = maxTempCutoffArrray[configValuesArray[3]];                       //min internal temp sensor value in Celsius to trigger cutoff. 0 = OFF
    IdleReset = IdleResetArray[configValuesArray[4]];                                 //duration between pin status change on aux2 in seconds. reset positive, cutoff negative. 0 = OFF. aux1 pin output
  }
}

void setup() {
  // force all pins to non-floating state
  for ( int i = 0; i <= 4; i++ ) {
    pinMode( i, INPUT_PULLUP ) ;
  }
  pinMode(outEnPin, OUTPUT);
  digitalWrite(outEnPin, HIGH);
  pinMode(LEDPin, OUTPUT);
  digitalWrite(LEDPin, HIGH);
  pinMode(buttonPin, INPUT_PULLUP);

  setBlink();                                                   //set RTC to wake from sleep periodically
}

void loop() {
  timeIdle = millis();
  timeReset = millis();
  while(readSupplyVoltage() >= minimumVoltage){                 //check voltage is above cutoff. can start below resume voltage.
    if(readTemp() <= maxTempCutoff){
      digitalWrite(outEnPin, LOW);
    }else{
      digitalWrite(outEnPin, HIGH);
      while(readTemp() > maxTempCutoff){
        digitalWrite(LEDPin, LOW);
        delay(longBlinkDelay);
        digitalWrite(LEDPin, HIGH);
        delay(longBlinkDelay);
      }
    }
    if(digitalRead(buttonPin) == false){                       //enter menu if button is pressed.
      configMenu();
    }
    
    if(resetTriggerPeriod != 0){
      if(millis() - timeReset >= 86400000){                       //increments every 24 hours
        dayCount++;
        timeReset = millis();
        if(dayCount >= resetTriggerPeriod){                       //reset node if the period has been reached.
          digitalWrite(outEnPin, !digitalRead(outEnPin));
          delay(500);
          digitalWrite(outEnPin, !digitalRead(outEnPin));
          dayCount = 0;
        }
      }
    }
    if(IdleReset != 0){                                         //check if idle reset is enabled. 
      if(digitalRead(sensePin)){                                //check aux2 pin many times a second to see if pin state has changed to LOW.
        timeIdle = millis();
      }else{
        if(IdleReset > 0){                                     //if the value is negative, reset the board if the pinstate HAS changed.
          if(millis() - timeIdle >= (IdleReset * 1000)){       //reset the board if the pin state has NOT changed in the given period.
            digitalWrite(outEnPin, !digitalRead(outEnPin));
            delay(500);
            digitalWrite(outEnPin, !digitalRead(outEnPin));
            timeIdle = millis();
          }
        }else{
          while(millis() - timeIdle <= (IdleReset * -1000)){          // ONLY USE NEGATIVE VALUES FOR EXTERNAL TRIGGERS
            digitalWrite(outEnPin, HIGH);
            attachInterrupt(digitalPinToInterrupt(sensePin), buttonISR, CHANGE); // Attach interrupt on falling edge (button press to GND)
            sleep_mode();                                                         // enter standby sleep mode
            detachInterrupt(digitalPinToInterrupt(sensePin));
            pinMode(sensePin, INPUT_PULLUP);
            if(digitalRead(sensePin)){                                //check aux2 pin many times a second to see if pin state has changed to LOW.
              timeIdle = millis();
            }
          }
        }
      }
    }
    digitalWrite(LEDPin, !digitalRead(LEDPin));
  }
  digitalWrite(LEDPin, HIGH);
  digitalWrite(outEnPin, HIGH);
  while(readSupplyVoltage() <= resumeVoltage){                            //minimum voltage ahs triggered cutoff. waiting to hit resume voltage
    attachInterrupt(digitalPinToInterrupt(buttonPin), buttonISR, CHANGE); // Attach interrupt on falling edge (button press to GND)
    sleep_mode();                                                         // enter standby sleep mode
    detachInterrupt(digitalPinToInterrupt(buttonPin));
    pinMode(buttonPin, INPUT_PULLUP);
    if(digitalRead(buttonPin) == false){                                  //if woken by button press, enter menu
      configMenu();
    }
    digitalWrite(LEDPin, LOW);
    delay(10);
    digitalWrite(LEDPin, HIGH);
  }
  detachInterrupt(digitalPinToInterrupt(buttonPin));                      //disable pin interrupt when cutoff is disabled.
  pinMode(buttonPin, INPUT_PULLUP);
}
