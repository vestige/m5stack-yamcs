#include <Arduino.h>

// M5Stack Core の Grove Port A
// GPS Unit: TX → GPIO22 (M5Stack RX)
//           RX → GPIO21 (M5Stack TX)
static const int GPS_RX = 22;
static const int GPS_TX = 21;

HardwareSerial GPS(2);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("====================");
  Serial.println("M5Stack GPS test");
  Serial.println("====================");

  // Unit GPS SMA (AT6668)
  GPS.begin(115200, SERIAL_8N1, GPS_RX, GPS_TX);

  Serial.println("Waiting for GPS data...");
}

void loop() {
  while (GPS.available()) {
    Serial.write(GPS.read());
  }
}
