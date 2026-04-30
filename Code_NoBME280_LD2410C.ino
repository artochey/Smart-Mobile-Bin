// --- การตั้งค่า Blynk (ต้องเอามาจากเว็บ blynk.cloud) ---
#define BLYNK_TEMPLATE_ID "TMPLxxxxxxxxx"
#define BLYNK_TEMPLATE_NAME "AI TrashBin"
#define BLYNK_AUTH_TOKEN "LbQFOwAzl26KoG9h9LhlNhrz-eoCLeX6"

// --- การตั้งค่า WiFi ---
char ssid[] = "iPhone 16 Pro Super Ultra Max";
char pass[] = "12347777";

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include "DHT.h"

// --- ขาอุปกรณ์ ---
const int trig1 = 5;  const int echo1 = 18; 
const int trig2 = 16; const int echo2 = 17; 
const int servoPin = 13;
const int buzzer = 4;
const int ledRed = 15;
const int ledGreen = 2;

// --- ขา L298N ---
const int IN1 = 27; 
const int IN2 = 26; 
const int IN3 = 25; 
const int IN4 = 33; 

#define DHTPIN 21       
#define DHTTYPE DHT11   
DHT dht(DHTPIN, DHTTYPE);

Servo myServo;
BlynkTimer timer;

// --- ตัวแปร State Machine ---
enum State { STANDBY, CONTROL, SERVICE, FULL_BIN };
State currentState = STANDBY;
State previousState = STANDBY;

unsigned long lastPrintTime = 0;
unsigned long serviceTimer = 0; 
unsigned long ignoreSensorUntil = 0;
unsigned long ignoreInsideSensorUntil = 0; 
bool isLidOpen = false;        
bool autoSensorEnabled = true;

// 🌟 ตัวแปรใหม่สำหรับปุ่ม V9 (ขับตอนฝาเปิด)
bool canDriveWhenOpen = false; 

// --- ฟังก์ชันพื้นฐาน ---
long getDist(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long duration = pulseIn(echo, HIGH, 20000);
  return (duration == 0) ? 999 : duration * 0.034 / 2;
}

void beep(int times, int duration) {
  for(int i=0; i<times; i++) {
    digitalWrite(buzzer, HIGH); delay(duration);
    digitalWrite(buzzer, LOW);  if(times>1) delay(duration);
  }
}

void printLog(String msg) {
  Serial.print(msg);
  Blynk.virtualWrite(V5, msg + "\n"); 
}

void motorStop()     { digitalWrite(IN1, LOW);  digitalWrite(IN2, LOW);  digitalWrite(IN3, LOW);  digitalWrite(IN4, LOW);  }
void motorForward()  { digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);  }
void motorBackward() { digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH); digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH); }
void motorLeft()     { digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH); digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);  }
void motorRight()    { digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH); }

void sendSensorData() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  
  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }
  Blynk.virtualWrite(V0, t); 
  Blynk.virtualWrite(V1, h); 
}

// ==========================================
// --- รับคำสั่งจาก Blynk App ---
// ==========================================

// V2: สวิตช์เลือกโหมด Standby / Control
BLYNK_WRITE(V2) { 
  if(param.asInt() == 1) {
    currentState = CONTROL; 
    printLog(">> MODE: MANUAL CONTROL <<");
    motorStop();
    if(isLidOpen) { 
      myServo.write(0); 
      isLidOpen = false; 
      ignoreInsideSensorUntil = millis() + 2000; 
    } 
  } else {
    currentState = STANDBY; 
    printLog(">> MODE: STANDBY <<");
    motorStop();
  }
}

// V8: สวิตช์ ปิด/เปิด เซนเซอร์หน้ารถ
BLYNK_WRITE(V8) { 
  if(param.asInt() == 1) {
    autoSensorEnabled = true;
    printLog(">> FRONT SENSOR: ON <<");
  } else {
    autoSensorEnabled = false; 
    printLog(">> FRONT SENSOR: OFF (BYPASS) <<");
  }
}

// 🌟 V9: สวิตช์เปิดโหมด "ขับรถตอนฝาอ้า"
BLYNK_WRITE(V9) { 
  if(param.asInt() == 1) {
    canDriveWhenOpen = true;
    printLog(">> DRIVE WHEN OPEN: ENABLED <<");
  } else {
    canDriveWhenOpen = false; 
    printLog(">> DRIVE WHEN OPEN: DISABLED <<");
    if(currentState == SERVICE) motorStop(); // ถ้าปิดปุ่มนี้ตอนฝากำลังอ้า ให้เบรกทันที
  }
}

// 🌟 อัปเดตปุ่มควบคุม (V3-V7) ให้เช็กตัวแปร canDriveWhenOpen
BLYNK_WRITE(V3) { if(currentState == CONTROL || (currentState == SERVICE && previousState == CONTROL && canDriveWhenOpen)) { if(param.asInt() == 1) motorForward();  else motorStop(); } }
BLYNK_WRITE(V4) { if(currentState == CONTROL || (currentState == SERVICE && previousState == CONTROL && canDriveWhenOpen)) { if(param.asInt() == 1) motorBackward(); else motorStop(); } }
BLYNK_WRITE(V6) { if(currentState == CONTROL || (currentState == SERVICE && previousState == CONTROL && canDriveWhenOpen)) { if(param.asInt() == 1) motorLeft();     else motorStop(); } }
BLYNK_WRITE(V7) { if(currentState == CONTROL || (currentState == SERVICE && previousState == CONTROL && canDriveWhenOpen)) { if(param.asInt() == 1) motorRight();    else motorStop(); } }


