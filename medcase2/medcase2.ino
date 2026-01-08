/*
  Smart Medication Reminder Case
  FINAL PAPER-ALIGNED VERSION
*/

#include <Wire.h>
#include <RTClib.h>
#include <ESP32Servo.h>

// ================= PIN DEFINITIONS =================
#define SERVO_AM     13
#define SERVO_NOON   12
#define SERVO_PM     14

#define TOUCH_AM     27
#define TOUCH_NOON   26
#define TOUCH_PM     25

#define IR_AM        33
#define IR_NOON      32
#define IR_PM        35

#define LED_PIN      2
#define BUZZER_PIN   4

#define RTC_SDA      21
#define RTC_SCL      22

// ================= SERVO POSITIONS =================
#define LOCK_POS     0
#define UNLOCK_POS   90

// ================= OBJECTS =================
RTC_DS3231 rtc;
Servo servoAM, servoNOON, servoPM;

// ================= FIXED TIME WINDOWS =================
// (Later replaceable via backend)
int amStartH = 8,  amStartM = 0;
int amEndH   = 8,  amEndM   = 30;

int noonStartH = 12, noonStartM = 0;
int noonEndH   = 12, noonEndM   = 30;

int pmStartH = 18, pmStartM = 0;
int pmEndH   = 18, pmEndM   = 30;

// ================= STATE FLAGS =================
bool usedAM = false;
bool usedNOON = false;
bool usedPM = false;

bool missedAM = false;
bool missedNOON = false;
bool missedPM = false;

// ================= HELPERS =================
bool withinWindow(DateTime now, int sh, int sm, int eh, int em) {
  int nowM = now.hour() * 60 + now.minute();
  int sM = sh * 60 + sm;
  int eM = eh * 60 + em;
  return (nowM >= sM && nowM <= eM);
}

void beep(int n) {
  for (int i = 0; i < n; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(120);
    digitalWrite(BUZZER_PIN, LOW);
    delay(120);
  }
}

void unlockThenLock(Servo &s) {
  s.write(UNLOCK_POS);
  delay(3000);
  s.write(LOCK_POS);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Wire.begin(RTC_SDA, RTC_SCL);
  rtc.begin();

  pinMode(TOUCH_AM, INPUT);
  pinMode(TOUCH_NOON, INPUT);
  pinMode(TOUCH_PM, INPUT);

  pinMode(IR_AM, INPUT);
  pinMode(IR_NOON, INPUT);
  pinMode(IR_PM, INPUT);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  servoAM.attach(SERVO_AM);
  servoNOON.attach(SERVO_NOON);
  servoPM.attach(SERVO_PM);

  servoAM.write(LOCK_POS);
  servoNOON.write(LOCK_POS);
  servoPM.write(LOCK_POS);

  Serial.println("Smart Pill Reminder – Final Firmware");
}

// ================= LOOP =================
void loop() {
  DateTime now = rtc.now();

  bool amActive   = withinWindow(now, amStartH, amStartM, amEndH, amEndM);
  bool noonActive = withinWindow(now, noonStartH, noonStartM, noonEndH, noonEndM);
  bool pmActive   = withinWindow(now, pmStartH, pmStartM, pmEndH, pmEndM);

  digitalWrite(LED_PIN, amActive || noonActive || pmActive);

  // ---------- AM ----------
  if (amActive && !usedAM && digitalRead(TOUCH_AM)) {
    usedAM = true;
    Serial.println("[AM] Touch accepted");
    unlockThenLock(servoAM);
  }

  if (!amActive && !usedAM && !missedAM &&
      (now.hour()*60 + now.minute() >
       amEndH*60 + amEndM)) {
    missedAM = true;
    Serial.println("[AM] Dose missed");
  }

  if (digitalRead(IR_AM) && !amActive) {
    Serial.println("[AM] Unauthorized interaction");
    beep(3);
  }

  // ---------- NOON ----------
  if (noonActive && !usedNOON && digitalRead(TOUCH_NOON)) {
    usedNOON = true;
    Serial.println("[NOON] Touch accepted");
    unlockThenLock(servoNOON);
  }

  if (!noonActive && !usedNOON && !missedNOON &&
      (now.hour()*60 + now.minute() >
       noonEndH*60 + noonEndM)) {
    missedNOON = true;
    Serial.println("[NOON] Dose missed");
  }

  if (digitalRead(IR_NOON) && !noonActive) {
    Serial.println("[NOON] Unauthorized interaction");
    beep(3);
  }

  // ---------- PM ----------
  if (pmActive && !usedPM && digitalRead(TOUCH_PM)) {
    usedPM = true;
    Serial.println("[PM] Touch accepted");
    unlockThenLock(servoPM);
  }

  if (!pmActive && !usedPM && !missedPM &&
      (now.hour()*60 + now.minute() >
       pmEndH*60 + pmEndM)) {
    missedPM = true;
    Serial.println("[PM] Dose missed");
  }

  if (digitalRead(IR_PM) && !pmActive) {
    Serial.println("[PM] Unauthorized interaction");
    beep(3);
  }

  delay(250);
}
