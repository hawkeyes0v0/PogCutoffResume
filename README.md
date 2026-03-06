# PogCutoffResume
Attiny412 low voltage cutoff, delayed resume

ok I think I have the firmware for the cutoff boards finished now.

# Properties:
- 1.8-5.5V operating voltage
- 2A max continuous current
- -40-105C temperature range
- 20x20mm board, holes centered at 15x15mm
- Source battery on VIN, node on VOUT
- <2mA during normal operation, 1uA during cutoff
- operating frequency: 1MHz
- VCC sampling frequency during normal operation: 300Hz (approximately)
- VCC sampling every 10 seconds during cutoff
- MCU flash space used: 4045/4096 Bytes (could be optimized significantly)

# Functions:
- cutoff at minimum voltage
- resume at resume voltage
- max temperature cutoff (could flipflop a bit since theres no hysteresis, but you have bigger things to worry about at this point)
- periodic reset
- idle reset, listen for pinstate change on aux2 pin
- single button menu to configure the board during runtime

# Testing Info:
So I really havent had time to do extensive testing, but I have tested the below:

- Temperature cutoff works when i point a hair dryer at it. I did not check how accurate the sensor is, but it shows 20C when at room temp, so thats good enough for me.
- Voltage cutoff is quick enough that the tiny little capacitor on VCC has enough power left to run the MCU for about a minute after cutoff.
- Resume voltage works.
- Idle timeout reset positive values work. havent tried negative values... dont have something to test it reliably right now.
- Periodic reset tested at 1000th scaled counters. works as far as i can see... 
- Voltage accuracy is to within a few mV (the voltage supply is not granular enough to test super accurately).
- Power consumption is about 1uA average when in cutoff mode. good enough. Under 2mA during normal operation.
- Menu enter, set, save, exit. All works, but sometimes the values stick during cutoff and are not applied until resume voltage is reached. I have no idea why yet.
- Output power is cut and resumed successfully in each scenario.

# To use the configuration menu:
- One button press to enter
- when the button is released, the menu item and value in their array position are shown as long and short flashes.
- short press (< 1s) changes config value
- Long press (1s to 5s) changes the config item
- Longer press (5s or more) exits the config menu
- 30second timeout if button is not pressed
- reset all configs to defaults by using the 6th menu option with a value of 5

# Config menu options
| Long Flash \ Short Flash|1 (default)|2|3|4|5|
|:-------------|:-------------:|:-------------:|:-------------:|:-------------:|:-------------:|
|1: Cutoff Voltage|3000|2900|3100|2800|3200|
|2: Resume Voltage|3600|3300|3700|3900|4100|
|3: Days Between Reset|7|0|14|28|3|
|4: Max Temperature Cutoff|60|65|50|70|0|
|5: Idle Time Reset|0|30|60|-600*|-1800*|
|6: Set Config to Defaults|0|0|0|0|1|

*reset on positive values, cutoff on negative values 

Arrays used to store config values:

int minimumVoltageArray[5] = {3000, 2900, 3100, 2800, 3200}; //minimum voltage to trigger cuttoff

int resumeVoltageArray[5] = {3600, 3300, 3700, 3900, 4100}; //minimum voltage to resume providing power to the node

int resetTriggerPeriodArray[5] = {7, 0, 14, 28, 3}; //reset trigger period in days. 0 = OFF. aux1 pin output

int maxTempCutoffArrray[5] = {60, 65, 50, 70, 0}; //min internal temp sensor value in Celsius to trigger cutoff. 0 = OFF

int IdleResetArray[5] = {0, 30, 60, -600, -1800}; //duration between pin status change on aux2 in seconds. reset positive, cutoff negative. 0 = OFF. aux1 pin output


![alt text](https://github.com/hawkeyes0v0/PogCutoffResume/blob/main/PCB/412a.png)

![alt text](https://github.com/hawkeyes0v0/PogCutoffResume/blob/main/PCB/412b.png)

![alt text](https://github.com/hawkeyes0v0/PogCutoffResume/blob/main/PCB/Screenshot%202026-03-04%20233930.png)

![alt text](https://github.com/hawkeyes0v0/PogCutoffResume/blob/main/PCB/Screenshot%202026-03-04%20234356.png)

![alt text](https://github.com/hawkeyes0v0/PogCutoffResume/blob/main/PCB/Screenshot%202026-03-04%20234455.png)

![alt text](https://github.com/hawkeyes0v0/PogCutoffResume/blob/main/PCB/e605dbf2ea734236a26d63ae861d4c91_T.png)
