#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

// إعدادات WiFi - قم بتغييرها حسب شبكتك
const char* ssid = "MSR-3";
const char* password = "60006000";

// عنوان السيرفر - قم بتغييره حسب عنوان IP لجهاز الكمبيوتر
// استخدم ipconfig في PowerShell لمعرفة عنوان IP
const char* serverURL = "http://192.168.110.235:3000/recognize";

// إعدادات ESP32-CAM (AI-Thinker)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ملاحظة: تم إلغاء استخدام شاشة LCD على ESP32-CAM
// سيتم عرض النتائج على ESP32 منفصل

// متغيرات
unsigned long lastCaptureTime = 0;
const unsigned long captureInterval = 10000; // التقاط صورة كل 10 ثواني

void setup() {
  Serial.begin(115200);
  Serial.println("بدء تشغيل ESP32-CAM...");

  Serial.println("ESP32-CAM Camera Only Mode");
  Serial.println("LCD will be shown on separate ESP32");

  delay(500);

  // تهيئة الكاميرا
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // دقة الصورة - يمكن تغييرها حسب الحاجة
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // تهيئة الكاميرا
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ خطأ في تهيئة الكاميرا: 0x%x\n", err);
    Serial.println("تحقق من توصيل الكاميرا وأعد التشغيل");
    while(1) {
      delay(1000);
    }
  }

  Serial.println("تم تهيئة الكاميرا بنجاح");

  // الاتصال بشبكة WiFi
  WiFi.begin(ssid, password);
  Serial.print("جارٍ الاتصال بشبكة WiFi");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("تم الاتصال بشبكة WiFi!");
    Serial.print("عنوان IP للكاميرا: ");
    Serial.println(WiFi.localIP());
    Serial.println("جاهز لالتقاط الصور...");
  } else {
    Serial.println("فشل الاتصال بشبكة WiFi!");
    Serial.println("تحقق من الإعدادات وأعد التشغيل");
    while(1) {
      delay(1000);
    }
  }

  delay(1000);
}

void loop() {
  // التحقق من الاتصال بشبكة WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("انقطع الاتصال بشبكة WiFi! إعادة الاتصال...");
    WiFi.reconnect();
    delay(5000);
    return;
  }

  // التقاط صورة كل فترة زمنية محددة
  unsigned long currentTime = millis();
  if (currentTime - lastCaptureTime >= captureInterval) {
    lastCaptureTime = currentTime;

    Serial.println("=== جارٍ التقاط صورة جديدة ===");
    captureAndSendImage();
  }

  delay(100);
}

void captureAndSendImage() {
  Serial.println("التقاط صورة...");

  // التقاط صورة
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ فشل التقاط الصورة");
    return;
  }

  // التحقق من أن الصورة صالحة (JPEG يبدأ بـ FF D8)
  if (fb->len < 2 || fb->buf[0] != 0xFF || fb->buf[1] != 0xD8) {
    Serial.println("❌ الصورة غير صالحة (ليست JPEG)");
    esp_camera_fb_return(fb);
    return;
  }

  Serial.printf("✓ تم التقاط صورة بحجم: %d bytes\n", fb->len);
  Serial.println("إرسال إلى السيرفر...");

  // إرسال الصورة إلى السيرفر مع إعادة المحاولة
  int maxRetries = 3;
  bool success = false;

  for (int attempt = 1; attempt <= maxRetries && !success; attempt++) {
    if (attempt > 1) {
      Serial.printf("محاولة %d من %d...\n", attempt, maxRetries);
      delay(1000);
    }

    HTTPClient http;
    http.begin(serverURL);
    http.addHeader("Content-Type", "image/jpeg");
    http.setTimeout(15000);
    http.setConnectTimeout(5000);

    int httpResponseCode = http.POST(fb->buf, fb->len);

    if (httpResponseCode > 0) {
      Serial.printf("✓ رمز الاستجابة: %d\n", httpResponseCode);

      if (httpResponseCode == 200) {
        String response = http.getString();
        Serial.println("✓ تم إرسال الصورة بنجاح");
        Serial.println("النتيجة: " + response);
        Serial.println("=============================\n");
        success = true;
      } else {
        String errorResponse = http.getString();
        Serial.println("❌ خطأ من السيرفر - Code: " + String(httpResponseCode));
        Serial.println("الرسالة: " + errorResponse);

        if (httpResponseCode == 400 || httpResponseCode == 500) {
          break;
        }
      }
    } else {
      Serial.printf("❌ خطأ في الإرسال: %s\n", http.errorToString(httpResponseCode).c_str());
      Serial.printf("   رمز الخطأ: %d\n", httpResponseCode);
    }

    http.end();
  }

  if (!success) {
    Serial.println("❌ فشل إرسال الصورة بعد جميع المحاولات");
  }

  esp_camera_fb_return(fb);
}
