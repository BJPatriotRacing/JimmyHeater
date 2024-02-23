/*

  Copyright (c) Patriot Racing 2022

  Jimmy temperature controller

  connections define in the #define connection secion


*/

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <Colors.h>
#include <EEPROM.h>
#include <FlickerFreePrint.h>
#include <elapsedMillis.h>
#include <DS18B20.h>

#define NO_PRESS      0
#define SHORT_PRESS   60
#define LONG_PRESS    2000
#define DEBOUNCE      200
#define VOLUME 30

// pin connections
#define CS_PIN        10
#define RST_PIN        8
#define DC_PIN         9
#define BK_PIN         5
#define AL_PIN         6
#define L1_PIN         2
#define L2_PIN         3
#define BU_PIN         A2
#define BD_PIN         A1
#define BE_PIN         A3
#define TS_PIN         4
#define STAGE1_LIMITS 0.15
#define STAGE2_LIMITS 1.15
#define SCALE_BAND 5.0
#define ALARM_BAND 2.0

double Setpoint = 77.0;
bool AlarmState = false;
double TempAdjust = 0.0;

unsigned long Bits, Count;
double Volts, r2;

byte Light2Warning = 0;
bool ResetLight2Warning = false;
bool TempLimitWarning = false;

bool RefreshDisplay = true;
byte PointsX[145];
byte PointsY[145];
uint8_t address[] = {0, 0, 0, 0, 0, 0, 0, 0};
bool ScreenSaverFlag = false;
int i = 0, TempX = 0;
double TempF = 0.0, oldTempF = 0.0;
double Output = 0.0;

double x = 0.0, ox = 0.0, y = 0.0, oy = 0.0;
double gx = 15,  gy = 125,  w = 140,  h = 95,  xlo = 0,  xhi = 140,  xinc = 28,  ylo = Setpoint - SCALE_BAND,  yhi = Setpoint - SCALE_BAND,  yinc = SCALE_BAND / 2;
byte  oldSSx = 0, SSx = 0, oldSSy = 0, SSy = 0;
unsigned long ButtonDebounce = 200;
int BEButtonPress = NO_PRESS;
int BUButtonPress = NO_PRESS;
int BDButtonPress = NO_PRESS;

bool Light1State = false;
bool Light2State = false;
bool TempAdjustState = false;

// the adafruit library has an issue with init method
// our displays are green tab, but you must init with the black_tab and change the library

// Display.initR(INITR_BLACKTAB);

// in the ST7735.cpp library around line 240 add
//   _colstart = 2;
//   _rowstart = 1;
Adafruit_ST7735 Display = Adafruit_ST7735(CS_PIN, DC_PIN, RST_PIN);

elapsedSeconds ScreenUpdate;
elapsedMillis TempUpdate;
elapsedMillis AlarmPulse;
elapsedMillis AlarmUpdate;
elapsedMillis DebounceTime;
elapsedSeconds ScreenSaver;
elapsedSeconds TempLimitTimer;

DS18B20 Dallas(TS_PIN);

void setup() {

  byte temp = 0;

  Serial.begin(57600);

  //  Serial.println("Starting...");
  pinMode(BK_PIN, OUTPUT);
  pinMode(AL_PIN, OUTPUT);
  pinMode(L1_PIN, OUTPUT);
  pinMode(L2_PIN, OUTPUT);

  // shut the heaters off
  Light1(false);
  Light2(false);

  analogWrite(BK_PIN, 0);

  Dallas.getAddress(address);
  Dallas.select(address);
  Dallas.setResolution(12);

  Display.initR(INITR_BLACKTAB);
  Display.setRotation(1);
  Display.fillScreen(C_BLACK);

  // if new mcu, set defaults
  EEPROM.get(0, temp);
  if ((temp == 255) || (analogRead(BE_PIN) < 500)) {
    // reset eeprom, must be new MCU
    EEPROM.put(0, 0);
    EEPROM.put(10, Setpoint);
    EEPROM.put(20, TempAdjust);
    EEPROM.put(30, AlarmState);
  }

  EEPROM.get(10, Setpoint);
  EEPROM.get(20, TempAdjust);
  EEPROM.get(30, AlarmState);

  //  Serial.print("Setpoint  "); Serial.println(Setpoint);
  //  Serial.print("TempAdjust  "); Serial.println(TempAdjust);
  //  Serial.print("AlarmState  "); Serial.println(AlarmState);

  analogWrite(AL_PIN, VOLUME);
  delay(100);
  analogWrite(AL_PIN, 0);
  delay(100);
  analogWrite(AL_PIN, VOLUME);
  delay(100);
  analogWrite(AL_PIN, 0);
  delay(100);

  TempF = Dallas.getTempF();
  TempF = TempF + TempAdjust;

  GetYScale();
  DrawGrid();
  DisplayData();

  for (i = 0; i <= 255; i++) {
    analogWrite(BK_PIN, i);
    delay(10);
  }

  ScreenSaver  = 0;
  ScreenUpdate = 0;
  AlarmPulse = 0;
  TempUpdate = 10; // force temp read
  ScreenSaverFlag = false;

}

