#include <Arduino.h>
#include <mbedtls/gcm.h>
void setup() {
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
}
void loop() {}
