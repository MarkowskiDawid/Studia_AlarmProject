#include <Wire.h>

const int PCF_LED = 0x20; 
const int PCF_PIR = 0x21; 

const int BTN_UP = A0;
const int BTN_OK = A1;
const int BTN_DOWN = A2;
const int BUZZER = A3;

const int DB7 = 2;
const int DB6 = 3;
const int DB5 = 4;
const int DB4 = 5;
const int E = 6;
const int RS = 7;

const int RedLed = 11;
const int OrnLed = 12;
const int BlueLed = 13;

#define SENSOR_NONE 0
#define SENSOR_PIR 1
#define SENSOR_DOOR 2
#define SENSOR_VIB 3

#define ALARM_DISARMED 0
#define ALARM_ARMED 1
#define ALARM_EXIT_DELAY 2
#define ALARM_TRIGGERED 3

struct Sensor {
  byte pin;
  byte type;
  bool active;
  bool violated;
};

Sensor sensors[8] = {
  {0, SENSOR_NONE, false, false}, {1, SENSOR_NONE, false, false},
  {2, SENSOR_NONE, false, false}, {3, SENSOR_NONE, false, false},
  {4, SENSOR_NONE, false, false}, {5, SENSOR_NONE, false, false},
  {6, SENSOR_NONE, false, false}, {7, SENSOR_NONE, false, false}
};

byte systemState = ALARM_DISARMED; 
int alarm_sense = 0; 
int exitDelay = 5;
int triggeringSensorIndex = -1;

const char* menuItems[] = {"Uzbroj alarm", "Rozbroj alarm", "Czujniki", "Ustawienia"};
const char* sensorMenuItems[] = {"Lista czujnikow", "Test czujnikow", "Wroc"};
const char* typeNames[] = {"BRAK", "PIR", "DRZWI", "WIBRACJA"};

int menuLevel = 0; 
int currentMenuIndex = 0;
int subMenuIndex = 0;
int listIndex = 0;
int testIndex = 0;
int tempPin = 0;
int tempType = 1;

bool inMenu = false;
unsigned long lastActivityTime = 0;
unsigned long armStartTime = 0;
unsigned long sirenLastToggle = 0;
bool sirenState = false;

const unsigned long MENU_TIMEOUT = 5000; 

bool lastUp = HIGH, lastDown = HIGH, lastOk = HIGH;

void updateLEDDisplay(byte state) {
  Wire.beginTransmission(PCF_LED); 
  Wire.write(state);            
  Wire.endTransmission();
}

byte readSensors() {
  Wire.requestFrom(PCF_PIR, 1);
  if (Wire.available()) return Wire.read();
  return 0xFF;
}

void LCD_Write4Bits(unsigned char dtw) {
  digitalWrite(E, LOW);
  digitalWrite(DB4, (dtw & 0x01));
  digitalWrite(DB5, (dtw & 0x02));
  digitalWrite(DB6, (dtw & 0x04));
  digitalWrite(DB7, (dtw & 0x08));
  digitalWrite(E, HIGH); delay(2);
  digitalWrite(E, LOW); delay(2);
}

void LCD_WriteData4Bits(unsigned char dtw) {
  digitalWrite(RS, HIGH);
  LCD_Write4Bits(dtw >> 4);
  LCD_Write4Bits(dtw & 0x0F);
}

void LCD_WriteCommand4Bits(unsigned char dtw) {
  digitalWrite(RS, LOW);
  LCD_Write4Bits(dtw >> 4);
  LCD_Write4Bits(dtw & 0x0F);
}

void LCD_Init4bit(void) {
  digitalWrite(RS, LOW); digitalWrite(E, LOW);
  LCD_Write4Bits(0x03); delay(5);
  LCD_Write4Bits(0x03); delay(2);
  LCD_Write4Bits(0x03); delay(2);
  LCD_Write4Bits(0x02);
  LCD_WriteCommand4Bits(0x28);
  LCD_WriteCommand4Bits(0x0F);
  LCD_WriteCommand4Bits(0x01);
  LCD_WriteCommand4Bits(0x06);
}

void LCD_WriteText(char *ttw) {
  while(*ttw) LCD_WriteData4Bits(*ttw++);
}

