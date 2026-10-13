// 地上局の無線機 (M5StickC)
//
//   衛星 (M5Stack) ~~~ ESP-NOW ~~~ この無線機 ──USB シリアル── Mac (tools/udp_receiver.py → YAMCS)
//
// 衛星から ESP-NOW で届いたパケットを SerialFrame にして Mac へ流し、
// Mac から届いた TC を ESP-NOW で衛星へ送る。
#include <Arduino.h>
#include <M5Unified.h>
#include <SerialFrame.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#ifndef ESPNOW_CHANNEL
#define ESPNOW_CHANNEL 1
#endif

static const int RX_LED_PIN = 10;  // M5StickC の赤いLED (LOWで点灯)
static const uint32_t LOSS_OF_SIGNAL_MS = 3000;
static const uint32_t DISPLAY_INTERVAL_MS = 500;

struct ReceivedPacket {
  uint8_t mac[6];
  uint8_t size;
  uint8_t data[ESP_NOW_MAX_DATA_LEN];
};

QueueHandle_t receivedQueue;
SerialFrame::Decoder uplinkDecoder;
M5Canvas canvas(&M5.Display);

uint8_t satelliteMac[6];
bool satelliteKnown = false;
uint32_t lastReceivedMs = 0;
uint32_t downlinkCount = 0;
uint32_t uplinkCount = 0;
uint32_t uplinkDropped = 0;
uint32_t lastDisplayMs = 0;

// Wi-Fiのタスクから呼ばれるので、キューに入れるだけにする
void onReceive(const uint8_t* mac, const uint8_t* data, int size) {
  if (size <= 0 || size > ESP_NOW_MAX_DATA_LEN) return;
  ReceivedPacket packet;
  memcpy(packet.mac, mac, sizeof(packet.mac));
  packet.size = static_cast<uint8_t>(size);
  memcpy(packet.data, data, size);
  xQueueSend(receivedQueue, &packet, 0);
}

void rememberSatellite(const uint8_t* mac) {
  if (satelliteKnown && memcmp(mac, satelliteMac, sizeof(satelliteMac)) == 0) return;
  if (satelliteKnown) esp_now_del_peer(satelliteMac);
  memcpy(satelliteMac, mac, sizeof(satelliteMac));
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, sizeof(peer.peer_addr));
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  satelliteKnown = esp_now_add_peer(&peer) == ESP_OK;
}

void downlink() {
  ReceivedPacket packet;
  while (xQueueReceive(receivedQueue, &packet, 0) == pdTRUE) {
    rememberSatellite(packet.mac);
    uint8_t frame[ESP_NOW_MAX_DATA_LEN + SerialFrame::kOverhead];
    size_t size = SerialFrame::encode(packet.data, packet.size, frame, sizeof(frame));
    Serial.write(frame, size);
    downlinkCount++;
    lastReceivedMs = millis();
  }
}

void uplink() {
  while (Serial.available()) {
    if (!uplinkDecoder.push(static_cast<uint8_t>(Serial.read()))) continue;
    if (!satelliteKnown || uplinkDecoder.size() > ESP_NOW_MAX_DATA_LEN) {
      uplinkDropped++;
      continue;
    }
    if (esp_now_send(satelliteMac, uplinkDecoder.payload(), uplinkDecoder.size()) == ESP_OK) {
      uplinkCount++;
    } else {
      uplinkDropped++;
    }
  }
}

bool inContact(uint32_t now) { return lastReceivedMs != 0 && now - lastReceivedMs < LOSS_OF_SIGNAL_MS; }

void drawStatus(uint32_t now) {
  bool contact = inContact(now);
  canvas.fillScreen(TFT_BLACK);

  canvas.fillRect(0, 0, canvas.width(), 18, contact ? TFT_DARKGREEN : TFT_MAROON);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextDatum(middle_left);
  canvas.drawString("M5Sat GS", 4, 9);
  canvas.setTextDatum(middle_right);
  canvas.drawString(contact ? "AOS" : "LOS", canvas.width() - 4, 9);

  char line[32];
  canvas.setTextDatum(top_left);
  snprintf(line, sizeof(line), "TM  %lu", static_cast<unsigned long>(downlinkCount));
  canvas.drawString(line, 4, 24);
  snprintf(line, sizeof(line), "TC  %lu  drop %lu", static_cast<unsigned long>(uplinkCount),
           static_cast<unsigned long>(uplinkDropped));
  canvas.drawString(line, 4, 40);
  if (satelliteKnown) {
    snprintf(line, sizeof(line), "SAT %02X:%02X:%02X  ch%d", satelliteMac[3], satelliteMac[4], satelliteMac[5],
             ESPNOW_CHANNEL);
  } else {
    snprintf(line, sizeof(line), "SAT ?  ch%d", ESPNOW_CHANNEL);
  }
  canvas.drawString(line, 4, 56);
  canvas.pushSprite(0, 0);

  digitalWrite(RX_LED_PIN, contact && now - lastReceivedMs < 100 ? LOW : HIGH);
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  pinMode(RX_LED_PIN, OUTPUT);
  digitalWrite(RX_LED_PIN, HIGH);

  M5.Display.setRotation(1);
  canvas.setColorDepth(16);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  canvas.setFont(&fonts::Font2);

  receivedQueue = xQueueCreate(16, sizeof(ReceivedPacket));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_now_init();
  esp_now_register_recv_cb(onReceive);
}

void loop() {
  downlink();
  uplink();

  uint32_t now = millis();
  if (now - lastDisplayMs >= DISPLAY_INTERVAL_MS || (lastReceivedMs != 0 && now - lastReceivedMs < 100)) {
    lastDisplayMs = now;
    drawStatus(now);
  }
}
