#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Watchdog.h>

Watchdog watchdog;  // bring watchdog class object in

// section good for cross platform work
//#if defined(ARDUINO) && ARDUINO >= 100 this if is enabled for Arduino UNO
//#define printByte(args)  write(args); // keep if wanting to use lcd.printByte instead of lcd.write
//#else
//#define printByte(args)  print(args,BYTE);
//#endif

// pin good placement
// maybe switch closer to OR gate and run wire from NOT switch to clip near pin 2
#define upLevel 3         // raise the number of cm to trigger (trigger with lower water level)
#define downLevel 4       // lower the number of cm to trigger (trigger with higher water level)
#define echo 7            // input signal from ultrasonic sensor
#define trig 8            // output pulses for ultrasonic sensor
#define alarm 11          // pin for alarm to sound (go to 555 flip flop configuration for TRIGGER pin)
#define runPumpRelay 12   // output signal pin to digital pin 2 for setting pump on
#define watchdogRelay 13  // output signal pin to indicate reset (stays high going into inverter until reset)
// do not use analog 4 and 5, used for SDA SCL on I2C display

// box reset is referring to reseting the arduino and all 5 volt sensor or signal components. Use watchdog timer to send low signal on reset.

LiquidCrystal_I2C lcd(0x27, 16, 2);

int avgDistance = 0;     // set average distance to global for use in multiple functions
int heightSetting = 25;  // set default trigger height for water level, keep global to adjust with one function (adjustLevel) and read the value for comparison in another (setupDisplay, refreshDisplay, and autoRunPump)

// all alarm conditions shown here
int sensorError = 0;  // tracking variable for errors in sensor, keep global to adjust with one function and read the value for comparison in another

// alarm will also set off if arduino resets, also triggering relay reset for modules, does not turn off alarm when no longer true, manual button to reset latch must be pushed and all bools false
bool checkSensor = false;  // fails on reporting too many zeros
bool checkRelay = false;   // current draw sense required
bool checkWater = false;   // water sensor input required

// section for display formatting
char cm[] = "cm";
char noData3[] = "???";
char set[] = "Set:";      // for displaying set trigger level
char water[] = "Wtr:";    // for displaying current water distance
char error[] = "Err";     // only show when displaying error messages
char pumping[] = "Pump";  // for displaying second of pumping

byte periodsChar[8] = {  // ellipses character (custom char 2)
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B10101
};
byte linesChar[8] = {  // 2 vertical lines seperation character (custom char 1)
  B01010,
  B01010,
  B01010,
  B01010,
  B01010,
  B01010,
  B01010,
  B01010
};


// this function only controlls the ultrasonic distance sensor, loop for averaging the measurement, and assigning the value read to a global variable to allow the refreshDisplay function to update at the end after leaving loop but before leaving function
// THIS IS NOT A MODULAR FAST SORTING ALGORITHM IT REFERENCES THE LAST VALUE RETURNED AFTER A FULL PROGRAM LOOP TO SORT OUT ERRANT INPUTS
// IF THE TOTAL TIME BETWEEN THE LAST CYCLE THIS FUNCTION RAN ON AND THE NEXT SENSOR INPUT BEING RELEVANT TO DATA SORTING IS TOO LONG IT IS UNUSABLE IN THAT SETTING

// SET REFRESH DISPLAY TO HANDLE 0 as showing noData char --------------------------------------------------------------------------!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// STILL NEED TO BE ABLE TO TRACK IF AUTORUNPUMP HAS GONE OFF otherwise water level change will be greater than the error correction allowed between loops

