#include <Arduino.h>
#include <ArduinoJson.h>
#include <GxEPD2_3C.h>
#include <GxEPD2_BW.h>
#include <WiFi.h>
#include <esp_now.h>
#include <mbedtls/gcm.h>

// === E-PAPER PINOUT (ESP32-S3) ===
#define EPD_CS 7
#define EPD_DC 6
#define EPD_RST 5
#define EPD_BUSY 4
#define EPD_SCK 12
#define EPD_MOSI 11

// Display Driver: 2.13" Z98c (3-color)
GxEPD2_3C<GxEPD2_213_Z98c, GxEPD2_213_Z98c::HEIGHT>
    display(GxEPD2_213_Z98c(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// === SAME KEY & SEED AS TRANSMITTER ===
uint8_t aes_key[32] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                       0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
                       0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                       0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};

const uint16_t r_fixed = 1023;

// Global variables for display
float temp1 = 0;
float hum1 = 0;
uint32_t seq1 = 0;

float temp2 = 0;
float hum2 = 0;
uint32_t seq2 = 0;

float temp3 = 0;
float hum3 = 0;
uint32_t seq3 = 0;

String lastStatus = "WAITING...";

// Robust 16-bit logistic map logic (Cross-platform identical)
uint8_t get_logistic_byte(uint16_t &x) {
  uint16_t next_x = (uint16_t)((1023UL * (uint32_t)x * (256UL - (uint32_t)x)) >> 8);
  x = next_x;
  return (uint8_t)x;
}

void updateDisplay() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(1);
    display.setCursor(5, 2);
    display.println("SECURE COMMAND CENTER");
    display.drawLine(0, 12, 250, 12, GxEPD_BLACK);

    // Node 1
    display.setCursor(5, 15);
    display.printf("N1: Seq %u", seq1);
    display.setTextSize(2);
    display.setCursor(5, 25);
    display.printf("T:%.1f H:%.0f%%", temp1, hum1);
    display.drawLine(0, 45, 250, 45, GxEPD_BLACK);

    // Node 2
    display.setTextSize(1);
    display.setCursor(5, 48);
    display.printf("N2: Seq %u", seq2);
    display.setTextSize(2);
    display.setCursor(5, 58);
    display.printf("T:%.1f H:%.0f%%", temp2, hum2);
    display.drawLine(0, 78, 250, 78, GxEPD_BLACK);

    // Node 3
    display.setTextSize(1);
    display.setCursor(5, 81);
    display.printf("N3: Seq %u", seq3);
    display.setTextSize(2);
    display.setCursor(5, 91);
    display.printf("T:%.1f H:%.0f%%", temp3, hum3);

    display.setTextSize(1);
    display.setCursor(5, 115);
    display.print("Rx: "); display.print(lastStatus.c_str());
  } while (display.nextPage());
}

void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  // Detect Chaotic Secure JSON from Node 2 (ESP8266)
  if (len > 5 && data[0] == 0xBB) {
    uint32_t seq;
    memcpy(&seq, data + 1, 4);
    int ct_len = len - 5;

    char jsonStr[ct_len + 1];
    uint16_t temp_x = 79;
    for (int i = 0; i < ct_len; i++) {
        jsonStr[i] = data[5 + i] ^ get_logistic_byte(temp_x);
    }
    jsonStr[ct_len] = '\0';

    Serial.printf("[RX] Node 2 Chaotic Paylaod: %s\n", jsonStr);

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, jsonStr);
    if (!err) {
      temp2 = doc["temp_C"] | doc["t"];
      hum2 = doc["humidity"] | doc["h"];
      seq2 = seq;
      lastStatus = "Node 2 (Chaotic Secure)";
      updateDisplay();
    }
    return;
  }

  if (len < 32)
    return;

  uint32_t seq;
  uint8_t iv[12];
  uint8_t tag[16];
  size_t ct_len = len - 4 - 12 - 16;

  memcpy(&seq, data, 4);
  memcpy(iv, data + 4, 12);
  const uint8_t *ciphertext = data + 16;
  memcpy(tag, data + 16 + ct_len, 16);

  // 1. Decrypt AES-GCM
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, aes_key, 256);
  uint8_t plaintext[ct_len];
  mbedtls_gcm_starts(&gcm, MBEDTLS_GCM_DECRYPT, iv, 12, NULL, 0);
  mbedtls_gcm_update(&gcm, ct_len, ciphertext, plaintext);
  int ret = mbedtls_gcm_finish(&gcm, tag, 16);
  mbedtls_gcm_free(&gcm);

  if (ret != 0) {
    Serial.println("[RX] Decrypt Fail (Tag)");
    return;
  }

  // 2. Remove Scramble
  uint16_t x_sync = 79;
  for (int i = 0; i < 12; i++) get_logistic_byte(x_sync);
  for (size_t i = 0; i < ct_len; i++) {
    plaintext[i] ^= get_logistic_byte(x_sync);
  }

  char json_buf[ct_len + 1];
  memcpy(json_buf, plaintext, ct_len);
  json_buf[ct_len] = '\0';

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, json_buf);
  if (err) {
    Serial.printf("[RX] JSON Error (Len %d): %s\n", ct_len, json_buf);
    return;
  }

  int node_id = doc.containsKey("id") ? (int)doc["id"] : 1;

  if (node_id == 3) {
    temp3 = doc["t"];
    hum3 = doc["h"];
    seq3 = seq;
    lastStatus = "Node 3 (AES)";
    Serial.printf("[RX] SECURE DATA Node 3: T=%.1f C, H=%.1f %%, Seq=%u\n",
                  temp3, hum3, seq3);
  } else {
    temp1 = doc["t"];
    hum1 = doc["h"];
    seq1 = seq;
    lastStatus = "Node 1 (AES-GCM)";
    Serial.printf("[RX] SECURE DATA Node 1: T=%.1f C, H=%.1f %%, Seq=%u\n",
                  temp1, hum1, seq1);
  }

  updateDisplay();
}

void setup() {
  Serial.begin(115200);

  // Setup SPI for E-Paper
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);

  // Init Display
  display.init(115200);
  display.setRotation(1);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(25, 60);
    display.setTextSize(2);
    display.println("WAITING FOR TX...");
  } while (display.nextPage());

  WiFi.mode(WIFI_STA);
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init fail");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("Receiver Ready - AES-GCM/Chaos Active");
}

void loop() { delay(100); }