void loop(void) {

  ProcessButtons();

  // if screen is on for say more the 10 min, blank it out
  // 10 min = 600 sec

  if ((ScreenSaver > 60) && (!ScreenSaverFlag)) {
    ScreenSaverFlag = true;
    Display.fillScreen(C_BLACK);
  }

  // sound alarms
  if (AlarmState) {

    if (Light2Warning >= 10) {
      // either heaters can't keep up with thermal load
      // or bulbs burned out
      // pulse the alarm
      // I figure if we cycle 10 times we probably need to look
      // at the lights
      if (AlarmPulse < 500) {
        analogWrite(AL_PIN, VOLUME);
      }
      else if (AlarmPulse > 500) {
        analogWrite(AL_PIN, 0);
      }
      if (AlarmPulse > 1000) {
        AlarmPulse = 0;
      }
    }

    if ((TempLimitWarning) && (TempLimitTimer > 300)) {
      // let it over limit for say 5 min (300 seconds)
      // that way when we open/close lid, jimmy temp
      // should recover before alarm sounds
      analogWrite(AL_PIN, VOLUME);
    }
    else {
      analogWrite(AL_PIN, 0);
    }
  }

  // fetch temperature and determine if heaters need to activate
  // do this every few seconds
  if (TempUpdate >= 2000) {

    TempF = Dallas.getTempF() ;
    TempF = TempF + TempAdjust;

    TempAdjustState = false;

    if (TempF >= (Setpoint + ALARM_BAND)) {

      // ensure heaters are off
      Light1State = false;
      Light2State = false;
      Light1(Light1State);
      Light2(Light2State);
      if (!TempLimitWarning) {
        TempLimitWarning = true;
        TempLimitTimer = 0;
      }
    }

    if (TempF <= (Setpoint - ALARM_BAND)) {
      TempLimitWarning = true;
      TempLimitTimer = 0;
    }

    if ((TempF > (Setpoint - ALARM_BAND)) && (TempF < (Setpoint + ALARM_BAND)))  {
      TempLimitWarning = false;
    }

    if (TempF > (Setpoint + STAGE1_LIMITS)) {
      // turn all heaters off
      Light1State = false;
      Light2State = false;
      Light1(Light1State);
      Light2(Light2State);
      ResetLight2Warning = true;
    }
    else if (TempF <  (Setpoint - STAGE2_LIMITS)) {
      // fire up heater 1 and heater 2
      Light1State = true;
      Light2State = true;
      Light1(Light1State);
      Light2(Light2State);
      // if you had to fire up heater 2, maybe heater 1 bulb is burned out?
      if (ResetLight2Warning) {
        ResetLight2Warning = false;
        Light2Warning++;
      }
    }
    else if (TempF <  (Setpoint - STAGE1_LIMITS)) {
      // fire up heater 1
      Light1State = true;
      Light2State = false;
      Light1(Light1State);
      Light2(Light2State);

    }

    DisplayData();

    TempUpdate = 0;
  }

  // update graph every 1 minutes
  // 1 min = 60 seconds
  // we have 140 points on graph so full grap is 2 hr 2 min
  if (ScreenUpdate >= 60) {

    ScreenUpdate = 0;

    x++;

    //cache the data and graph if needed
    // drawing check in Graph function
    Graph(x, TempF, RefreshDisplay);

    if (!ScreenSaverFlag) {
      if (x >= 140) {
        RefreshDisplay = false;
        x = 0;
        RedrawData();
      }
    }
    else {
      if (x >= 140) {
        RefreshDisplay = false;
        x = 0;
      }
    }

  }
}