void showMainScreen() {
  LCD_WriteCommand4Bits(0x80);
  if (systemState == ALARM_DISARMED) {
    LCD_WriteText((char*)"Alarm: OFF      ");
    LCD_WriteCommand4Bits(0xC0); LCD_WriteText((char*)"                ");
  } else if (systemState == ALARM_ARMED) {
    LCD_WriteText((char*)"Alarm: ON       ");
    LCD_WriteCommand4Bits(0xC0); LCD_WriteText((char*)"                ");
  } else if (systemState == ALARM_EXIT_DELAY) {
    LCD_WriteText((char*)"Uzbrajanie...   ");
  } else if (systemState == ALARM_TRIGGERED) {
    LCD_WriteText((char*)"ALARM!          ");
    LCD_WriteCommand4Bits(0xC0);
    if (triggeringSensorIndex >= 0) {
      LCD_WriteText((char*)"CZUJNIK: ");
      if (sensors[triggeringSensorIndex].type == SENSOR_PIR) {
        LCD_WriteText((char*)"PIR"); LCD_WriteData4Bits(triggeringSensorIndex + 1 + '0');
        LCD_WriteText((char*)"   ");
      }
      else if (sensors[triggeringSensorIndex].type == SENSOR_DOOR) LCD_WriteText((char*)"DRZWI  ");
      else if (sensors[triggeringSensorIndex].type == SENSOR_VIB) LCD_WriteText((char*)"WIBR.  ");
      else LCD_WriteText((char*)"INNY   ");
    } else LCD_WriteText((char*)"CZUJNIK: NIEZN. ");
  }
}

void showMenuDisplay() {
  if (menuLevel != 4) {
    LCD_WriteCommand4Bits(0x01);
  }
  LCD_WriteCommand4Bits(0x80);

  if (menuLevel == 0) {
    LCD_WriteText((char*)"--- MENU ---    ");
    LCD_WriteCommand4Bits(0xC0); LCD_WriteText((char*)menuItems[currentMenuIndex]);
  } 
  else if (menuLevel == 1) {
    LCD_WriteText((char*)"--- CZUJNIKI ---");
    LCD_WriteCommand4Bits(0xC0); LCD_WriteText((char*)sensorMenuItems[subMenuIndex]);
  } 
  else if (menuLevel == 2) {
    if (listIndex == 8) {
      LCD_WriteText((char*)"-> Wroc         ");
    } else {
      LCD_WriteText((char*)"PIN P"); LCD_WriteData4Bits(listIndex + '0');
      LCD_WriteText((char*)": ");
      if (sensors[listIndex].active) LCD_WriteText((char*)typeNames[sensors[listIndex].type]);
      else LCD_WriteText((char*)"PUSTY");
      LCD_WriteCommand4Bits(0xC0); LCD_WriteText((char*)"OK=Zmien/Dodaj");
    }
  } 
  else if (menuLevel == 3) {
    LCD_WriteText((char*)"TYP DLA P"); LCD_WriteData4Bits(tempPin + '0');
    LCD_WriteCommand4Bits(0xC0); LCD_WriteText((char*)typeNames[tempType]);
  } 
  else if (menuLevel == 4) {
    LCD_WriteText((char*)"P"); LCD_WriteData4Bits(testIndex + '0'); LCD_WriteText((char*)"-");
    if (sensors[testIndex].type == SENSOR_PIR) LCD_WriteText((char*)"PIR-");
    else if (sensors[testIndex].type == SENSOR_DOOR) LCD_WriteText((char*)"DRZWI-");
    else if (sensors[testIndex].type == SENSOR_VIB) LCD_WriteText((char*)"WIBR-");
    else LCD_WriteText((char*)"INNY-");

    byte data = readSensors();
    if (bitRead(data, testIndex)) LCD_WriteText((char*)"RUCH    ");
    else LCD_WriteText((char*)"OK      ");

    LCD_WriteCommand4Bits(0xC0);
    LCD_WriteText((char*)"OK = Wroc       ");
  } 
  else if (menuLevel == 5) {
    LCD_WriteText((char*)"CZAS WYJSCIA:");
    LCD_WriteCommand4Bits(0xC0);
    LCD_WriteData4Bits((exitDelay / 10) + '0'); LCD_WriteData4Bits((exitDelay % 10) + '0');
    LCD_WriteText((char*)" sek");
  }
}