int sensorData() {
  int loops = 6;            // set number of loops for avg measurement
  int rollingTotal[loops];  // establish array to store sensor inputs while sorting through data
  int loopAvg = 0;          // used as return value for function, calculate avg of rollingTotal at end of function, otherwise allow to return 0

  // active error tracking
  int zeroCount = 0;
  int zeroMax = 10;   // total number of 0s that can be in one loop before fault condition is met
  int faultMax = 10;  // number of loops with more than zeroMax 0 readings before throwing error

  int variableMod = 10;  // sets a value to compare the modulus in data sorting later, prevents errant inputs from being considered, does not add to an error count like 0s

  for (int i = 0; i < loops; i++) {

    digitalWrite(trig, LOW);  // sets trigger pin to LOW for 5 microseconds to reset pin
    delayMicroseconds(5);
    digitalWrite(trig, HIGH);  // setting trigger pin high for 10 microseconds (not flexible, only try up to 20 micro)
    delayMicroseconds(15);
    digitalWrite(trig, LOW);                    // keep low until new trigger
    int distance = pulseIn(echo, HIGH, 26000);  //read in time

    // algorithm invloving speed of sound in air converted to centimeters or other
    distance = (distance / 2) / 29.1;  // distance/58 for centimeters OR distance/

    delay(58);  //sets delay for sensing times (recommended 60ms total measurement cycle) we have spare time spent on calculations so can be reduced slightly

    Serial.print("Distance = ");
    Serial.print(distance);
    Serial.print("\t");

    // first check for a zero reading to allow breakout structure to be checked each time a 0 is read
    if (distance == 0) {
      i--;
      zeroCount++;
      Serial.print("0! ");

      // break out of loop if sensor is returning too many 0 readings
      if (zeroCount >= zeroMax) {
        lcd.setCursor(4, 0);  // draw no data on water level measurement for visual indication of sensor loop reading 0s
        lcd.print(noData3);
        sensorError++;
        // add a return for sensor error loops and boolean change if alarm conditions met
        if (sensorError > faultMax) {  // if sensor gets more loops than faultMax, with over zeroMax 0s, set error messsage true
          checkSensor = true;
        }
        Serial.println("faultMax reached");
        return loopAvg;
      }
    } else if (avgDistance == 0 && i < 3) {  // this condition only runs if no avgDistance has been set yet (besides initial 0) allows the establishment of a baseline on startup for error correction
      rollingTotal[i] = distance;
      Serial.print("avgDistance = 0\t");Serial.print("rollingTotal at index = ");Serial.println(rollingTotal[i]);
    } else if (i < 2) {  // for first two spots in each loop check data against last measured and corrected avg measurement of the loop that was stored globally

      // use your variable modulus for error correction here, ONLY MOD THE LARGER VS THE SMALLER NUMBER (smaller % larger = smaller, so if the smaller number is anything larger then the modulus check it will false positive)
      // if data is considered errant, remove loop tracker as we do with a 0 value input, disregard the gathered data and continue on to next for() iteration final else is going to allow data assignment
      if (distance < avgDistance && avgDistance % distance > variableMod) {  // odds are the water level is higher (distance is lower, or closer to sensor) so we sort through this first
        Serial.println("distance < avgDistance && avgDistance % distance > variableMod");
        i--;
      } else if (distance > avgDistance && distance % avgDistance > variableMod) {
        Serial.println("distance > avgDistance && distance % avgDistance > variableMod");
        i--;
      } else {
        rollingTotal[i] = distance;
        Serial.print("rollingTotal at index = ");Serial.println(rollingTotal[i]);
      }
    } else {
      int rollAvg;
      rollAvg = rollingTotal[i - 1] + rollingTotal[i - 2];  // two accepted inputs with error correction based on average (or being the only first 3 inputs)
      rollAvg = rollAvg / 2;

      // use your variable modulus for error correction here, ONLY MOD THE LARGER VS THE SMALLER NUMBER (smaller % larger = smaller, so if the smaller number is anything larger then the modulus check it will false positive)
      // if data is considered errant, remove loop tracker as we do with a 0 value input, disregard the gathered data and continue on to next for() iteration final else is going to allow data assignment
      if (distance < rollAvg && rollAvg % distance > variableMod) {  // odds are the water level is higher (distance is lower, or closer to sensor) so we sort through this first
        Serial.println("distance < rollAvg && rollAvg % distance > variableMod");
        i--;
      } else if (distance > rollAvg && distance % rollAvg > variableMod) {
        Serial.println("distance > rollAvg && distance % rollAvg > variableMod");
        i--;
      } else {
        rollingTotal[i] = distance;
        Serial.print("rollingTotal at index = ");Serial.println(rollingTotal[i]);
      }
    }
    // if a 0 is returned we skip here the the end of the for() loop unless we hit the max 0s permitted for the loop
  }

  for (int i = 0; i < loops; i++) {
    loopAvg += rollingTotal[i];
  }
  loopAvg = loopAvg / loops;

  // after for loop check if any zeros were read, if not we can reset the alarm for checkSensor
  if (zeroCount == 0) {
    checkSensor = false;
  }
  Serial.print("\n------Leaving sensor loop------\n");Serial.print("loopAvg = ");Serial.println(loopAvg);Serial.println("\n\n");
  return loopAvg;
}

