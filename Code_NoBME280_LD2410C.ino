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
const int trig1 = 5;  const int echo1 = 18; // อัลตราโซนิกหน้ารถ
const int trig2 = 16; const int echo2 = 17; // อัลตราโซนิกในถัง
const int servoPin = 13;
const int buzzer = 4;
const int ledRed = 15;
const int ledGreen = 2;

// --- ขา L298N ---
const int IN1 = 27; // ล้อซ้าย เดินหน้า
const int IN2 = 26; // ล้อซ้าย ถอยหลัง
const int IN3 = 25; // ล้อขวา เดินหน้า
const int IN4 = 33; // ล้อขวา ถอยหลัง

// --- ขา DHT11 ---
#define DHTPIN 21       // ต่อสาย DATA ของ DHT11 เข้าขา D21
#define DHTTYPE DHT11   // เลือกชนิดเซนเซอร์
DHT dht(DHTPIN, DHTTYPE);

Servo myServo;
BlynkTimer timer;

// --- ตัวแปร State Machine ---
// ตัดโหมดที่ไม่ใช้ออก เหลือแค่ STANDBY, CONTROL, SERVICE(เปิดฝา), FULL_BIN
enum State { STANDBY, CONTROL, SERVICE, FULL_BIN };
State currentState = STANDBY;
State previousState = STANDBY;

unsigned long lastPrintTime = 0;
unsigned long serviceTimer = 0; 
unsigned long ignoreSensorUntil = 0;
bool isLidOpen = false;        

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

// ฟังก์ชันปริ้นล็อก ส่งเข้าทั้ง Serial และแอป Blynk (V5)
void printLog(String msg) {
  Serial.print(msg);
  Blynk.virtualWrite(V5, msg + "\n"); 
}

void motorStop()     { digitalWrite(IN1, LOW);  digitalWrite(IN2, LOW);  digitalWrite(IN3, LOW);  digitalWrite(IN4, LOW);  }
void motorForward()  { digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);  }
void motorBackward() { digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH); digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH); }
void motorLeft()     { digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH); digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);  }
void motorRight()    { digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH); }

// --- ฟังก์ชันส่งค่า DHT11 เข้า Blynk ---
void sendSensorData() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  
  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }
  Blynk.virtualWrite(V0, t); // ส่งอุณหภูมิไป V0
  Blynk.virtualWrite(V1, h); // ส่งความชื้นไป V1
}

// ==========================================
// --- รับคำสั่งจาก Blynk App ---
// ==========================================

// สวิตช์เลือกโหมด (V2)
BLYNK_WRITE(V2) { 
  if(param.asInt() == 1) {
    currentState = CONTROL; // เปลี่ยนเป็นโหมดบังคับมือ
    printLog(">> MODE: MANUAL CONTROL <<");
    motorStop();
    if(isLidOpen) { myServo.write(0); isLidOpen = false; } // ปิดฝาก่อนวิ่ง
  } else {
    currentState = STANDBY; // กลับโหมดรอรับขยะ
    printLog(">> MODE: STANDBY <<");
    motorStop();
  }
}

// ปุ่มควบคุมรถ (ทำงานเฉพาะตอนอยู่โหมด CONTROL เท่านั้น)
BLYNK_WRITE(V3) { if(currentState == CONTROL) { if(param.asInt() == 1) motorForward();  else motorStop(); } } // หน้า
BLYNK_WRITE(V4) { if(currentState == CONTROL) { if(param.asInt() == 1) motorBackward(); else motorStop(); } } // หลัง
BLYNK_WRITE(V6) { if(currentState == CONTROL) { if(param.asInt() == 1) motorLeft();     else motorStop(); } } // ซ้าย
BLYNK_WRITE(V7) { if(currentState == CONTROL) { if(param.asInt() == 1) motorRight();    else motorStop(); } } // ขวา


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
  
  // ให้ฟังก์ชัน sendSensorData ทำงานทุกๆ 2 วินาที
  timer.setInterval(2000L, sendSensorData);

  // สั่งให้ Blynk รีเซ็ตสวิตช์โหมดเป็น Standby ตอนเปิดเครื่อง
  Blynk.virtualWrite(V2, 0); 

  Serial.println("\n--- AI System Ready (Standby & Control Mode) ---");
}

