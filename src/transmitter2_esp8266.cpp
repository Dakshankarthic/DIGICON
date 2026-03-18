#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// === CONFIGURATION ===
#define DHTPIN D4     // GPIO2 on NodeMCU
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// I2C LCD Configuration (0x27 is common, some are 0x3F)
// NodeMCU Wiring:
// LCD VCC -> VIN (5V) or 3V3 
// LCD GND -> GND
// LCD SDA -> D2 (GPIO4)
// LCD SCL -> D1 (GPIO5)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// === SECURE MAC ADDRESS (Update this!) ===
uint8_t receiverMac[] = {0x50, 0x78, 0x7D, 0x15, 0xB4, 0x08};

static uint32_t seq_counter = 0;
static uint16_t x_fixed = 79;
const uint16_t r_fixed = 1023;

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("\n[TX2] Send status: ");
  if (sendStatus == 0) {
    Serial.println("Delivery Success");
  } else {
    Serial.println("Delivery Fail");
  }
}

void setup() {
  Serial.begin(115200);

  // Hardware Init
  dht.begin();
  
  // Wire.begin(SDA, SCL) - default for NodeMCU is D2, D1
  Wire.begin(D2, D1);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("NODE 2 (ESP8266)");
  lcd.setCursor(0, 1);
  lcd.print("TX STARTING...");

  // Network Init
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // ESP8266 role
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(OnDataSent);

  // Add peer
  esp_now_add_peer(receiverMac, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
  
  delay(2000);
  lcd.clear();
}

void loop() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  if (isnan(temp) || isnan(hum)) {
    Serial.println("DHT Read Error");
    lcd.setCursor(0, 0);
    lcd.print("DHT Read Error");
    delay(2000);
    return;
  }

  // Update LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("T: ");
  lcd.print(temp, 1);
  lcd.print("C H: ");
  lcd.print(hum, 0);
  lcd.print("%");
  lcd.setCursor(0, 1);
  lcd.print("Seq: ");
  lcd.print(seq_counter);

  // Create JSON
  StaticJsonDocument<256> doc;
  doc["id"] = 2;
  doc["seq"] = seq_counter;
  doc["temp_C"] = temp;
  doc["humidity"] = hum;

  String plain;
  serializeJson(doc, plain);
  size_t plain_len = plain.length();
  
  // Security: Create Chaotic Scrambled Packet
  // Format: [0xBB (1 byte)] + [Sequence (4 bytes)] + [XOR Scrambled JSON (plain_len bytes)]
  uint8_t packet[1 + 4 + plain_len];
  packet[0] = 0xBB;
  memcpy(packet + 1, &seq_counter, 4);
  
  x_fixed = 79; // Reset chaotic seed for each packet
  for (size_t i = 0; i < plain_len; i++) {
    x_fixed = (r_fixed * x_fixed * (256 - x_fixed)) >> 8;
    packet[5 + i] = plain[i] ^ (uint8_t)x_fixed;
  }
  
  // Send over ESP-NOW
  esp_now_send(receiverMac, packet, sizeof(packet));
  
  Serial.printf("[TX2] Sent CHAOTIC SECURE Data (%u bytes)\n", sizeof(packet));

  seq_counter++;
  delay(30000);
}