void RedrawData() {

  for (i = 3; i < 139; i++) {
    if (PointsX[i] > 0) {
      if (i < x) {
        Display.drawLine(PointsX[i - 1], PointsY[i - 1], PointsX[i], PointsY[i], C_YELLOW);
        Display.drawLine(PointsX[i - 1], PointsY[i - 1] + 1, PointsX[i], PointsY[i] + 1, C_YELLOW);
        Display.drawLine(PointsX[i - 1], PointsY[i - 1] - 1, PointsX[i], PointsY[i] - 1, C_YELLOW);
      }
      else if (i == x) {
        Display.drawLine(PointsX[i], gy, PointsX[i], gy - h, C_CYAN);
      }
      else {
        Display.drawLine(PointsX[i - 1], PointsY[i - 1], PointsX[i], PointsY[i], C_DKYELLOW);
        Display.drawLine(PointsX[i - 1], PointsY[i - 1] + 1, PointsX[i], PointsY[i] + 1, C_DKYELLOW);
        Display.drawLine(PointsX[i - 1], PointsY[i - 1] - 1, PointsX[i], PointsY[i] - 1, C_DKYELLOW);
      }
    }
  }
}

void DisplayData() {



  if (ScreenSaverFlag) {

    SSx = rand() % 35;
    SSx = SSx + 2;
    SSy = rand() % 85;
    SSy = SSy + 5;

    Display.setTextSize(5);
    Display.setTextColor(C_BLACK, C_BLACK);
    Display.setCursor(oldSSx, oldSSy);
    Display.print(oldTempF, 0);
    Display.setTextSize(3);
    Display.println( (int) ((oldTempF) * 10) % 10);

    if ((Light1State) || (Light2State)) {
      Display.setTextColor(C_RED, C_BLACK);
    }
    else {
      Display.setTextColor(C_CYAN, C_BLACK);
    }

    //Serial.println( TempF, 4);
    //Serial.println( (int) ((TempF) * 10) % 10);

    Display.setTextSize(5);
    Display.setCursor(SSx, SSy);
    Display.print(TempF, 0);
    Display.setTextSize(3);
    Display.println( (int) ((TempF) * 10) % 10);

    oldSSx = SSx;
    oldSSy = SSy;
    oldTempF = TempF;
    return;

  }

  Display.setTextSize(2);

  Display.setCursor(1 , 5);
  Display.setTextColor(C_GREEN, C_DKGREY);
  Display.print(Setpoint, 0);

  if (TempAdjustState) {
    Display.setTextColor(C_RED, C_DKGREY);
  }
  else {
    Display.setTextColor(C_YELLOW, C_DKGREY);
  }

  Display.setCursor(36, 5);
  Display.print(TempF, 1);

  Display.setTextSize(2);
  Display.setCursor(95, 5);

  if (AlarmState) {
    Display.setTextColor(C_RED, C_DKGREY);
    Display.print(F("A"));
  }
  else {
    Display.setTextColor(C_DKRED, C_DKGREY);
    Display.print(F("A"));
    Display.drawLine(93, 4, 107, 18, C_DKRED);
    Display.drawLine(93, 5, 107, 19, C_DKRED);
    Display.drawLine(93, 6, 107, 20, C_DKRED);
  }

  if (Light1State) {
    Display.setTextColor(C_WHITE, C_RED);
    Display.fillRoundRect(115, 2, 19, 22, 5, C_RED);
  }
  else {
    Display.setTextColor(C_DKRED, C_MDGREY);
    Display.fillRoundRect(115, 2, 19, 22, 5, C_MDGREY);
  }

  Display.setCursor(120, 5);
  Display.print(F("1"));

  if (Light2State) {
    Display.setTextColor(C_WHITE, C_RED);
    Display.fillRoundRect(135, 2, 19, 22, 5, C_RED);
  }
  else {
    Display.setTextColor(C_DKRED, C_MDGREY);
    Display.fillRoundRect(135, 2, 19, 22, 5, C_MDGREY);
  }
  Display.setCursor(140, 5);

  Display.print(F("2"));

}
void GetYScale() {

  ylo = Setpoint - SCALE_BAND;
  yhi = Setpoint + SCALE_BAND;
  yinc = SCALE_BAND / 2;

  // blank out old since scale has been changed
  Display.fillRect(0, 0, 160, 25, C_DKGREY);

}

