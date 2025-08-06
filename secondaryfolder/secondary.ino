#include "Arduino.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include <Update.h>
#include "driver/spi_slave.h"
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
String myteam;  //sent from primary
String mygame;  //sent from primary
String mymac;   //sent from primary
String dmg;     //sent from primary

int sendtime;
int rpm;  //sent from primary

int fileSize;

#define PIN_SCK 45
#define PIN_MOSI 21
#define PIN_CS 20
#define PIN_MISO 19

bool stopPollSPI;
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void initslave() {
  pinMode(PIN_CS, INPUT_PULLUP);
  spi_bus_config_t buscfg = {
    .mosi_io_num = PIN_MOSI,
    .miso_io_num = PIN_MISO,
    .sclk_io_num = PIN_SCK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1
  };
  spi_slave_interface_config_t slvcfg = {
    .spics_io_num = PIN_CS,
    .flags = 0,
    .queue_size = 1,
    .mode = 0
  };
  spi_slave_initialize(SPI3_HOST, &buscfg, &slvcfg, 0);
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void sendSPI(const String& token) {
  static char buffer[64] = { 0 };  //persistent buffer
  memset(buffer, 0, 64);           //clear buffer
  token.toCharArray(buffer, 64);   //copy string with null terminator

  spi_slave_transaction_t trx = {};
  trx.length = 64 * 8;  //bits
  trx.tx_buffer = buffer;
  spi_slave_transmit(SPI3_HOST, &trx, portMAX_DELAY);
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
String receiveSPI() {
  char buffer[64 + 1] = { 0 };  //+1 for safety
  spi_slave_transaction_t trx = {};
  trx.length = 64 * 8;
  trx.rx_buffer = buffer;
  spi_slave_transmit(SPI3_HOST, &trx, portMAX_DELAY);
  return String(buffer);  //cuts off at '\0'
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void pollSPI(void* pvParameters) {
  for (;;) {
    if (!stopPollSPI) {
      String msg = receiveSPI();

      if (msg[0] == '5') {
        mymac = msg.substring(1);
        Serial.println(mymac);

      } else if (msg[0] == '6') {
        mygame = msg.substring(1);
        Serial.println(mygame);

      } else if (msg[0] == '7') {
        dmg = msg.substring(1, 4);
        rpm = msg.substring(4).toInt();
        Serial.println(mygame);

      } else if (msg[0] == 'c') {
        update();
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void update() {
  stopPollSPI = true;
  static const uint32_t MAX_SIZE = 1024u * 1024u;
  static const size_t CHUNK_SIZE = 64;
  static const uint8_t ACK_BYTE = 0xAC;
  spi_slave_transaction_t trx;

  //HEADER
  uint32_t fileSize;
  uint8_t hdr[4];
  while (true) {
    memset(&trx, 0, sizeof(trx));
    trx.length = 32;
    trx.rx_buffer = hdr;
    spi_slave_transmit(SPI3_HOST, &trx, portMAX_DELAY);
    fileSize = (uint32_t)hdr[0] << 24 | hdr[1] << 16 | hdr[2] << 8 | hdr[3];
    if (fileSize > 0 && fileSize < MAX_SIZE) break;
  }
  Serial.printf("Size: %u\n", fileSize);

  //HS1 receive
  for (;;) {
    String s = receiveSPI();
    if (s.startsWith("hs1")) {
      Serial.println("got hs1");
      break;
    }
    delay(1);
  }

  //Update.begin
  if (!Update.begin(fileSize, U_FLASH, -1, LOW, "app1")) {
    Serial.printf("Upd.begin failed: %s\n", Update.errorString());
    ESP.restart();
    while (1) delay(1);
  }

  //HS2 send
  delay(500);  // Give master a moment after hs1
  sendSPI("hs2");
  Serial.println("sent hs2");

  //Wait for "hs3"
  for (;;) {
    String s = receiveSPI();
    if (s.startsWith("hs3")) {
      Serial.println("got hs3");
      break;
    }
    delay(1);
  }

  // Receive binary data
  static DRAM_ATTR uint8_t buf[CHUNK_SIZE];
  size_t remaining = fileSize;
  while (remaining) {
    size_t len = min(CHUNK_SIZE, remaining);
    memset(&trx, 0, sizeof(trx));
    trx.length = len * 8;
    trx.rx_buffer = buf;
    spi_slave_transmit(SPI3_HOST, &trx, portMAX_DELAY);
    delayMicroseconds(100);

    if (Update.write(buf, len) != len) {
      Serial.printf("Upd.write fail: %s\n", Update.errorString());
      ESP.restart();
      while (1) delay(1);
    }
    remaining -= len;

    // send ACK
    uint8_t ack = ACK_BYTE;
    memset(&trx, 0, sizeof(trx));
    trx.length = 8;
    trx.tx_buffer = &ack;
    spi_slave_transmit(SPI3_HOST, &trx, portMAX_DELAY);

    Serial.printf("…%u left\n", remaining);
  }

  if (!Update.end(true)) {
    Serial.printf("Upd.end fail: %s\n", Update.errorString());
    ESP.restart();
  }

  delay(1000);
  ESP.restart();
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void broadcast(const String& msg) {
  uint8_t fullPacket[24 + 8 + msg.length() + 1];  //+1 to hold null terminator.

  const uint8_t macHeader[24] = {
    0x08, 0x00,
    0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    0x00, 0x00
  };
  memcpy(fullPacket, macHeader, 24);

  const uint8_t marker[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  memcpy(fullPacket + 24, marker, 8);
  msg.toCharArray((char*)(fullPacket + 24 + 8), msg.length() + 1);
  esp_wifi_80211_tx(WIFI_IF_STA, fullPacket, 24 + 8 + msg.length() + 1, false);
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void shoot() {
  if (rpm != 0 && (millis() - sendtime >= 60000 / rpm || sendtime == 0)) {  //If the time since last shot was long enough
    sendtime = millis();                                                    //Timestamp last shot

    String msg = ("1" + myteam + mygame + mymac + dmg);
    broadcast(msg);

    Serial.println(msg);
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void setup() {
  Serial.begin(115200);
  Serial.println("\nSECONDARY");

  initslave();
  xTaskCreatePinnedToCore(pollSPI, "pollSPI", 8192, NULL, 1, NULL, 0);

  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11N);
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
  esp_wifi_set_channel(11, WIFI_SECOND_CHAN_NONE);
  esp_wifi_start();
  esp_wifi_set_max_tx_power(78);
  delay(500);

  sendSPI("b" + String(ESP.getSketchSize()));
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void loop() {
  //shoot();
}
