# DIGICON

DIGICON is an IoT project that features secure multi-node communication between distributed ESP32 and ESP8266 microcontrollers. The system captures temperature and humidity data from sensor nodes, securely encrypts and transmits the data to a central receiver, and visualizes the network health and sensor readings on a real-time web dashboard.

## System Architecture

The project consists of three main hardware nodes and a software dashboard:

1. **Node 1 (Transmitter 1):** ESP32-based node that securely transmits environmental data (AES-GCM encrypted).
2. **Node 2 (Transmitter 2):** ESP8266-based node (NodeMCU v2) that transmits data using Chaotic Encryption.
3. **Node 3 (Receiver):** ESP32-S3-based central node equipped with an E-paper display. It receives, decrypts, and logs the secure data payloads from the transmitter nodes, forwarding them over Serial.
4. **Web Dashboard:** A Python (Flask) web application that listens to the Receiver node over Serial. It provides a real-time visualization of temperature, humidity, packet sequence numbers, and fault rates for all active nodes.

## Hardware & Libraries

- **Microcontrollers:** ESP32, ESP32-S3, ESP8266
- **Sensors:** DHT Temperature & Humidity Sensors
- **Displays:** SSD1306 (OLED), LiquidCrystal_I2C, GxEPD2 (E-paper screen)
- **Encryption:** MbedTLS (AES-GCM), Chaotic Math Cryptography
- **Platform:** PlatformIO

## Getting Started

### 1. Hardware Firmware Verification & Upload
To compile and upload the firmware to the respective microcontrollers, use the following PlatformIO CLI commands:

**For Node 1 (ESP32 Transmitter):**
```bash
pio run -e transmitter_hw --target upload
```

**For Node 2 (ESP8266 Transmitter):**
```bash
pio run -e transmitter2_esp8266 --target upload
```

**For Node 3 (ESP32-S3 Receiver):**
```bash
pio run -e receiver_hw --target upload
```

### 2. Running the Web Dashboard
First, ensure the Receiver (Node 3) is connected to your host machine via USB. You may need to update the `SERIAL_PORT` variable in `web/app.py` to match the active COM port of your receiver (default is `COM16`).

Run the Flask application:
```bash
cd web
pip install flask pyserial
python app.py
```
Open a browser and navigate to `http://localhost:5000` to view the Real-Time Operational Dashboard.
