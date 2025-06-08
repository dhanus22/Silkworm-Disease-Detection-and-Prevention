// ESP32-CAM Streaming with Image Registration and Alert via Telegram using aHash
// Libraries Required:
// - esp_camera.h
// - WiFi.h
// - WebServer.h
// - SPIFFS.h
// - HTTPClient.h

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <HTTPClient.h>

// WiFi credentials
const char* ssid = "SMS";
const char* password = "120120120";

// Telegram bot details
#define TELEGRAM_BOT_TOKEN "7697626875:AAF3n3ieDbH0RKc1v6P-zd5_0Ou-YWcjXP0"
#define TELEGRAM_CHAT_ID "1905510916"

WebServer server(80);
bool imageRegistered = false;

#define PART_BOUNDARY "123456789"

// AI-Thinker camera pin configuration
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

void startCamera() {
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
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 10;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
}

String sendHtml() {
  return R"rawliteral(
  <html><body>
  <h2>ESP32-CAM Face Lock</h2>
  <img src="/stream" width="320" /><br><br>
  <form action="/register" method="POST">
    <input type="submit" value="Register Face" />
  </form>
  </body></html>
  )rawliteral";
}

void handleRoot() {
  server.send(200, "text/html", sendHtml());
}

uint64_t averageHash(camera_fb_t* fb) {
  uint64_t hash = 0;
  for (int i = 0; i < 64; i++) {
    hash <<= 1;
    hash |= fb->buf[i] > 127;
  }
  return hash;
}

void handleRegister() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera error");
    return;
  }
  File file = SPIFFS.open("/registered.jpg", FILE_WRITE);
  if (file) {
    file.write(fb->buf, fb->len);
    file.close();
    imageRegistered = true;
  }
  esp_camera_fb_return(fb);
  server.send(200, "text/plain", "Image registered");
}

void sendToTelegram(uint8_t* image, size_t len) {
  HTTPClient http;
  String url = "https://api.telegram.org/bot" TELEGRAM_BOT_TOKEN "/sendPhoto";
  http.begin(url);
  http.addHeader("Content-Type", "multipart/form-data; boundary=" PART_BOUNDARY);

  String bodyStart = "--" PART_BOUNDARY "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" TELEGRAM_CHAT_ID "\r\n";
  bodyStart += "--" PART_BOUNDARY "\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"image.jpg\"\r\n";
  bodyStart += "Content-Type: image/jpeg\r\n\r\n";
  String bodyEnd = "\r\n--" PART_BOUNDARY "--\r\n";

  int contentLength = bodyStart.length() + len + bodyEnd.length();
  http.addHeader("Content-Length", String(contentLength));
  WiFiClient* stream = http.getStreamPtr();

  stream->print(bodyStart);
  stream->write(image, len);
  stream->print(bodyEnd);
  int code = http.POST("");
  Serial.println("Telegram status: " + String(code));
  http.end();
}

void handleStream() {
  WiFiClient client = server.client();
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  client.print(response);
  static uint64_t registeredHash = 0;

  while (1) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) continue;

    client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n", fb->len);
    client.write(fb->buf, fb->len);
    client.print("\r\n");

    if (imageRegistered) {
      uint64_t currentHash = averageHash(fb);
      if (registeredHash == 0) {
        File file = SPIFFS.open("/registered.jpg", "r");
        if (file) {
          uint8_t buffer[64];
          file.read(buffer, 64);
          file.close();
          for (int i = 0; i < 64; i++) {
            registeredHash <<= 1;
            registeredHash |= buffer[i] > 127;
          }
        }
      }
      int hamming = __builtin_popcountll(registeredHash ^ currentHash);
      if (hamming < 10) {
        sendToTelegram(fb->buf, fb->len);
        delay(5000);
      }
    }

    esp_camera_fb_return(fb);
    if (!client.connected()) break;
  }
}

void setup() {
  Serial.begin(115200);
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
  startCamera();
  server.on("/", HTTP_GET, handleRoot);
  server.on("/register", HTTP_POST, handleRegister);
  server.on("/stream", HTTP_GET, handleStream);
  server.begin();
}

void loop() {
  server.handleClient();
}
