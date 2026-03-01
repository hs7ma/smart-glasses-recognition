/*
 * =====================================================
 *  ESP32 + حساس مسافة Ultrasonic + مشغل MP3 (HW-311)
 *  نظام النظارات الذكية للتعرف على الأشياء
 * =====================================================
 *
 * الوظيفة:
 *   1. حساس المسافة يكتشف جسم قريب (< 50 سم)
 *   2. يرسل طلب التقاط صورة للسيرفر
 *   3. يجلب نتيجة التعرف من السيرفر
 *   4. يشغل الملف الصوتي العربي المناسب عبر HW-311
 *
 * التوصيلات:
 *   ┌─────────────────────────────────────────────────┐
 *   │  ESP32          →  Ultrasonic Sensor            │
 *   │  GPIO 12        →  TRIG                         │
 *   │  GPIO 13        →  ECHO                         │
 *   │  5V             →  VCC                          │
 *   │  GND            →  GND                          │
 *   ├─────────────────────────────────────────────────┤
 *   │  ESP32          →  HW-311 MP3 Player            │
 *   │  GPIO 17 (TX2)  →  RX  (عبر مقاومة 1KΩ)       │
 *   │  GPIO 16 (RX2)  →  TX                           │
 *   │  5V             →  VCC                          │
 *   │  GND            →  GND                          │
 *   ├─────────────────────────────────────────────────┤
 *   │  HW-311         →  مكبر صوت / سماعة            │
 *   │  SPK1           →  Speaker +                    │
 *   │  SPK2           →  Speaker -                    │
 *   └─────────────────────────────────────────────────┘
 *
 * ملاحظات:
 *   - بطاقة SD بتنسيق FAT32
 *   - ملفات MP3 في المجلد الجذر: 001.mp3 - 080.mp3
 *   - HW-311 يستخدم بروتوكول UART بسرعة 9600 baud
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// =====================================================
//  إعدادات WiFi
// =====================================================
const char* ssid = "MSR3";
const char* password = "60006000";

// =====================================================
//  إعدادات السيرفر
// =====================================================
const char* serverURL  = "http://192.168.110.235:3000/latest";
const char* triggerURL = "http://192.168.110.235:3000/trigger";

// =====================================================
//  إعدادات حساس المسافة Ultrasonic
// =====================================================
#define TRIG_PIN 18
#define ECHO_PIN 19
const float TRIGGER_DISTANCE = 50.0;  // سنتيمتر

// =====================================================
//  إعدادات مشغل MP3 (HW-311 / GD3300B)
// =====================================================
#define MP3_SERIAL    Serial2       // UART2: TX=GPIO17, RX=GPIO16
#define MP3_BAUD      9600
#define MP3_VOLUME    25            // مستوى الصوت (0-30)

// أوامر بروتوكول HW-311
#define CMD_PLAY_TRACK  0x03
#define CMD_SET_VOLUME  0x06
#define CMD_STOP        0x16
#define CMD_RESET       0x0C
#define CMD_SET_SOURCE  0x09
#define SOURCE_SD_CARD  0x02

// =====================================================
//  إعدادات التوقيت
// =====================================================
const unsigned long CHECK_INTERVAL    = 2000;   // التحقق من النتائج كل 2 ثانية
const unsigned long TRIGGER_COOLDOWN  = 5000;   // انتظار 5 ثوانٍ بين كل طلب التقاط
const unsigned long PLAY_COOLDOWN     = 3000;   // انتظار 3 ثوانٍ بين كل تشغيل صوت

// =====================================================
//  جدول ربط الأشياء بأرقام ملفات MP3
//  يطابق ملفات 001.mp3 - 080.mp3 على بطاقة SD
// =====================================================
struct ObjectTrack {
  const char* name;
  uint8_t track;
};

const ObjectTrack OBJECT_TRACKS[] = {
  {"person",         1},  {"bicycle",        2},  {"car",            3},
  {"motorcycle",     4},  {"airplane",       5},  {"bus",            6},
  {"train",          7},  {"truck",          8},  {"boat",           9},
  {"traffic light", 10},  {"fire hydrant",  11},  {"stop sign",     12},
  {"parking meter", 13},  {"bench",         14},  {"bird",          15},
  {"cat",           16},  {"dog",           17},  {"horse",         18},
  {"sheep",         19},  {"cow",           20},  {"elephant",      21},
  {"bear",          22},  {"zebra",         23},  {"giraffe",       24},
  {"backpack",      25},  {"umbrella",      26},  {"handbag",       27},
  {"tie",           28},  {"suitcase",      29},  {"frisbee",       30},
  {"skis",          31},  {"snowboard",     32},  {"sports ball",   33},
  {"kite",          34},  {"baseball bat",  35},  {"baseball glove",36},
  {"skateboard",    37},  {"surfboard",     38},  {"tennis racket", 39},
  {"bottle",        40},  {"wine glass",    41},  {"cup",           42},
  {"fork",          43},  {"knife",         44},  {"spoon",         45},
  {"bowl",          46},  {"banana",        47},  {"apple",         48},
  {"sandwich",      49},  {"orange",        50},  {"broccoli",      51},
  {"carrot",        52},  {"hot dog",       53},  {"pizza",         54},
  {"donut",         55},  {"cake",          56},  {"chair",         57},
  {"couch",         58},  {"potted plant",  59},  {"bed",           60},
  {"dining table",  61},  {"toilet",        62},  {"tv",            63},
  {"laptop",        64},  {"mouse",         65},  {"remote",        66},
  {"keyboard",      67},  {"cell phone",    68},  {"microwave",     69},
  {"oven",          70},  {"toaster",       71},  {"sink",          72},
  {"refrigerator",  73},  {"book",          74},  {"clock",         75},
  {"vase",          76},  {"scissors",      77},  {"teddy bear",    78},
  {"hair drier",    79},  {"toothbrush",    80}
};
const int OBJECT_COUNT = sizeof(OBJECT_TRACKS) / sizeof(OBJECT_TRACKS[0]);

// =====================================================
//  متغيرات عامة
// =====================================================
unsigned long lastCheckTime       = 0;
unsigned long lastTriggerTime     = 0;
unsigned long lastPlayTime        = 0;
String        lastTimestampStr     = "";
unsigned long lastDistanceLog     = 0;
String        lastObject          = "";
bool          waitingForResult    = false;
bool          mp3Ready            = false;
int           checkAttempts       = 0;

// =====================================================
//  دوال مشغل MP3 (HW-311)
// =====================================================

// إرسال أمر إلى HW-311 عبر بروتوكول UART
// صيغة الأمر: 7E FF 06 [CMD] 00 [DH] [DL] [CHK_H] [CHK_L] EF
void mp3SendCommand(uint8_t cmd, uint16_t param) {
  uint8_t paramHigh = (param >> 8);
  uint8_t paramLow  = (param & 0xFF);

  // حساب Checksum (مطلوب من GD3300B)
  int16_t checksum = -(0xFF + 0x06 + cmd + 0x00 + paramHigh + paramLow);

  uint8_t buffer[10];
  buffer[0] = 0x7E;                    // Start byte
  buffer[1] = 0xFF;                    // Version
  buffer[2] = 0x06;                    // Length
  buffer[3] = cmd;                     // Command
  buffer[4] = 0x00;                    // Feedback: off
  buffer[5] = paramHigh;               // Parameter high byte
  buffer[6] = paramLow;                // Parameter low byte
  buffer[7] = (checksum >> 8) & 0xFF;  // Checksum high byte
  buffer[8] = checksum & 0xFF;         // Checksum low byte
  buffer[9] = 0xEF;                    // End byte

  Serial.printf("[MP3] >> CMD=0x%02X PARAM=%d | HEX:", cmd, param);
  for (int i = 0; i < 10; i++) Serial.printf(" %02X", buffer[i]);
  Serial.println();

  MP3_SERIAL.write(buffer, 10);
  delay(100);

  // قراءة رد المشغل (إن وجد)
  if (MP3_SERIAL.available()) {
    Serial.print("[MP3] << Response:");
    while (MP3_SERIAL.available()) {
      Serial.printf(" %02X", MP3_SERIAL.read());
    }
    Serial.println();
  }
}

// تهيئة مشغل MP3
void mp3Init() {
  Serial.println("[MP3] ==============================");
  Serial.println("[MP3] Initializing HW-311...");
  Serial.printf("[MP3] UART: Serial2 (RX=GPIO%d, TX=GPIO%d, Baud=%d)\n", 16, 17, MP3_BAUD);

  MP3_SERIAL.begin(MP3_BAUD, SERIAL_8N1, 16, 17);  // RX=16, TX=17
  delay(1000);
  Serial.println("[MP3] [1/5] UART opened");

  // تفريغ أي بيانات قديمة في البفر
  while (MP3_SERIAL.available()) MP3_SERIAL.read();
  Serial.println("[MP3] [2/5] Buffer flushed");

  // إعادة تهيئة المشغل
  Serial.println("[MP3] [3/5] Sending RESET...");
  mp3SendCommand(CMD_RESET, 0);
  delay(3000);  // GD3300B يحتاج 3 ثوانٍ بعد RESET

  // تحديد مصدر الصوت: بطاقة SD
  Serial.println("[MP3] [4/5] Setting source: SD card...");
  mp3SendCommand(CMD_SET_SOURCE, SOURCE_SD_CARD);
  delay(1000);

  // ضبط مستوى الصوت
  Serial.printf("[MP3] [5/5] Setting volume: %d/30...\n", MP3_VOLUME);
  mp3SendCommand(CMD_SET_VOLUME, MP3_VOLUME);
  delay(500);

  mp3Ready = true;
  Serial.println("[MP3] Init complete!");

  // اختبار تشغيل الملف الأول للتحقق
  Serial.println("[MP3] TEST: Playing 001.mp3...");
  mp3SendCommand(CMD_PLAY_TRACK, 1);
  delay(2000);
  mp3SendCommand(CMD_STOP, 0);
  delay(500);

  Serial.println("[MP3] ==============================");
}

// تشغيل ملف MP3 برقم محدد (1-80)
void mp3PlayTrack(uint8_t track, const char* objectName) {
  if (!mp3Ready) {
    Serial.println("[MP3] Player not ready!");
    return;
  }

  if (track < 1 || track > 80) {
    Serial.printf("[MP3] Invalid track: %d\n", track);
    return;
  }

  // منع تشغيل صوت جديد قبل انتهاء المهلة
  unsigned long elapsed = millis() - lastPlayTime;
  if (elapsed < PLAY_COOLDOWN) {
    Serial.printf("[MP3] Cooldown active... (%lu ms left)\n", PLAY_COOLDOWN - elapsed);
    return;
  }

  Serial.println("[MP3] ========= PLAY START =========");
  Serial.printf("[MP3] Step 1: Object name = %s\n", objectName);
  Serial.printf("[MP3] Step 2: Track number = %d\n", track);
  Serial.printf("[MP3] Step 3: File on SD   = %03d.mp3\n", track);
  Serial.printf("[MP3] Step 4: Sending PLAY command (0x03, %d)...\n", track);
  mp3SendCommand(CMD_PLAY_TRACK, track);
  lastPlayTime = millis();

  // انتظار رد المشغل للتحقق من التشغيل
  delay(200);
  if (MP3_SERIAL.available()) {
    Serial.print("[MP3] Step 5: Player response:");
    while (MP3_SERIAL.available()) {
      Serial.printf(" %02X", MP3_SERIAL.read());
    }
    Serial.println(" -> OK");
  } else {
    Serial.println("[MP3] Step 5: No response from player (check wiring!)");
  }
  Serial.println("[MP3] ========= PLAY END ===========");
}

// إيقاف التشغيل
void mp3Stop() {
  mp3SendCommand(CMD_STOP, 0);
}

// ضبط مستوى الصوت (0-30)
void mp3SetVolume(uint8_t vol) {
  if (vol > 30) vol = 30;
  mp3SendCommand(CMD_SET_VOLUME, vol);
  Serial.printf("[MP3] Volume: %d/30\n", vol);
}

// =====================================================
//  دالة البحث عن رقم الملف حسب اسم الشيء
// =====================================================
uint8_t getTrackNumber(const String& objectName) {
  String lowerName = objectName;
  lowerName.toLowerCase();
  lowerName.trim();

  for (int i = 0; i < OBJECT_COUNT; i++) {
    if (lowerName.equals(OBJECT_TRACKS[i].name)) {
      return OBJECT_TRACKS[i].track;
    }
  }

  Serial.printf("[MP3] No track found for: %s\n", objectName.c_str());
  return 0;  // 0 = غير موجود
}

// =====================================================
//  دالة قياس المسافة
// =====================================================
float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) return -1;

  return (duration * 0.0343) / 2.0;
}

// =====================================================
//  دوال التواصل مع السيرفر
// =====================================================

// إرسال طلب التقاط صورة
void sendTrigger() {
  Serial.println("[TRIGGER] Sending capture request...");
  Serial.printf("[TRIGGER] URL: %s\n", triggerURL);

  HTTPClient http;

  if (!http.begin(triggerURL)) {
    Serial.println("[TRIGGER] Connection failed!");
    return;
  }

  http.setTimeout(5000);
  http.setConnectTimeout(3000);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST("{}");

  if (code == 200) {
    String response = http.getString();
    Serial.printf("[TRIGGER] OK! (HTTP %d) Response: %s\n", code, response.c_str());
    checkAttempts = 0;
  } else if (code < 0) {
    Serial.printf("[TRIGGER] Connection error: %s\n", http.errorToString(code).c_str());
  } else {
    Serial.printf("[TRIGGER] HTTP error: %d\n", code);
  }

  http.end();
}

// جلب آخر نتيجة من السيرفر
void checkLatestResult() {
  checkAttempts++;
  Serial.printf("[CHECK] Fetching result (attempt #%d)...\n", checkAttempts);

  HTTPClient http;

  if (!http.begin(serverURL)) {
    Serial.println("[CHECK] Connection failed!");
    return;
  }

  http.setTimeout(10000);
  http.setConnectTimeout(5000);

  int code = http.GET();

  if (code == 200) {
    String response = http.getString();
    Serial.printf("[CHECK] OK! (HTTP %d) Length: %d bytes\n", code, response.length());
    Serial.printf("[CHECK] Raw: %s\n", response.c_str());
    processResult(response);
  } else if (code < 0) {
    Serial.printf("[CHECK] Connection error: %s\n", http.errorToString(code).c_str());
  } else {
    Serial.printf("[CHECK] HTTP error: %d\n", code);
  }

  http.end();
}

// =====================================================
//  معالجة نتيجة التعرف وتشغيل الصوت
// =====================================================
void processResult(const String& jsonResponse) {
  Serial.println("[RESULT] Parsing JSON...");

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, jsonResponse);

  if (error) {
    Serial.printf("[RESULT] JSON parse error: %s\n", error.c_str());
    return;
  }

  const char* objectName = doc["objectEnglish"] | doc["object"] | "Unknown";
  const char* confidence = doc["confidence"] | "0.00";
  unsigned long age = doc["age"] | 0;

  // استخدام timestamp كنص لتجنب مشكلة overflow (Date.now() اكبر من 32-bit)
  String timestampStr = doc["timestamp"].as<String>();

  Serial.printf("[RESULT] Object: %s | Confidence: %s | Age: %lu ms\n", objectName, confidence, age);
  Serial.printf("[RESULT] Timestamp: %s | Last: %s\n", timestampStr.c_str(), lastTimestampStr.c_str());

  // تجاهل النتائج غير الصالحة
  if (strcmp(objectName, "Waiting...") == 0 || strcmp(objectName, "Unknown") == 0) {
    Serial.printf("[RESULT] >> Invalid result (%s), skipping\n", objectName);
    return;
  }

  if (strcmp(objectName, "Nothing") == 0) {
    Serial.println("[RESULT] >> Nothing detected in image");
    lastTimestampStr = timestampStr;
    waitingForResult = false;
    return;
  }

  // تجاهل النتائج القديمة (مقارنة نصية)
  if (timestampStr == lastTimestampStr) {
    Serial.println("[RESULT] >> Same timestamp, skipping");
    return;
  }

  // نتيجة جديدة!
  lastTimestampStr = timestampStr;
  lastObject = objectName;
  waitingForResult = false;

  Serial.println();
  Serial.println("==========================================");
  Serial.println("          NEW DETECTION!");
  Serial.printf( "  Object:  %s\n", objectName);
  Serial.printf( "  Score:   %s\n", confidence);

  // البحث عن رقم الملف الصوتي
  uint8_t track = getTrackNumber(String(objectName));

  if (track > 0) {
    Serial.printf( "  Track:   %03d.mp3\n", track);
    Serial.println("  Status:  Playing audio...");
    Serial.println("==========================================");
    mp3PlayTrack(track, objectName);
  } else {
    Serial.println("  Track:   NOT FOUND!");
    Serial.println("==========================================");
  }
}

// =====================================================
//  Setup
// =====================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n==========================================");
  Serial.println("  Smart Glasses System");
  Serial.println("  ESP32 + Ultrasonic + HW-311 MP3");
  Serial.println("==========================================\n");

  // تهيئة حساس المسافة
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  Serial.printf("[SENSOR] Ready (TRIG=%d, ECHO=%d)\n", TRIG_PIN, ECHO_PIN);

  // تهيئة مشغل MP3
  mp3Init();

  // الاتصال بشبكة WiFi
  WiFi.begin(ssid, password);
  Serial.print("[WiFi] Connecting");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] Server: %s\n", serverURL);
  } else {
    Serial.println("\n[WiFi] Connection FAILED!");
    while (1) delay(1000);
  }

  Serial.println("\n[SYSTEM] Ready!\n");
}

// =====================================================
//  Loop الرئيسية
// =====================================================
void loop() {
  // التحقق من اتصال WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Disconnected! Reconnecting...");
    WiFi.reconnect();
    delay(5000);
    return;
  }

  unsigned long now = millis();

  // قياس المسافة
  float distance = measureDistance();

  // طباعة المسافة كل 3 ثوانٍ للمراقبة
  if (now - lastDistanceLog >= 3000) {
    lastDistanceLog = now;
    if (distance > 0) {
      Serial.printf("[LOOP] Distance: %.1f cm %s | waiting=%s\n",
                    distance,
                    (distance <= TRIGGER_DISTANCE) ? "<< CLOSE!" : "",
                    waitingForResult ? "true" : "false");
    } else {
      Serial.println("[LOOP] Distance: no reading (out of range)");
    }
  }

  // اكتشاف جسم قريب
  if (distance > 0 && distance <= TRIGGER_DISTANCE) {
    if (now - lastTriggerTime >= TRIGGER_COOLDOWN) {
      lastTriggerTime = now;
      Serial.println();
      Serial.println("========================================");
      Serial.printf("[SENSOR] Object detected at %.1f cm!\n", distance);
      Serial.println("========================================");
      sendTrigger();
      waitingForResult = true;
    }
  }

  // جلب النتائج بشكل دوري
  if (waitingForResult && (now - lastCheckTime >= CHECK_INTERVAL)) {
    lastCheckTime = now;
    checkLatestResult();
  }

  delay(100);
}
