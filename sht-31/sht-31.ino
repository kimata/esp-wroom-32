/*
 * SHT-31 を使って，温度・湿度を取得し，それをあらかじめ指定された IP アドレスの
 * Fluentd に WiFi で定期送信します．
 */

#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include <esp_deep_sleep.h>

#include <ArduinoJson.h> // 追加インストール要

#define I2C_SDA       25
#define I2C_SCL       26

#define WIFI_HOSTNAME "ESP32-outdoor"

#define FLUENTD_IP    "192.168.2.20"
#define FLUENTD_TAG   "/sensor"
#define FLUENTD_PORT  8888

#define SHT31_DEV_ADDR  0x44
#define INTERVAL_MIN    5

typedef struct {
  float temp;
  float humi;
} sht31_value_t;

bool postToFluentd(String jsonStr) {
  HTTPClient http;

  http.begin(FLUENTD_IP, FLUENTD_PORT, FLUENTD_TAG);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST("json=" + jsonStr);
  http.end();

  if (code != HTTP_CODE_OK) {
    log_e("Faile to POST to " FLUENTD_IP ".");
  }
  return true;
}

uint8_t crc8(const uint8_t *data, int len) {
  static const uint8_t POLY = 0x31;
  uint8_t crc = 0xFF;

  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ POLY;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

const char *createJsonStr(sht31_value_t& sht31_value) {
  static char buffer[256];

  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["temp"] = sht31_value.temp;
  json["humi"] = sht31_value.humi;
  json["hostname"] = WIFI_HOSTNAME;
  json.printTo(buffer);

  return buffer;
}

bool senseSht31(sht31_value_t& sht31_value) {
  uint8_t data[6];

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  Wire.beginTransmission(SHT31_DEV_ADDR);
  Wire.write(0x24);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(20);
  Wire.requestFrom(SHT31_DEV_ADDR, 6, 1);

  if (Wire.available() != 6) {
    log_e("Faile to read from SHT-31.");
    return false;
  }
  for (int i = 0; i < sizeof(data); i++) {
    data[i] =  Wire.read();
  }
  if ((crc8(data, 2) != data[2]) || (crc8(data + 3, 2) != data[5])) {
    log_e("Faile to validate CRC.");
    return false;
  }

  // disable pull up
  pinMode(I2C_SDA, INPUT);
  pinMode(I2C_SCL, INPUT);

  sht31_value.temp = -45 + (175 * (((uint16_t)data[0]) << 8 | data[1])) / (float)((1 << 16) - 1);
  sht31_value.humi = 100 * (((uint16_t)data[3]) << 8 | data[4]) / (float)((1 << 16) - 1);

  return true;
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_STA_START:
      WiFi.setHostname(WIFI_HOSTNAME);
      break;
    default:
      break;
  }
}

bool sense_and_send() {
  sht31_value_t sht31_value;

  WiFi.mode(WIFI_STA);
  WiFi.onEvent(WiFiEvent);

  WiFi.begin();
  // 一度は下記のようにして，SSID・パスワードを指定する必要有り．
  // WiFi.begin("SSID", "パスワード");

  if (!senseSht31(sht31_value)) {
    log_e("Failed to communicate with SHT-31.");
    return false;
  }

  String jsonStr(createJsonStr(sht31_value));

  for (int i = 0; i < 50; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      return postToFluentd(jsonStr);
    }
    delay(200);
  }
  log_e("Failed to connect WiFI.");
  
  return false;
}

void IRAM_ATTR onTimer() {
  esp_deep_sleep_enable_timer_wakeup(INTERVAL_MIN * 60 * 1000 * 1000);
  esp_deep_sleep_start();
}

void setup() {
  // 2.5 秒後に onTimer を呼び出して deelp sleep に入る
  hw_timer_t * timer = NULL;
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 2500000, true);
  timerAlarmEnable(timer);
  
  esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
  esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
  esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
  esp_deep_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_OFF);

  sense_and_send();
  
  delay(1000);
}

void loop() {
  
}