void Light1(bool state) {
  if (state) {
    digitalWrite(L1_PIN, HIGH);
  }
  else {
    digitalWrite(L1_PIN, LOW);
  }
  delay(100);
}

void Light2(bool state) {
  if (state) {
    digitalWrite(L2_PIN, HIGH);
  }
  else {
    digitalWrite(L2_PIN, LOW);
  }
  delay(100);
}

void DrawGrid() {

  double i = 0.0;
  double temp;

  // draw y scale
  temp =  (ylo - ylo) * (gy - h - gy) / (yhi - ylo) + gy;
  Display.drawLine(gx, temp, gx + w, temp, C_BLUE);
  Display.drawLine(gx, temp - 1, gx + w, temp - 1, C_BLUE);

  temp =  (yhi - ylo) * (gy - h - gy) / (yhi - ylo) + gy;
  Display.drawLine(gx, temp, gx + w, temp, C_GREY);

  // draw alarm limits
  y = Setpoint + ALARM_BAND;
  y =  (y - ylo) * (gy - h - gy) / (yhi - ylo) + gy;
  for (i = gx; i < (gx + w); i += 6) {
    Display.drawLine(i, y, i + 2, y, C_RED);
  }

  y = Setpoint - ALARM_BAND;
  y =  (y - ylo) * (gy - h - gy) / (yhi - ylo) + gy;
  for (i = gx; i < (gx + w); i += 6) {
    Display.drawLine(i, y, i + 2, y, C_BLUE);
  }

  // draw lights off
  y = Setpoint + (1.0f * STAGE1_LIMITS);
  y =  (y - ylo) * (gy - h - gy) / (yhi - ylo) + gy;
  for (i = gx; i < (gx + w); i += 6) {
    Display.drawLine(i, y, i + 2, y, C_MDORANGE);
  }

  // draw light on stage 1
  y = Setpoint - (1.0f * STAGE1_LIMITS);
  y =  (y - ylo) * (gy - h - gy) / (yhi - ylo) + gy;
  for (i = gx; i < (gx + w); i += 6) {
    Display.drawLine(i, y, i + 2, y, C_MDORANGE);
  }
  // draw light on stage 2
  y = Setpoint - STAGE2_LIMITS;
  y =  (y - ylo) * (gy - h - gy) / (yhi - ylo) + gy;
  for (i = gx; i < (gx + w); i += 6) {
    Display.drawLine(i, y, i + 2, y, C_MDORANGE);
  }
  y = Setpoint;
  y =  (y - ylo) * (gy - h - gy) / (yhi - ylo) + gy;
  //temp =  (i - ylo) * (gy - h - gy) / (yhi - ylo) + gy;
  Display.drawLine(gx, y, gx + w, y, C_GREEN);

  Display.setTextSize(1);

  for ( i = ylo; i <= yhi; i += yinc) {

    temp =  (i - ylo) * (gy - h - gy) / (yhi - ylo) + gy;
    Display.setTextColor(C_WHITE, C_BLACK);
    if (i == yhi) {
      Display.setCursor(2, temp - 1);
    }
    else {
      Display.setCursor(2, temp - 4);
    }
    Display.println(i, 0);
  }

  // draw x scale
  for (i = xlo; i <= xhi; i += xinc) {
    temp =  (i - xlo) * ( w) / (xhi - xlo) + gx;
    if (i == xlo) {
      Display.drawLine(temp, gy, temp, gy - h, C_BLUE);
      Display.drawLine(temp + 1, gy, temp + 1, gy - h, C_BLUE);
    }
    else {
      Display.drawLine(temp, gy, temp, gy - h, C_GREY);
    }
  }
}

void Graph(double x, double y, bool & redraw) {

  // do some bounds checks to keep graph from falling out
  // of range
  if (y >= yhi) {
    y = yhi;
  }

  if (y <= ylo) {
    y = ylo;
  }

  TempX = x;

  if (redraw == true) {
    redraw = false;
    ox = (x - xlo) * ( w) / (xhi - xlo) + gx;
    oy = (y - ylo) * (gy - h - gy) / (yhi - ylo) + gy;
  }

  x =  (x - xlo) * ( w) / (xhi - xlo) + gx;
  y =  (y - ylo) * (gy - h - gy) / (yhi - ylo) + gy;

  PointsX[TempX] = x;
  PointsY[TempX] = y;

  if (ScreenSaverFlag) {
    ox = x;
    oy = y;
    return;
  }

  if (x > ox) {
    //Display.fillRect(ox, gy - h, 2, h, C_BLACK);
    Display.drawLine(ox, gy, ox, gy - h, C_BLACK);
    DrawGrid();
    Display.drawLine(x, gy, x, gy - h, C_CYAN);

    Display.drawLine(ox, oy, x, y, C_YELLOW);
    Display.drawLine(ox, oy + 1, x, y + 1, C_YELLOW);
    Display.drawLine(ox, oy - 1, x, y - 1, C_YELLOW);
  }

  ox = x;
  oy = y;

}