void setup() {
  Wire.begin(); 
  Wire.beginTransmission(PCF_PIR); Wire.write(0xFF); Wire.endTransmission();
  
  pinMode(OrnLed, OUTPUT); pinMode(RedLed, OUTPUT); pinMode(BlueLed, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(OrnLed, LOW); digitalWrite(BlueLed, HIGH); digitalWrite(RedLed, HIGH);
  
  pinMode(BTN_UP, INPUT_PULLUP); pinMode(BTN_DOWN, INPUT_PULLUP); pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(E, OUTPUT); pinMode(RS, OUTPUT);
  pinMode(DB4, OUTPUT); pinMode(DB5, OUTPUT); pinMode(DB6, OUTPUT); pinMode(DB7, OUTPUT);
  
  LCD_Init4bit(); 

  LCD_WriteCommand4Bits(0x80);
  LCD_WriteText((char*)"System Alarmowy ");
  LCD_WriteCommand4Bits(0xC0);
  LCD_WriteText((char*)"Start...        ");
  delay(2000);

  updateLEDDisplay(0); 
  showMainScreen();
}

void loop() {
  bool currentUp = digitalRead(BTN_UP); 
  bool currentDown = digitalRead(BTN_DOWN); 
  bool currentOk = digitalRead(BTN_OK);

  if (currentUp == LOW && lastUp == HIGH) {
    delay(50); lastActivityTime = millis();
    if (!inMenu && systemState != ALARM_TRIGGERED && systemState != ALARM_EXIT_DELAY) { inMenu = true; menuLevel = 0; }
    else if (inMenu) { 
      if (menuLevel == 0) { currentMenuIndex--; if(currentMenuIndex < 0) currentMenuIndex = 3; }
      else if (menuLevel == 1) { subMenuIndex--; if(subMenuIndex < 0) subMenuIndex = 2; }
      else if (menuLevel == 2) { listIndex--; if(listIndex < 0) listIndex = 8; }
      else if (menuLevel == 3) { tempType--; if(tempType < 1) tempType = 3; }
      else if (menuLevel == 4) { 
        do { testIndex--; if(testIndex < 0) testIndex = 7; } while(!sensors[testIndex].active);
      }
      else if (menuLevel == 5) { exitDelay -= 5; if(exitDelay < 0) exitDelay = 0; }
      if (menuLevel != 4) showMenuDisplay();
    }
  }
  lastUp = currentUp;

  if (currentDown == LOW && lastDown == HIGH) {
    delay(50); lastActivityTime = millis();
    if (!inMenu && systemState != ALARM_TRIGGERED && systemState != ALARM_EXIT_DELAY) { inMenu = true; menuLevel = 0; }
    else if (inMenu) { 
      if (menuLevel == 0) { currentMenuIndex++; if(currentMenuIndex > 3) currentMenuIndex = 0; }
      else if (menuLevel == 1) { subMenuIndex++; if(subMenuIndex > 2) subMenuIndex = 0; }
      else if (menuLevel == 2) { listIndex++; if(listIndex > 8) listIndex = 0; }
      else if (menuLevel == 3) { tempType++; if(tempType > 3) tempType = 1; }
      else if (menuLevel == 4) { 
        do { testIndex++; if(testIndex > 7) testIndex = 0; } while(!sensors[testIndex].active);
      }
      else if (menuLevel == 5) { exitDelay += 5; if(exitDelay > 95) exitDelay = 95; }
      if (menuLevel != 4) showMenuDisplay();
    }
  }
  lastDown = currentDown;

  if (currentOk == LOW && lastOk == HIGH) {
    delay(50); lastActivityTime = millis();
    if (systemState == ALARM_TRIGGERED) {
      systemState = ALARM_DISARMED; alarm_sense = 0; triggeringSensorIndex = -1;
      for(int i=0; i<8; i++) sensors[i].violated = false;
      noTone(BUZZER); digitalWrite(RedLed, HIGH); updateLEDDisplay(0); showMainScreen();
      while(digitalRead(BTN_OK) == LOW);
    } 
    else if (!inMenu && systemState != ALARM_EXIT_DELAY) { inMenu = true; showMenuDisplay(); }
    else if (inMenu) {
      if (menuLevel == 0) {
        if (currentMenuIndex == 0) { systemState = ALARM_EXIT_DELAY; armStartTime = millis(); updateLEDDisplay(2); inMenu = false; showMainScreen(); }
        else if (currentMenuIndex == 1) { systemState = ALARM_DISARMED; alarm_sense = 0; updateLEDDisplay(0); inMenu = false; showMainScreen(); }
        else if (currentMenuIndex == 2) { menuLevel = 1; subMenuIndex = 0; showMenuDisplay(); }
        else if (currentMenuIndex == 3) { menuLevel = 5; showMenuDisplay(); }
      } 
      else if (menuLevel == 1) {
        if (subMenuIndex == 0) { menuLevel = 2; listIndex = 0; showMenuDisplay(); }
        else if (subMenuIndex == 1) { 
          int activeCount = 0;
          for(int i=0; i<8; i++) if(sensors[i].active) activeCount++;
          if(activeCount == 0) {
            LCD_WriteCommand4Bits(0x01); LCD_WriteCommand4Bits(0x80);
            LCD_WriteText((char*)"BRAK CZUJNIKOW! "); delay(1500); showMenuDisplay();
          } else {
            testIndex = 0;
            while(!sensors[testIndex].active) testIndex++;
            menuLevel = 4;
            LCD_WriteCommand4Bits(0x01);
            showMenuDisplay();
          }
        }
        else { menuLevel = 0; showMenuDisplay(); }
      } 
      else if (menuLevel == 2) { 
        if (listIndex == 8) { menuLevel = 1; showMenuDisplay(); }
        else {
          if (sensors[listIndex].active) { sensors[listIndex].active = false; showMenuDisplay(); }
          else { tempPin = listIndex; tempType = 1; menuLevel = 3; showMenuDisplay(); }
        }
      } 
      else if (menuLevel == 3) { sensors[tempPin].active = true; sensors[tempPin].type = tempType; menuLevel = 2; showMenuDisplay(); }
      else if (menuLevel == 4) { menuLevel = 1; showMenuDisplay(); }
      else if (menuLevel == 5) { menuLevel = 0; showMenuDisplay(); }
    }
  }
  lastOk = currentOk;

  if (inMenu && menuLevel == 4) {
    static unsigned long lastTestRefresh = 0;
    if (millis() - lastTestRefresh > 200) {
      lastTestRefresh = millis();
      lastActivityTime = millis(); 
      showMenuDisplay();
    }
  }

  if (inMenu && menuLevel != 4 && (millis() - lastActivityTime > MENU_TIMEOUT)) { inMenu = false; menuLevel = 0; showMainScreen(); }

  if (!inMenu) {
    if (systemState == ALARM_DISARMED || systemState == ALARM_EXIT_DELAY) {
      digitalWrite(OrnLed, LOW); digitalWrite(BlueLed, HIGH); if (systemState != ALARM_TRIGGERED) digitalWrite(RedLed, HIGH);
    } else if (systemState == ALARM_ARMED) {
      digitalWrite(OrnLed, HIGH); digitalWrite(BlueLed, LOW); digitalWrite(RedLed, HIGH);
    }

    if (systemState == ALARM_EXIT_DELAY) {
      unsigned long elapsed = millis() - armStartTime;
      if (elapsed >= (unsigned long)exitDelay * 1000UL) { systemState = ALARM_ARMED; updateLEDDisplay(1); showMainScreen(); }
      else {
        unsigned long remaining = ((unsigned long)exitDelay * 1000UL - elapsed) / 1000UL;
        LCD_WriteCommand4Bits(0xC0); LCD_WriteText((char*)"Czas: ");
        LCD_WriteData4Bits((remaining / 10) + '0'); LCD_WriteData4Bits((remaining % 10) + '0'); LCD_WriteText((char*)" sek");
      }
    } else if (systemState == ALARM_ARMED || systemState == ALARM_DISARMED) {
      byte sensorData = readSensors(); bool anyViolated = false; byte lastV = 0;
      for (int i = 0; i < 8; i++) {
        if (sensors[i].active && bitRead(sensorData, sensors[i].pin) == HIGH) {
          sensors[i].violated = true; anyViolated = true; lastV = i;
        }
      }
      if (anyViolated) {
        if (systemState == ALARM_ARMED) {
          alarm_sense++; LCD_WriteCommand4Bits(0x80); LCD_WriteText((char*)"NARUSZONY: S"); LCD_WriteData4Bits(lastV + 1 + '0');
          LCD_WriteCommand4Bits(0xC0); LCD_WriteText((char*)"Ruchy: "); LCD_WriteData4Bits(alarm_sense + '0');
          updateLEDDisplay(4); delay(1000); 
          if (alarm_sense >= 3) { systemState = ALARM_TRIGGERED; triggeringSensorIndex = lastV; updateLEDDisplay(3); showMainScreen(); }
          else { updateLEDDisplay(1); showMainScreen(); }
        } else { updateLEDDisplay(4); delay(200); updateLEDDisplay(0); }
      }
    }
    if (systemState == ALARM_TRIGGERED) {
      digitalWrite(OrnLed, HIGH); digitalWrite(BlueLed, HIGH);
      unsigned long currentMillis = millis();
      if (currentMillis - sirenLastToggle >= 300) {
        sirenLastToggle = currentMillis; sirenState = !sirenState;
        if (sirenState) { digitalWrite(RedLed, LOW); tone(BUZZER, 800); }
        else { digitalWrite(RedLed, HIGH); tone(BUZZER, 600); }
      }
    }
  }
}