void autoRunPump(int startPump) {
  int pumpTime = startPump;  // set startPump to pumpTime as pumpTime is changed iteratively, startPump is the inital value only
  int S = 1000;              // used for math to split pump time into 1 second sections by milliseconds

  if (avgDistance <= heightSetting) {
    digitalWrite(runPumpRelay, HIGH);
    while (pumpTime >= 0) {
      if (pumpTime == startPump) {
        // use the following line only initially (15x0) will only need to be updated after each delay

        // we dont want to create a global variable for tracking time or have to pass in a value to refreshDisplay function. Update timer display within this function
        lcd.setCursor(10, 0);  // must be on upper row column (10x0) after vertical divider

        // use the following line only initially (15x0) will only need to be updated after each delay
        // prints "Pump...#" where # is the inital pumpTime in seconds
        lcd.print(pumping);
        lcd.write(2);
        lcd.print(pumpTime / S);

        delay(S);
        pumpTime = pumpTime - S;
      } else {
        lcd.setCursor(15, 0);
        lcd.print(pumpTime / S);
        delay(S);
        watchdog.reset();
        pumpTime = pumpTime - S;
      }
    }
    digitalWrite(runPumpRelay, LOW);
    delay(1000);
    watchdog.reset();
    lcd.setCursor(10, 0);
    lcd.print("      ");  // display 6 spaces as blanking for pump timer after 3 second settling period... this allows splashing water and moving parts to not make erratic sensor reading for ultrasonic style sensors. Direct contact water sensors may not need this at all depending on setup
  }
  return;
}

void setupDisplay() {
  // start display and write the charaters that will not be written over
  lcd.init();                      // initialize the lcd
  lcd.createChar(1, linesChar);    // vertical line divider custom char
  lcd.createChar(2, periodsChar);  // elipses custom char
  lcd.backlight();                 // enable backlight
  lcd.setCursor(0, 0);             // begin 0,0 writing
  lcd.print(water);                // display "Wtr:"
  lcd.print(noData3);              // display no data during startup for water level (allows visual inidcator of code hangup if things don't load far enough to get senor data)
  lcd.print(cm);                   // display "cm"
  lcd.write(1);                    // create vertical line divier (currnetly 9x0 coord on LCD)
  lcd.setCursor(0, 1);             // go to line 2 on LCD
  lcd.print(set);                  // display "Set:"
  lcd.print(" ");                  // empty spot next to "Set:" since trigger level should be 2 digits in CM (<10 is too high for sump pit, >100 would run too often and pump for too long with 5 second pumping config and volume of our sump pit)
  lcd.print(heightSetting);        // display default height setting
  lcd.print(cm);                   // display "cm"
  lcd.write(1);                    // create vertical line divier (currnetly 9x0 coord on LCD)
}

int lastSetting;  // used to store a value between loops in refresh display function, saves time overwriting values that aren't normally updated (I2C commm. takes multiple clock cycles compared to quick if() statement check)

void refreshDisplay() {
  // run through on each refresh to display new reading for water level from sensor
  if (avgDistance < 10) {
    lcd.setCursor(4, 0);
    lcd.print("  ");
    lcd.print(avgDistance);
  } else if (avgDistance < 100) {
    lcd.setCursor(4, 0);
    lcd.print(" ");
    lcd.print(avgDistance);
  } else {
    lcd.setCursor(4, 0);
    lcd.print(avgDistance);
  }

  // prevents LCD commands from running when not making adjustments, compare to global variable lastSetting
  if (lastSetting != heightSetting) {
    lcd.setCursor(5, 1);
    lcd.print(heightSetting);
    lastSetting = heightSetting;
  }

  return;
}