void setup() {
  Serial.begin(115200);
  pinMode(trig1, OUTPUT); pinMode(echo1, INPUT);
  pinMode(trig2, OUTPUT); pinMode(echo2, INPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(ledRed, OUTPUT); pinMode(ledGreen, OUTPUT);
  
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  motorStop(); 
  
  myServo.attach(servoPin, 500, 2400); myServo.write(0);

  Serial.println("Connecting to WiFi & Blynk...");
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  dht.begin();
  
  timer.setInterval(2000L, sendSensorData);

  // ตั้งค่าปุ่มในแอปตอนเปิดเครื่อง
  Blynk.virtualWrite(V2, 0); // อยู่ในโหมด Standby
  Blynk.virtualWrite(V8, 1); // เปิดเซนเซอร์หน้ารถปกติ
  Blynk.virtualWrite(V9, 0); // 🌟 ปิดโหมดขับตอนฝาอ้าไว้ก่อน (เซฟตี้)

  Serial.println("\n--- AI System Ready ---");
}

void loop() {
  Blynk.run(); 
  timer.run(); 

  long distFront = getDist(trig1, echo1);
  long distInside = 999; 
  unsigned long currentMillis = millis();

  if (currentMillis < ignoreSensorUntil) {
    distFront = 999; 
  }

  if (!isLidOpen && currentMillis >= ignoreInsideSensorUntil) {
    distInside = getDist(trig2, echo2);
  }

  // --- 1. เช็กขยะเต็ม ---
  if (distInside != 999 && distInside > 2 && distInside < 5 && currentState != FULL_BIN && currentState != SERVICE) {
    printLog(">> WARNING: BIN IS FULL! <<");
    currentState = FULL_BIN;
    isLidOpen = false; 
    motorStop(); 
  }

  // --- 2. State Logic หลัก ---
  switch (currentState) {
    
    case STANDBY:
      digitalWrite(ledGreen, HIGH); digitalWrite(ledRed, LOW);
      if (distFront < 35 && autoSensorEnabled) {
        printLog(">> TARGET LOCKED: OPENING BIN <<");
        beep(1, 100); 
        previousState = STANDBY; 
        isLidOpen = false;
        currentState = SERVICE;
      }
      break;

    case CONTROL:
      digitalWrite(ledGreen, (currentMillis/500)%2); 
      digitalWrite(ledRed, !((currentMillis/500)%2));
      
      if (distFront < 35 && autoSensorEnabled) {
        motorStop(); // เบรกก่อนเปิดฝา
        printLog(">> TARGET LOCKED: OPENING BIN <<");
        beep(1, 100); 
        previousState = CONTROL; 
        isLidOpen = false;
        currentState = SERVICE;
      }
      break;

    case SERVICE:
      digitalWrite(ledRed, HIGH); digitalWrite(ledGreen, LOW);
      
      // 🌟 ถ้าไม่ได้เปิดปุ่ม V9 ไว้ ให้บังคับเบรกรถเพื่อความปลอดภัย!
      if (!canDriveWhenOpen) {
        motorStop(); 
      }

      if (!isLidOpen) {
        myServo.write(150); isLidOpen = true;
        serviceTimer = currentMillis; ignoreSensorUntil = currentMillis + 1000; 
      }
      if (currentMillis > ignoreSensorUntil && distFront < 35) serviceTimer = currentMillis; 

      if (currentMillis - serviceTimer > 3000) {
        beep(1, 100); 
        myServo.write(0); 
        isLidOpen = false;
        ignoreSensorUntil = currentMillis + 3000; 
        ignoreInsideSensorUntil = currentMillis + 2000; 
        
        String modeName = (previousState == CONTROL) ? "MANUAL CONTROL" : "STANDBY";
        printLog(">> DONE: BACK TO " + modeName + " <<");
        
        currentState = previousState; 
      }
      break;

    case FULL_BIN: {
      motorStop(); // ขยะเต็มต้องบังคับเบรกเสมอ!
      digitalWrite(ledRed, (currentMillis/500)%2); digitalWrite(ledGreen, LOW);
      if (!isLidOpen) {
        myServo.write(150); isLidOpen = true; serviceTimer = currentMillis; 
        printLog(">> ALARM: 15 SECONDS TO CLEAR TRASH <<");
      }
      
      long tFull = currentMillis % 1000;
      if (tFull < 100 || (tFull > 200 && tFull < 300) || (tFull > 400 && tFull < 500)) digitalWrite(buzzer, HIGH); 
      else digitalWrite(buzzer, LOW);

      if (distFront < 30) serviceTimer = currentMillis;

      if (currentMillis - serviceTimer > 15000) {
        digitalWrite(buzzer, LOW); 
        myServo.write(0); 
        isLidOpen = false;
        ignoreInsideSensorUntil = currentMillis + 2000; 
        
        printLog(">> RE-CHECKING BIN LEVEL... <<");
        delay(2000); 
        long checkDist = getDist(trig2, echo2);
        
        if (checkDist >= 8 && checkDist != 999) { 
          printLog(">> BIN CLEARED! <<");
          ignoreSensorUntil = currentMillis + 3000; 
          currentState = STANDBY;
          Blynk.virtualWrite(V2, 0); 
        }
      }
      break;
    }
  }

  if (currentMillis - lastPrintTime > 1000) {
    String sName = (currentState == STANDBY) ? "STANDBY" : (currentState == CONTROL) ? "CONTROL" : (currentState == SERVICE) ? "SERVICE" : "FULL_BIN";
    String sSensor = autoSensorEnabled ? "ON" : "OFF";
    String output = "[" + sName + "] Sensor:" + sSensor + " F:" + String(distFront) + "cm | B:" + String(distInside) + "cm";
    Serial.println(output); 
    lastPrintTime = currentMillis;
  }
}