unsigned int Debounce(int pin, unsigned long & dtime) {
  if (digitalRead(pin) == LOW) {
    if ((millis() - dtime) > DEBOUNCE) {
      // button now debounced, long press or short one
      dtime = millis();
      while (digitalRead(pin) == LOW) {
        if ((millis() - dtime) > LONG_PRESS) {
          dtime = millis();
          return LONG_PRESS;
        }
      }
      dtime = millis();
      return SHORT_PRESS;
    }
  }
  return NO_PRESS;
}

void ProcessButtons() {

  if (ScreenSaverFlag) {
    // wake up display and bail out
    if ( (analogRead(BE_PIN) < 500) || (analogRead(BU_PIN) < 500) || (analogRead(BD_PIN) < 500)) {
      ScreenSaverFlag = false;
      Display.fillScreen(C_BLACK);
      ScreenSaver = 0;
      GetYScale();
      DrawGrid();
      DisplayData();
      RedrawData();
      delay(500); // poor mans debounce
      return;
    }
  }
  else {
    // if button enter fire up menu
    if (analogRead(BE_PIN) < 500) {
      Menu();
      ScreenSaverFlag = false;
      Display.fillScreen(C_BLACK);
      ScreenSaver = 0;
      GetYScale();
      DrawGrid();
      DisplayData();
      RedrawData();
    }
    // if button up light up 1
    if (analogRead(BU_PIN) < 500) {
      if (!Light1State) {
        Light1State = true;
        DisplayData();
        Light1(Light1State);
        while (analogRead(BU_PIN) < 500) {
          // keep light on until button not pressed
        }
        // turn light off
        Light1State = false;
        DisplayData();
        Light1(Light1State);
      }
      else {
        Light1State = false;
        DisplayData();
        Light1(Light1State);
        while (analogRead(BU_PIN) < 500) {
          // keep light on until button not pressed
        }
        // turn light off
        Light1State = true;
        DisplayData();
        Light1(Light1State);
        return;
      }

    }
    // if button down light up 2
    if (analogRead(BD_PIN) < 500) {
      if (!Light2State) {
        Light2State = true;
        DisplayData();
        Light2(Light2State);
        while (analogRead(BD_PIN) < 500) {
          // keep light on until button not pressed
        }
        // turn light off
        Light2State = false;
        DisplayData();
        Light2(Light2State);
      }
      else {
        Light2State = false;
        DisplayData();
        Light2(Light2State);
        while (analogRead(BD_PIN) < 500) {
          // keep light on until button not pressed
        }
        // turn light off
        Light2State = true;
        DisplayData();
        Light2(Light2State);
        return;
      }
    }
  }
}

