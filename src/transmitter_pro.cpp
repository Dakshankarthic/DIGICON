#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
#include <mbedtls/gcm.h>

// === CONFIGURATION ===
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// === SECURE MAC ADDRESS (Update this!) ===
uint8_t receiverMac[] = {0x50, 0x78, 0x7D, 0x15, 0xB4, 0x08};

// AES-256 key (Must match Receiver)
uint8_t aes_key[32] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                       0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
                       0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                       0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};

// Chaos parameters
static uint16_t x_fixed = 79;
const uint16_t r_fixed = 1023;

uint8_t get_logistic_byte() {
  uint32_t next_x = ( (uint32_t)r_fixed * (uint32_t)x_fixed * (256 - (uint32_t)x_fixed) ) >> 8;
  x_fixed = (uint16_t)next_x;
  return (uint8_t)x_fixed;
}

static uint32_t seq_counter = 0;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\n[TX] Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
}

void generate_chaotic(uint8_t *iv, uint8_t *keystream, size_t len) {
  x_fixed = 79; // Reset seed
  for (int i = 0; i < 12; i++)
    iv[i] = get_logistic_byte();
  for (size_t i = 0; i < len; i++)
    keystream[i] = get_logistic_byte();
}

void setup() {
  Serial.begin(115200);

  // Hardware Init
  dht.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("SECURE TX STARTING");
  display.display();

  // Network Init
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init fail");
    return;
  }
  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverMac, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Peer add fail");
  }
}

void loop() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  if (isnan(temp) || isnan(hum)) {
    Serial.println("DHT Read Error");
    delay(2000);
    return;
  }

  // Update OLED
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SECURE NODE (TX)");
  display.drawLine(0, 10, 128, 10, WHITE);
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.printf("T: %.1f C\n", temp);
  display.printf("H: %.0f %%", hum);
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.printf("Encoded Seq: %u", seq_counter);
  display.display();

  // Create JSON
  StaticJsonDocument<256> doc;
  doc["id"] = 3;
  doc["seq"] = seq_counter;
  doc["t"] = temp;
  doc["h"] = hum;

  String plain;
  serializeJson(doc, plain);
  size_t plain_len = plain.length();

  // Security Steps
  uint8_t iv[12];
  uint8_t keystream[plain_len];
  generate_chaotic(iv, keystream, plain_len);

  // 1. XOR Scramble
  uint8_t scrambled[plain_len];
  for (size_t i = 0; i < plain_len; i++) {
    scrambled[i] = (uint8_t)plain[i] ^ keystream[i];
  }

  // 2. AES-GCM Encrypt
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, aes_key, 256);

  uint8_t ciphertext[300];
  uint8_t tag[16];
  size_t olen = 0;

  mbedtls_gcm_starts(&gcm, MBEDTLS_GCM_ENCRYPT, iv, 12, NULL, 0);
  // Using update/finish for robustness
  mbedtls_gcm_update(&gcm, plain_len, scrambled, ciphertext);
  olen = plain_len;
  mbedtls_gcm_finish(&gcm, tag, 16);
  mbedtls_gcm_free(&gcm);

  // 3. Pack: [Seq(4)] + [IV(12)] + [Ciphertext(olen)] + [Tag(16)]
  uint8_t packet[4 + 12 + olen + 16];
  memcpy(packet, &seq_counter, 4);
  memcpy(packet + 4, iv, 12);
  memcpy(packet + 16, ciphertext, olen);
  memcpy(packet + 16 + olen, tag, 16);

  // Send
  esp_now_send(receiverMac, packet, sizeof(packet));

  seq_counter++;
  delay(30000);
}