void loop() {
  Blynk.run(); 
  timer.run(); 

  long distFront = getDist(trig1, echo1);
  long distInside = getDist(trig2, echo2);
  unsigned long currentMillis = millis();

  // เมินค่าเซนเซอร์อัลตราโซนิกตอนเพิ่งปิดฝา
  if (currentMillis < ignoreSensorUntil) {
    distFront = 999; 
  }

  // --- 1. เช็กขยะเต็ม (เช็กตลอดเวลาไม่ว่าจะอยู่โหมดไหน) ---
  if (distInside < 1 && currentState != FULL_BIN && currentState != SERVICE) {
    printLog(">> WARNING: BIN IS FULL! <<");
    currentState = FULL_BIN;
    isLidOpen = false; 
    motorStop(); 
  }

  // --- 2. State Logic หลัก ---
  switch (currentState) {
    
    case STANDBY:
      digitalWrite(ledGreen, HIGH); digitalWrite(ledRed, LOW);
      // ในโหมดนี้ รถจะไม่วิ่ง แต่จะรอเปิดฝาถ้ามีคนมาใกล้
      if (distFront < 15) {
        printLog(">> TARGET LOCKED: OPENING BIN <<");
        beep(1, 100); 
        previousState = STANDBY; 
        isLidOpen = false;
        currentState = SERVICE;
      }
      break;

    case CONTROL:
      // โหมดบังคับมือ (ไฟกะพริบสลับสีให้รู้ว่าพร้อมซิ่ง)
      digitalWrite(ledGreen, (currentMillis/500)%2); 
      digitalWrite(ledRed, !((currentMillis/500)%2));
      // **การขยับรถ ถูกสั่งงานผ่าน BLYNK_WRITE(V3-V7) ด้านบนแล้ว**
      break;

    case SERVICE:
      // โหมดเปิดฝาทิ้งไว้
      digitalWrite(ledRed, HIGH); digitalWrite(ledGreen, LOW);
      motorStop(); 
      if (!isLidOpen) {
        myServo.write(150); isLidOpen = true;
        serviceTimer = currentMillis; ignoreSensorUntil = currentMillis + 1000; 
      }
      if (currentMillis > ignoreSensorUntil && distFront < 30) serviceTimer = currentMillis; 

      if (currentMillis - serviceTimer > 3000) {
        beep(1, 100); myServo.write(0); isLidOpen = false;
        ignoreSensorUntil = currentMillis + 3000; 
        printLog(">> DONE: BACK TO STANDBY <<");
        currentState = previousState; // กลับไปโหมดก่อนหน้า
      }
      break;

    case FULL_BIN: {
      motorStop(); 
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
        digitalWrite(buzzer, LOW); myServo.write(0); isLidOpen = false;
        printLog(">> RE-CHECKING BIN LEVEL... <<");
        delay(2000); 
        long checkDist = getDist(trig2, echo2);
        if (checkDist > 12 && checkDist != 999) { 
          printLog(">> BIN CLEARED! <<");
          ignoreSensorUntil = currentMillis + 3000; 
          currentState = STANDBY;
          Blynk.virtualWrite(V2, 0); // อัปเดตสวิตช์ในแอพให้กลับมาที่ Standby
        }
      }
      break;
    }
  }

  // --- 3. Serial Log (ส่งค่าทุกๆ 1 วินาที) ---
  if (currentMillis - lastPrintTime > 1000) {
    String sName = (currentState == STANDBY) ? "STANDBY" : (currentState == CONTROL) ? "CONTROL" : (currentState == SERVICE) ? "SERVICE" : "FULL_BIN";
    String output = "[" + sName + "] F:" + String(distFront) + "cm | B:" + String(distInside) + "cm";
    Serial.println(output); 
    lastPrintTime = currentMillis;
  }
}