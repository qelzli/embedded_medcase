/*
  Smart Medication Reminder Case (ESP32)
  FINAL THESIS / PROTOTYPE VERSION
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "RTClib.h"
#include <ESP32Servo.h>

// -------------------- WiFi + Server --------------------
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* SERVER_BASE_URL = "http://192.168.1.25:8000";
const char* DEVICE_API_KEY  = "change-me-to-a-long-random-string";

// -------------------- Pins --------------------
// Touch sensors
const int PIN_TOUCH_AM   = 14;
const int PIN_TOUCH_NOON = 27;
const int PIN_TOUCH_PM   = 26;

// IR sensors
const int PIN_IR_AM   = 33;
const int PIN_IR_NOON = 32;
const int PIN_IR_PM   = 35;

// Servos
const int PIN_SERVO_AM   = 18;
const int PIN_SERVO_NOON = 19;
const int PIN_SERVO_PM   = 21;

// Indicators
const int PIN_BUZZER = 25;
const int PIN_LED    = 2;

// Servo positions
const int SERVO_LOCKED_POS   = 10;
const int SERVO_UNLOCKED_POS = 90;

// -------------------- RTC --------------------
RTC_DS3231 rtc;

// -------------------- Data Structures --------------------
struct Window {
  bool enabled;
  int startMin;
  int endMin;
};

Window winAM   = {false, 0, 0};
Window winNOON = {false, 0, 0};
Window winPM   = {false, 0, 0};

// Flags
bool completedAM=false, completedNOON=false, completedPM=false;
bool missedAM=false, missedNOON=false, missedPM=false;
bool usedTouchAM=false, usedTouchNOON=false, usedTouchPM=false;
bool servoUnlockedAM=false, servoUnlockedNOON=false, servoUnlockedPM=false;

// Touch edge detection
bool lastTouchAM=false, lastTouchNOON=false, lastTouchPM=false;

// Window alerts
bool alertedAM=false, alertedNOON=false, alertedPM=false;

// Servo timers
unsigned long unlockStartAM=0, unlockStartNOON=0, unlockStartPM=0;
const unsigned long SERVO_UNLOCK_DURATION = 5000;

// Servos
Servo servoAM, servoNOON, servoPM;

// Timers
unsigned long lastScheduleFetchMs=0;
unsigned long lastStatusPostMs=0;
const unsigned long SCHEDULE_FETCH_INTERVAL_MS = 10UL * 60UL * 1000UL;
const unsigned long STATUS_POST_INTERVAL_MS    = 5UL * 1000UL;

String lastDate="";

// -------------------- Helpers --------------------
void beep(int times, int onMs=80, int offMs=60) {
  for (int i=0;i<times;i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(onMs);
    digitalWrite(PIN_BUZZER, LOW);
    delay(offMs);
  }
}

void setLocked(Servo &s)   { s.write(SERVO_LOCKED_POS); }
void setUnlocked(Servo &s) { s.write(SERVO_UNLOCKED_POS); }

int nowMinutes() {
  DateTime now = rtc.now();
  return now.hour()*60 + now.minute();
}

bool isWithinWindow(const Window &w) {
  if (!w.enabled) return false;
  int m = nowMinutes();
  return (m >= w.startMin && m <= w.endMin);
}

bool isWindowExpired(const Window &w) {
  if (!w.enabled) return false;
  return nowMinutes() > w.endMin;
}

String todayYmd() {
  DateTime now = rtc.now();
  char buf[11];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", now.year(), now.month(), now.day());
  return String(buf);
}

void resetDailyFlags() {
  completedAM=completedNOON=completedPM=false;
  missedAM=missedNOON=missedPM=false;
  usedTouchAM=usedTouchNOON=usedTouchPM=false;
  servoUnlockedAM=servoUnlockedNOON=servoUnlockedPM=false;
  alertedAM=alertedNOON=alertedPM=false;
}

// -------------------- Networking --------------------
bool httpPostJson(const String &path, const String &json) {
  if (WiFi.status()!=WL_CONNECTED) return false;
  HTTPClient http;
  http.begin(SERVER_BASE_URL + path);
  http.addHeader("Content-Type","application/json");
  http.addHeader("X-DEVICE-KEY",DEVICE_API_KEY);
  int code=http.POST(json);
  http.end();
  return code>=200 && code<300;
}

void postEvent(const String &comp, const String &type, const String &msg) {
  StaticJsonDocument<256> doc;
  doc["compartment"]=comp;
  doc["event_type"]=type;
  doc["message"]=msg;
  String json;
  serializeJson(doc,json);
  httpPostJson("/api/device/event",json);
}

// -------------------- Window Alert --------------------
void handleWindowAlert(const char* label, const Window &w, bool &alerted) {
  if (!w.enabled) return;
  if (isWithinWindow(w) && !alerted) {
    digitalWrite(PIN_LED, HIGH);
    beep(2);
    alerted = true;
    postEvent(label,"window_open","Medication window started");
  }
  if (isWindowExpired(w)) {
    digitalWrite(PIN_LED, LOW);
  }
}

// -------------------- Compartment Logic --------------------
void handleCompartment(const char* label,
                       Window &w,
                       bool &completed,
                       bool &missed,
                       bool &usedTouch,
                       bool &servoUnlocked,
                       bool &lastTouch,
                       unsigned long &unlockStart,
                       int touchPin,
                       int irPin,
                       Servo &servo) {

  if (!w.enabled || completed) return;

  if (!missed && isWindowExpired(w)) {
    missed = true;
    postEvent(label,"dose_missed","Dose window expired");
    beep(2);
    return;
  }

  // Auto-lock check
  if (servoUnlocked && millis() - unlockStart >= SERVO_UNLOCK_DURATION) {
    setLocked(servo);
    servoUnlocked = false;
    digitalWrite(PIN_LED, LOW);

    if (digitalRead(irPin)==LOW) {
      completed = true;
      postEvent(label,"dose_completed","Pill removed");
      beep(3);
    } else {
      postEvent(label,"pill_still_present","Pill still present");
      beep(2);
    }
  }

  if (!isWithinWindow(w) || usedTouch) return;

  bool currentTouch = digitalRead(touchPin)==HIGH;
  bool risingEdge = currentTouch && !lastTouch;
  lastTouch = currentTouch;

  if (risingEdge) {
    usedTouch = true;
    setUnlocked(servo);
    servoUnlocked = true;
    unlockStart = millis();
    digitalWrite(PIN_LED, HIGH);
    postEvent(label,"touch_accepted","Touch accepted");
    beep(1);
  }
}

// -------------------- Setup --------------------
void setup() {
  Serial.begin(115200);

  pinMode(PIN_TOUCH_AM,INPUT);
  pinMode(PIN_TOUCH_NOON,INPUT);
  pinMode(PIN_TOUCH_PM,INPUT);
  pinMode(PIN_IR_AM,INPUT);
  pinMode(PIN_IR_NOON,INPUT);
  pinMode(PIN_IR_PM,INPUT);
  pinMode(PIN_BUZZER,OUTPUT);
  pinMode(PIN_LED,OUTPUT);

  servoAM.attach(PIN_SERVO_AM);
  servoNOON.attach(PIN_SERVO_NOON);
  servoPM.attach(PIN_SERVO_PM);

  setLocked(servoAM);
  setLocked(servoNOON);
  setLocked(servoPM);

  Wire.begin();
  rtc.begin();

  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  while (WiFi.status()!=WL_CONNECTED) delay(300);

  lastDate = todayYmd();
  resetDailyFlags();
}

// -------------------- Loop --------------------
void loop() {
  if (todayYmd()!=lastDate) {
    lastDate=todayYmd();
    resetDailyFlags();
  }

  handleWindowAlert("AM",winAM,alertedAM);
  handleWindowAlert("NOON",winNOON,alertedNOON);
  handleWindowAlert("PM",winPM,alertedPM);

  handleCompartment("AM",winAM,completedAM,missedAM,usedTouchAM,
                    servoUnlockedAM,lastTouchAM,unlockStartAM,
                    PIN_TOUCH_AM,PIN_IR_AM,servoAM);

  handleCompartment("NOON",winNOON,completedNOON,missedNOON,usedTouchNOON,
                    servoUnlockedNOON,lastTouchNOON,unlockStartNOON,
                    PIN_TOUCH_NOON,PIN_IR_NOON,servoNOON);

  handleCompartment("PM",winPM,completedPM,missedPM,usedTouchPM,
                    servoUnlockedPM,lastTouchPM,unlockStartPM,
                    PIN_TOUCH_PM,PIN_IR_PM,servoPM);

  delay(50);
}
