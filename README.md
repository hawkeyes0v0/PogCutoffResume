# PogCutoffResume
Attiny412 low voltage cutoff, delayed resume

ok I think I have the firmware for the cutoff boards finished now. (maybe a bit of cleanup and commenting to do)

# Functions are:
- cutoff at minimum voltage
- resume at resume voltage
- max temperature cutoff (could flipflop a bit since theres no hysteresis, but you have bigger things to worry about at this point)
- periodic reset trigger on aux1 pin
- idle reset trigger on aux1 pin and listen for pinstate change on aux2 pin

# To use the configuration menu:
- One button press to enter
- when the button is released, the menu item and value in their array position are shown as long and short flashes.
- short press (< 1s) changes config value
- Long press (1s to 5s) changes the config item
- Longer press (5s or more) exits the config menu
- 30second timeout if button is not pressed again

# Arrays used to store config values:
int minimumVoltageArray[5] = {3400, 2900, 3100, 2800, 3200}; //minimum voltage to trigger cuttoff

int resumeVoltageArray[5] = {3600, 3300, 3700, 3900, 4100}; //minimum voltage to resume providing power to the node

int resetTriggerPeriodArray[5] = {7, 0, 14, 28, 3}; //reset trigger period in days. 0 = OFF. aux1 pin output

int maxTempCutoffArrray[5] = {60, 55, 50, 65, 0}; //min internal temp sensor value in Celsius to trigger cutoff. 0 = OFF

int IdleResetArray[5] = {0, 30, 60, -600, -1800}; //duration between pin status change on aux2 in seconds. reset positive, cutoff negative. 0 = OFF. aux1 pin output