// CAUTION CONTAINS A WHILE LOOP, unless other functions are added to interupt, will only leave the function once the minimum or maximum level setting is reached or no change is made (toggle switch released)
void adjustLevel() {
  int maxDistanceCM = 30;  // set upper limit for distance to trigger, higher is lower water level
  int minDistanceCM = 20;  // set lower limit for distance to trigger, lower is higher water level
  while (digitalRead(upLevel) == LOW || digitalRead(downLevel) == LOW) {
    if (digitalRead(upLevel) == LOW) {
      if (heightSetting >= maxDistanceCM) {
        return;
      } else {
        heightSetting++;
      }
    } else if (digitalRead(downLevel) == LOW) {
      if (heightSetting <= minDistanceCM) {
        return;
      } else {
        heightSetting--;
      }
    }
    refreshDisplay();  // called here to allow update to setting to display
    watchdog.reset();
    delay(125);
  }
  return;
}

void enableAlarm(bool sensor, bool relay, bool water) {
  if (sensor || relay || water) {
    // to NOT gate then trigger on 555 (PIN 2) set to latching behaviour for alarm
    digitalWrite(alarm, HIGH);
    return;
  } else if (sensor == false && relay == false && water == false) {
    // currently using hardwire reset button on alarm so this will not turn off the alarm just reset the error message
    digitalWrite(alarm, LOW);  // will keep the alarm set high until no conditions for error exist, even if reset button pushed, goes to trigger on 555 (PIN 2)
    return;
  }
}

void setup() {
  // enable watchdog with timer variable in seconds at the end, 1,2, or 4 works well for the application. If water levels can change quickly in the system consider evaluating all functions for new operating speed (less loops, more optimization) and watchdog reset positions
  watchdog.enable(Watchdog::TIMEOUT_4S);

  pinMode(watchdogRelay, OUTPUT);     // initialize "watchdog" pin to high
  digitalWrite(watchdogRelay, HIGH);  // do not overwrite unless using custom code to reset on program hangups for power relay on board, may be set low in this project to reset the power manually but right now is tracking power-on state of arduino, watchdog handles the reset itself

  pinMode(alarm, OUTPUT);  // alarm trigger signal initialization, routes to NOT gate and 555 as a latch circuit
  digitalWrite(alarm, LOW);

  pinMode(runPumpRelay, OUTPUT);  // pins for relay for pump out signal and button for manual run input

  pinMode(upLevel, INPUT_PULLUP);    // lower water level trigger switch (up cm)
  pinMode(downLevel, INPUT_PULLUP);  // higher water level trigger switch (down cm)

  pinMode(echo, INPUT);  // pins for ultrasonic sensor
  pinMode(trig, OUTPUT);
  digitalWrite(trig, LOW);  // default low for ultrasonic sensor trigger pin
  // digitalWrite(echo, HIGH);
  setupDisplay();
  Serial.begin(19200);
  watchdog.reset();
}

void loop() {
  adjustLevel();
  avgDistance = sensorData();
  refreshDisplay();
  // delay(1500);                                       // useful if debugging sensorData since it loops back around very quickly when not running the pump
  enableAlarm(checkSensor, checkRelay, checkWater);  // checks all three global booleans that are modified by above functions and checks based on external sensor input only
  watchdog.reset();
  autoRunPump(5000);  // pass in the length of time you want to run pump in milliseconds, auto refers to self check in function for conditions
  // IMPORTANT INTERGER FOR PUMP TIME, NOT CURRENTLY VARIABLE WHILE INSTALLED, TIME SHOULD EQUAL NO MORE THAN THE TIME TO PUMP FROM MAX OR MIN HEIGHT TO A SAFE LEVEL FOR PUMP
  // DO NOT SET TO LONGER THAN IT WOULD TAKE TO PUMP THE SYSTEM DRY AT LOWEST WATER LEVEL TRIGGER
}