void Menu() {

  int ItemID = 0;

  Display.fillScreen(C_BLACK);

  DrawItems(0, false);

  while (1) {

    if ( analogRead(BU_PIN) < 500) {
      delay(100);
      ItemID--;
      if (ItemID < 0) {
        ItemID = 3;
      }
      DrawItems(ItemID, false);
    }
    if (( analogRead(BD_PIN) < 500)) {
      delay(100);
      ItemID++;
      if (ItemID > 3) {
        ItemID = 0;
      }
      DrawItems(ItemID, false);
    }
    if ( analogRead(BE_PIN) < 500) {
      delay(100);
      if (ItemID == 0) {
        Display.fillScreen(C_BLACK);
        GetYScale();
        DrawGrid();
        DisplayData();
        break;
      }
      else {
        DrawItems(ItemID, true);

        while (1) {

          if ( analogRead(BE_PIN) < 500) {
            delay(100);
            Display.fillScreen(C_BLACK);
            DrawItems(ItemID, false);
            break;
          }
          if ( analogRead(BU_PIN) < 500) {
            delay(100);
            if (ItemID == 1) {
              AlarmState = !AlarmState;
              if (!AlarmState) {
                analogWrite(AL_PIN, 0);
              }
              DrawItems(ItemID, true);
            }
            else if (ItemID == 2) {
              Setpoint ++;
              if (Setpoint > 99.0) {
                Setpoint = 99.0;
                analogWrite(AL_PIN, VOLUME);
                delay(100);
                analogWrite(AL_PIN, 0);
                delay(100);
              }
              DrawItems(ItemID, true);
            }
            else if (ItemID == 3) {
              TempAdjust += 0.1;
              DrawItems(ItemID, true);
            }
          }
          if ( analogRead(BD_PIN) < 500) {
            delay(100);
            if (ItemID == 1) {
              AlarmState = !AlarmState;
              if (!AlarmState) {
                analogWrite(AL_PIN, 0);
              }
              DrawItems(ItemID, true);
            }
            else if (ItemID == 2) {
              Setpoint --;
              if (Setpoint < 77.0) {
                Setpoint = 77.0;
                analogWrite(AL_PIN, VOLUME);
                delay(100);
                analogWrite(AL_PIN, 0);
                delay(100);
              }
              DrawItems(ItemID, true);
            }
            else if (ItemID == 3) {
              TempAdjust -= 0.1;
              DrawItems(ItemID, true);
            }
          }
        }
      }
    }
  }

  EEPROM.put(10, Setpoint);
  EEPROM.put(20, TempAdjust);
  EEPROM.put(30, AlarmState);

  // reset the alarms, if user is making conscience deceision, they can check the heaters, etc.
  if (!AlarmState) {
    ResetLight2Warning = false;
    TempLimitWarning = false;
  }

}

void DrawItems(byte Item, bool high) {

  Display.fillScreen(C_BLACK);
  Display.setTextSize(2);
  Display.setCursor(5 , 5);

  if (Item == 0) {
    Display.fillRect(0 , 0, 159, 30, C_RED);
    Display.setTextColor(C_WHITE, C_RED);
    Display.print(F("Exit"));
  }
  else {
    Display.fillRect(0 , 0, 159, 30, C_DKBLUE);
    Display.setTextColor(C_WHITE, C_DKBLUE);
    Display.print(F("Settings"));
  }

  if (Item == 1) { // alarm
    if (high) {
      Display.fillRect(5 , 35, 155, 30, C_RED);
      Display.setTextColor(C_WHITE, C_RED);
    }
    else {
      Display.fillRect(5 , 35, 155, 30, C_DKBLUE);
      Display.setTextColor(C_WHITE, C_DKBLUE);
    }
  }

  else {
    Display.fillRect(5 , 35, 155, 30, C_BLACK);
    Display.setTextColor(C_WHITE, C_BLACK);
  }

  Display.setCursor(10 , 43);
  Display.print(F("Alarm"));
  Display.setCursor(117 , 43);
  if (AlarmState) {
    Display.print(F("ON"));
  }
  else {
    Display.print(F("OFF"));
  }

  if (Item == 2) { // setpoint
    if (high) {
      Display.fillRect(5 , 65, 155, 30, C_RED);
      Display.setTextColor(C_WHITE, C_RED);
    }
    else {
      Display.fillRect(5 , 65, 155, 30, C_DKBLUE);
      Display.setTextColor(C_WHITE, C_DKBLUE);
    }

  }
  else {
    Display.fillRect(5 , 65, 155, 30, C_BLACK);
    Display.setTextColor(C_WHITE, C_BLACK);
  }
  Display.setCursor(10 , 75);
  Display.print(F("Setpoint"));
  Display.setCursor(117 , 75);
  Display.print(Setpoint, 0);

  if (Item == 3) {
    // setpoint
    if (high) {
      Display.fillRect(5 , 95, 155, 30, C_RED);
      Display.setTextColor(C_WHITE, C_RED);
    }
    else {
      Display.fillRect(5 , 95, 155, 30, C_DKBLUE);
      Display.setTextColor(C_WHITE, C_DKBLUE);
    }
  }
  else {
    Display.fillRect(5 , 95, 155, 30, C_BLACK);
    Display.setTextColor(C_WHITE, C_BLACK);
  }
  Display.setCursor(10 , 105);
  Display.print(F("Calib."));
  Display.setCursor(110 , 105);
  Display.print(TempAdjust, 1);

}

/////////////////////////////////////
