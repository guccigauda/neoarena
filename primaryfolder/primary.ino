#include <Wire.h>
#include "Arduino.h"
#include "esp_wifi.h"
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <SPI.h>
#include <Preferences.h>
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
String shot = "1";
String ack = "2";
String leadercheck = "3";
String leaderresp = "4";
String gamesettings = "8";
String pickteams = "9";
String startgame = "a";

String myteam;  //make team 2 nulls or maybe newlines for ffa. otherwise, allow choosing teams
String game;
String mymac;
String dmg = "020";
String myname;

String rpm = "060";
String hp = "200";

String pregame;
bool gameleader;

struct messageStruct {
  String rawmsg;

  String type;
  String team;
  String game;
  String sendermac;
  String dmg;
  String name;

  int power;
  int delaystart;
  int delay;
};
messageStruct temp;
messageStruct clear;

messageStruct MSGbuffer[100];

const int optionsLen = 10;
const int settsLen = 20;

struct settingStruct {
  String name;  //setting name
  String type;  //bool, num, str

  bool boolval;  //setting true/false

  float numval;  //current num value
  float min;     //min num value
  float max;     //max num value
  float inc;     //num increment

  String options[optionsLen];  //holds options
  int optionsInd;              //current index of option
  String strval;               //current option
};

settingStruct setts[settsLen];

char gotmsgchars[128];
int gotpower = -1;

unsigned long micros1;

bool ingame;

bool exitPNR;

WiFiUDP udp;

#define PIN_SCK 45
#define PIN_MOSI 21
#define PIN_CS 20
#define PIN_MISO 19

String ssid;
String pass;
String secondaryBIN = "https://raw.githubusercontent.com/guccigauda/neoarena/main/secondary.ino.bin";
String primaryBIN = "https://raw.githubusercontent.com/guccigauda/neoarena/main/primary.ino.bin";

static const size_t CHUNK_SIZE = 64;
static const uint8_t HS1 = 0x5A;
static const uint8_t HS2 = 0xA5;
static const uint8_t HS3 = 0x55;
static const uint8_t ACK_BYTE = 0xAC;
static const SPISettings SPI_SPEED(5000000, MSBFIRST, SPI_MODE0);
static DRAM_ATTR uint8_t buf[CHUNK_SIZE];

Preferences prefs;

int secondarySize;

bool stopPollSPI;
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
#define FT6236_ADDR 0x38
float nowX = 0;
float nowY = 0;
float minX, maxX, minY, maxY;
int prevX;
int prevY;
String screen;
volatile bool touching = false;
void IRAM_ATTR touchInterrupt() {
  touching = !digitalRead(14);
}
uint8_t th_group, th_diff, period_active;

TFT_eSPI tft = TFT_eSPI();
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void onReceive(void* buf, wifi_promiscuous_pkt_type_t type) {
  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  uint8_t* payload = pkt->payload;
  uint8_t* data = payload + 24;  //Skip MAC header (24 bytes)

  //Check for 8-byte marker
  if (data[0] == 1 && data[1] == 2 && data[2] == 3 && data[3] == 4 && data[4] == 5 && data[5] == 6 && data[6] == 7 && data[7] == 8) {

    int i = 0;
    while (i < 127 && data[8 + i] != '\0') {  //read until null terminator or max limit
      gotmsgchars[i] = (char)data[8 + i];
      i++;
    }
    gotmsgchars[i] = '\0';  //Ensure proper null termination

    gotpower = pkt->rx_ctrl.rssi;
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void validatemsg() {
  if (gotmsgchars[0] != '\0' && gotpower != -1) {
    temp = clear;
    temp.rawmsg = String(gotmsgchars);
    temp.power = gotpower;

    gotmsgchars[0] = '\0';
    gotpower = -1;

    if (temp.rawmsg[0] == '1' && temp.power >= -40) {
      temp.type = shot;
      temp.team = temp.rawmsg.substring(1, 3);
      temp.game = temp.rawmsg.substring(3, 7);
      temp.sendermac = temp.rawmsg.substring(7, 19);
      temp.dmg = temp.rawmsg.substring(19);
      if (temp.sendermac == mymac || temp.game != game) {
        temp = clear;
        return;
      }

    } else if (temp.rawmsg[0] == '2') {
      temp.type = ack;
      temp.team = temp.rawmsg.substring(1, 3);
      temp.game = temp.rawmsg.substring(3, 7);
      temp.sendermac = temp.rawmsg.substring(7, 19);
      temp.dmg = temp.rawmsg.substring(19, 22);
      temp.name = temp.rawmsg.substring(22);
      if (temp.sendermac == mymac || temp.game != game) {
        temp = clear;
        return;
      }

    } else if (temp.rawmsg[0] == '3') {
      temp.type = leadercheck;
      temp.game = temp.rawmsg.substring(1, 5);
      if (temp.game != game || !gameleader) {
        temp = clear;
        return;
      }

    } else if (temp.rawmsg[0] == '4') {
      temp.type = leaderresp;
      temp.game = temp.rawmsg.substring(1, 5);
      if (temp.game != pregame) {
        temp = clear;
        return;
      }
      screen = temp.rawmsg.substring(5);

    } else if (temp.rawmsg[0] == '8') {
      temp.type = gamesettings;
      temp.game = temp.rawmsg.substring(1, 5);
      if (temp.game != game) {
        temp = clear;
        return;
      }

    } else if (temp.rawmsg[0] == '9') {
      temp.type = pickteams;
      temp.game = temp.rawmsg.substring(1, 5);
      if (temp.game != game) {
        temp = clear;
        return;
      }

    } else if (temp.rawmsg[0] == 'a') {
      temp.type = startgame;
      temp.game = temp.rawmsg.substring(1, 5);
      if (temp.game != game) {
        temp = clear;
        return;
      }

    } else {
      temp = clear;
      return;
    }

    printTemp();

    for (int i = 0; i < 100; i++) {
      if (MSGbuffer[i].rawmsg == "") {
        MSGbuffer[i] = temp;
        break;
      }
    }
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void msgController() {
  for (int m = 0; m < 100; m++) {  //iterate thru msgs
    if (MSGbuffer[m].type == ack) {
      if ((micros() - MSGbuffer[m].delaystart > MSGbuffer[m].delay) && (MSGbuffer[m].delaystart != 0 && MSGbuffer[m].delay != 0)) {
        broadcast(MSGbuffer[m].rawmsg);
        MSGbuffer[m] = clear;  //clear msg

      } else {
        for (int M2 = 0; M2 < 100; M2++) {
          if (MSGbuffer[M2].type == ack && M2 != m && MSGbuffer[M2].sendermac == MSGbuffer[m].sendermac) {
            MSGbuffer[m] = clear;
            MSGbuffer[M2] = clear;
            break;  //stop checking after removing both
          }
        }
      }
    }

    if (MSGbuffer[m].type == shot) {          //if msg is shot
      MSGbuffer[m].type = 2;                  //convert to ack
      MSGbuffer[m].rawmsg.setCharAt(0, '2');  //convert to ack
      MSGbuffer[m].rawmsg += myname;          //append myname to rawmsg

      MSGbuffer[m].delaystart = micros();                                          //timestamp
      MSGbuffer[m].delay = constrain((10 - MSGbuffer[m].power) * 1000, 0, 40000);  //calculate delay from power (INCREASE MARGIN AND LESS STEPS)
    }

    if (MSGbuffer[m].type == leadercheck) {
      if (MSGbuffer[m].delaystart == 0) {
        broadcast(leaderresp + game + screen);

        MSGbuffer[m].delaystart = millis();
        MSGbuffer[m].delay = 50;
      }

      if (millis() - MSGbuffer[m].delaystart > MSGbuffer[m].delay && (MSGbuffer[m].delaystart != 0 && MSGbuffer[m].delay != 0)) {
        broadcastsettings();
        MSGbuffer[m] = clear;
      }
    }

    if (MSGbuffer[m].type == leaderresp) {
      game = pregame;
      pregame = "";
      MSGbuffer[m] = clear;
    }

    if (MSGbuffer[m].type == gamesettings) {
      setts[0].optionsInd = MSGbuffer[m].rawmsg.substring(5, 7).toInt();
      setts[0].strval = setts[0].options[setts[0].optionsInd];          //gamemode from index
      setts[1].boolval = MSGbuffer[m].rawmsg.substring(7, 8).toInt();   //teams
      setts[2].numval = MSGbuffer[m].rawmsg.substring(8, 11).toInt();   //health
      setts[3].numval = MSGbuffer[m].rawmsg.substring(11, 13).toInt();  //respawn time
      setts[4].numval = MSGbuffer[m].rawmsg.substring(13, 15).toInt();  //time limit

      MSGbuffer[m] = clear;
      printSettings();
    }

    if (MSGbuffer[m].type == pickteams) {
      screen = "teampage";
      exitPNR = true;
      MSGbuffer[m] = clear;
    }

    if (MSGbuffer[m].type == startgame) {
      screen = "gamepanel";
      MSGbuffer[m] = clear;
    }
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void broadcast(const String& msg) {
  esp_wifi_set_promiscuous(false);  //Disable sniffing

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

  esp_wifi_set_promiscuous(true);  //Resume sniffing
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
String formatnum(int num, int digits) {
  String s = String(num);
  while (s.length() < digits) {
    s = "0" + s;
  }
  return s;
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void printTemp() {
  Serial.println("\nrawmsg:     " + temp.rawmsg);
  Serial.print("type:       ");
  Serial.println(temp.type);
  Serial.print("team:       ");
  Serial.println(temp.team);
  Serial.print("game:       ");
  Serial.println(temp.game);
  Serial.print("sendermac:  ");
  Serial.println(temp.sendermac);
  Serial.print("dmg:        ");
  Serial.println(temp.dmg);
  Serial.print("name:       ");
  Serial.println(temp.name);
  Serial.print("power:      ");
  Serial.println(temp.power);
  Serial.println();
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void setupPromiscuous() {
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

  wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA };
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(onReceive);
  esp_wifi_set_promiscuous(true);

  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  char fullHex[13];  //12 hex chars + 1 null terminator
  sprintf(fullHex, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  mymac = String(fullHex);
  Serial.println("mac: " + mymac);  //6 byte mac. 12 chars

  delay(500);
  sendSPI("5" + mymac);
  sendSPI("7" + dmg + rpm);
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void disconnectwifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(500);
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void sendSPI(const String& token) {
  char buffer[64] = { 0 };        //zero-padded
  token.toCharArray(buffer, 64);  //copy up to 63 chars + null

  noInterrupts();
  SPI.beginTransaction(SPI_SPEED);
  digitalWrite(PIN_CS, LOW);
  SPI.transfer((uint8_t*)buffer, 64);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
  interrupts();
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
String receiveSPI() {
  char buffer[64 + 1] = { 0 };  //+1 for null safety
  noInterrupts();
  SPI.beginTransaction(SPI_SPEED);
  digitalWrite(PIN_CS, LOW);
  SPI.transfer((uint8_t*)buffer, 64);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
  interrupts();
  return String(buffer);  //stops at first '\0'
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void pollSPI(void* pvParameters) {
  for (;;) {
    if (!stopPollSPI) {
      String msg = receiveSPI();

      if (msg[0] == 'b') {
        secondarySize = msg.substring(1).toInt();
        Serial.println(msg);
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
String retry(String text) {
  tft.fillScreen(TFT_BLACK);
  tft.drawString(text, 160, 75, 2);

  tft.drawRect(40, 150, 80, 60, TFT_WHITE);
  tft.drawString("Retry", 80, 180, 2);

  tft.drawRect(200, 150, 80, 60, TFT_WHITE);
  tft.drawString("Skip", 240, 180, 2);

  for (;;) {
    PNR();
    if (nowX > 40 && nowX < 120 && nowY > 150 && nowY < 210) {
      return "wifisetup";

    } else if (nowX > 200 && nowX < 280 && nowY > 150 && nowY < 210) {
      return "entername";
    }
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void updatesecondary() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Checking for updates...", 160, 120, 2);

  disconnectwifi();

  WiFi.begin(ssid, pass);
  if (WiFi.waitForConnectResult(10000) != WL_CONNECTED) {
    screen = retry("Couldn't connect to WiFi. Retry?");
    return;
  }

  WiFiClientSecure headCli;
  headCli.setInsecure();
  HTTPClient headReq;
  headReq.begin(headCli, secondaryBIN);
  headReq.setTimeout(10000);
  if (headReq.sendRequest("HEAD") != HTTP_CODE_OK) {
    screen = retry("HeadReq failed. Retry?");
    return;
  }

  size_t binSize = headReq.getSize();
  headReq.end();
  Serial.printf("→ secondary.bin is %u bytes\n", binSize);

  if (secondarySize == binSize) {
    Serial.println("bin same size as sketch. no update for secondary");
    screen = "updateprimary";
    return;
  }

  tft.fillScreen(TFT_BLACK);
  tft.drawString("Updating...", 160, 110, 2);
  tft.drawString("Will reboot automatically when done.", 160, 130, 2);

  stopPollSPI = true;
  sendSPI("c");
  delay(2000);

  auto spiSend = [&](const uint8_t* data, size_t len) {
    noInterrupts();
    SPI.beginTransaction(SPI_SPEED);
    digitalWrite(PIN_CS, LOW);
    SPI.transfer((void*)data, len);
    digitalWrite(PIN_CS, HIGH);
    SPI.endTransaction();
    interrupts();
    delay(1);
  };

  auto spiRecv = [&](uint8_t& out) {
    uint8_t dummy = 0;
    noInterrupts();
    SPI.beginTransaction(SPI_SPEED);
    digitalWrite(PIN_CS, LOW);
    out = SPI.transfer(dummy);
    digitalWrite(PIN_CS, HIGH);
    SPI.endTransaction();
    interrupts();
    delay(1);
  };

  //HEADER
  uint8_t hdr[4] = {
    uint8_t(binSize >> 24), uint8_t(binSize >> 16),
    uint8_t(binSize >> 8), uint8_t(binSize)
  };
  spiSend(hdr, 4);
  delay(10);  // slight pause

  //Step 1: send hs1
  sendSPI("hs1");
  Serial.println("sent hs1");

  //Step 2: wait for hs2
  for (;;) {
    String s = receiveSPI();
    if (s.startsWith("hs2")) {
      Serial.println("got hs2");
      break;
    }
    delay(1);
  }

  //Step 3: send hs3
  delay(500);
  sendSPI("hs3");
  Serial.println("sent hs3");

  // Begin transfer
  HTTPClient getReq;
  getReq.begin(headCli, secondaryBIN);
  getReq.setTimeout(60000);
  if (getReq.GET() != HTTP_CODE_OK) {
    Serial.println("GET failed");
    return;
  }

  WiFiClient* stream = getReq.getStreamPtr();
  size_t sent = 0;
  while (sent < binSize) {
    size_t toRead = min(CHUNK_SIZE, binSize - sent);
    size_t got = 0;
    unsigned long rs = millis();
    while (got < toRead && millis() - rs < 5000) {
      int r = stream->read(buf + got, toRead - got);
      if (r > 0) got += r;
      else delay(1);
    }
    if (got != toRead) {
      Serial.printf("Read timeout: %u/%u\n", got, toRead);
      tft.fillScreen(TFT_BLACK);
      tft.drawString("Update failed. Rebooting...", 160, 120, 2);
      ESP.restart();
    }

    spiSend(buf, toRead);

    // wait for ACK
    uint8_t ack = 0;
    unsigned long t1 = millis();
    while (millis() - t1 < 1000) {
      spiRecv(ack);
      if (ack == ACK_BYTE) break;
    }

    if (ack != ACK_BYTE) {
      Serial.println("No ACK, abort");
      tft.fillScreen(TFT_BLACK);
      tft.drawString("Update failed. Rebooting...", 160, 120, 2);
      ESP.restart();
    }

    sent += toRead;
    Serial.printf("→ streamed %u/%u\n", sent, binSize);
  }

  getReq.end();
  Serial.println("Done streaming");

  screen = "updateprimary";
  return;
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void updateprimary() {
  WiFiClientSecure client;
  client.setInsecure();

  //Step 1: Get binary size
  HTTPClient http;
  http.begin(client, primaryBIN);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int len = http.getSize();
    Serial.printf("Update binary size: %d bytes\n", len);

    if (ESP.getSketchSize() == len) {
      screen = "entername";
      return;
    }

  } else {
    Serial.printf("Failed to get file info. HTTP code: %d\n", httpCode);
    screen = retry("Error HTTP code: " + String(httpCode) + ".");
    http.end();
    return;
  }
  http.end();  //close connection before update

  //Step 2: Perform update
  t_httpUpdate_return ret = httpUpdate.update(client, primaryBIN);

  if (ret == HTTP_UPDATE_FAILED) {
    Serial.println("Update failed. Rebooting...");
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Update failed. Rebooting...", 160, 120, 2);
    ESP.restart();
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void touchSettings(uint8_t thGroup, uint8_t thDiff, uint8_t periodActiveMs) {
  //write TH_GROUP (0x80)
  Wire.beginTransmission(FT6236_ADDR);
  Wire.write(0x80);
  Wire.write(thGroup);
  Wire.endTransmission();

  //write TH_DIFF (0x85)
  Wire.beginTransmission(FT6236_ADDR);
  Wire.write(0x85);
  Wire.write(thDiff);
  Wire.endTransmission();

  //write PERIODACTIVE (0x88) – report interval in ms
  Wire.beginTransmission(FT6236_ADDR);
  Wire.write(0x88);
  Wire.write(periodActiveMs);
  Wire.endTransmission();
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
bool ft_read_touch(uint16_t* rx, uint16_t* ry) {
  Wire.beginTransmission(FT6236_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(FT6236_ADDR, (uint8_t)7) < 7) return false;
  uint8_t touches = Wire.read() & 0x0F;
  if (touches == 0) return false;
  uint8_t xh = Wire.read(), xl = Wire.read();
  uint8_t yh = Wire.read(), yl = Wire.read();
  Wire.read();
  Wire.read();  //discard ID & weight
  *rx = ((xh & 0x0F) << 8) | xl;
  *ry = ((yh & 0x0F) << 8) | yl;
  return true;
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
bool getTouch() {
  uint16_t rawx, rawy;
  if (!ft_read_touch(&rawx, &rawy)) return false;
  prevX = nowX;
  prevY = nowY;
  nowX = constrain(320 - (float)rawy, 0, 320);
  nowY = constrain((float)rawx, 0, 240);
  return true;
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void touchtask(void* parameter) {
  for (;;) {
    if (screen == "wifisetup") { wifisetup(); }
    if (screen == "updatesecondary") { updatesecondary(); }
    if (screen == "updateprimary") { updateprimary(); }
    if (screen == "entername") { entername(); }
    if (screen == "entercode") { entercode(); }
    if (screen == "settings") { settings(); }
    if (screen == "teampage") { teampage(); }
    if (screen == "gamepanel") { gamepanel(); }
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void PNR() {
  while (getTouch()) {
    if (exitPNR) {
      exitPNR = false;
      return;
    }
    delay(20);
  }

  while (!getTouch()) {
    if (exitPNR) {
      exitPNR = false;
      return;
    }
    delay(20);
  }

  while (getTouch()) {
    if (exitPNR) {
      exitPNR = false;
      return;
    }
    delay(20);
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
String keystrokes(String placeholder, int minLen, int maxLen, String backscreen, int datum) {
  static const char page0[] = "abcdefghijklmnopqrstuvwxyz";
  static const char page1[] = " !\"#$%&'()*+,-./0123456789";
  static const char page2[] = ":;<=>?@[\\]^_`{|}~";
  const char* pages[3] = { page0, page1, page2 };
  const size_t pageLens[3] = { sizeof(page0) - 1, sizeof(page1) - 1, sizeof(page2) - 1 };

  String text = "";
  int page = 0;
  bool shift = false;

  while (true) {
    tft.fillScreen(TFT_BLACK);

    for (int i = 0; i < 30; i++) {
      int row = i / 6 + 1;
      int col = i % 6;
      int x = col * 53;
      int y = row * 40;

      if ((i < pageLens[page] && i < 26) || (i >= 26 && i < 30)) {
        tft.drawRect(x, y, 53, 40, TFT_WHITE);

        if (i < pageLens[page] && i < 26) {
          if (page == 1 && i == 0) {
            tft.drawString("space", x + 26, y + 20, 2);
          } else {
            char c = pages[page][i];
            if (page == 0 && shift) c = toupper(c);
            tft.drawString(String(c), x + 26, y + 20, 2);
          }
        } else if (i >= 26) {
          int idx = i - 26;
          if (idx == 0) tft.drawString("Aa", x + 26, y + 20, 2);
          else if (idx == 1) tft.drawString("...", x + 26, y + 20, 2);
          else if (idx == 2) tft.drawString("<-", x + 26, y + 20, 2);
          else if (idx == 3) tft.drawString("enter", x + 26, y + 20, 2);
        }
      }
    }

    tft.fillRect(0, 0, 320, 40, TFT_BLACK);

    if (text.length() > 0) {
      tft.setTextDatum(datum);
      if (datum == 5) {
        tft.drawString(text, 316, 20, 2);
      } else if (datum == 4) {
        tft.drawString(text, 160, 20, 2);
      }
      tft.setTextDatum(4);

    } else {
      tft.drawString(placeholder, 160, 20, 2);
    }

    tft.fillRect(0, 0, 53, 40, TFT_BLACK);
    tft.drawString("back", 26, 20, 2);

    PNR();

    //Check for "back" button press
    if (nowY < 40 && nowX < 53) {
      screen = backscreen;
      return "";
    }

    if (nowY < 40) continue;
    int col = nowX / 53;
    int row = (nowY - 40) / 40;
    if (col < 0 || col > 5 || row < 0 || row > 4) continue;
    int idx = row * 6 + col;

    if (idx < pageLens[page] && idx < 26) {
      if (page == 1 && idx == 0) {
        if (text.length() < maxLen) text += ' ';
      } else {
        char c = pages[page][idx];
        if (page == 0 && shift) c = toupper(c);
        if (text.length() < maxLen) text += c;
      }
    } else if (idx >= 26 && idx < 30) {
      int special = idx - 26;

      if (special == 0 && page == 0) {
        shift = !shift;

      } else if (special == 1) {
        page = (page + 1) % 3;
        shift = false;

      } else if (special == 2 && text.length() > 0) {
        text.remove(text.length() - 1);

      } else if (special == 3 && text.length() >= minLen) {
        return text;
      }
    }
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void wifisetup() {
  for (;;) {
    prefs.begin("wifi", false);
    tft.fillScreen(TFT_BLACK);
    bool tempbool = false;

    ssid = prefs.getString("ssid", "");
    pass = prefs.getString("pass", "");

    if (ssid != "" && pass != "") {
      tft.drawString("wifi setup", 160, 20, 2);
      tft.drawString("SSID: \"" + ssid + "\"", 160, 60, 2);
      tft.drawString("Password: \"" + pass + "\"", 160, 100, 2);

      tft.drawRect(40, 150, 80, 60, TFT_RED);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("incorrect", 80, 180, 2);

      tft.drawRect(200, 150, 80, 60, TFT_GREEN);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("correct", 240, 180, 2);
      tft.setTextColor(TFT_WHITE);

      PNR();
      if (nowX > 40 && nowX < 120 && nowY > 150 && nowY < 210) {
        tempbool = true;

      } else if (nowX > 200 && nowX < 280 && nowY > 150 && nowY < 210) {
        screen = "updatesecondary";
        return;
      }
    }

    if (ssid == "" || pass == "" || tempbool) {
      String ssid2 = keystrokes("enter wifi SSID", 1, 64, "wifisetup", 5);
      if (ssid2 == "") { continue; }
      ssid = ssid2;
      prefs.putString("ssid", ssid);

      String pass2 = keystrokes("enter wifi password", 1, 64, "wifisetup", 5);
      if (pass2 == "") { continue; }
      pass = pass2;
      prefs.putString("pass", pass);
    }
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void entername() {
  myname = keystrokes("enter username", 1, 8, "wifisetup", 4);
  if (myname != "") { screen = "entercode"; }
  return;
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void entercode() {
  gameleader = false;
  game = "";
  pregame = "";
  pregame = keystrokes("enter 4-character game code", 4, 4, "entername", 4);

  if (pregame == "") { return; }

  broadcast(leadercheck + pregame);
  delay(50);

  if (game == "") {
    game = pregame;
    pregame = "";
    gameleader = true;
    screen = "settings";
  }

  sendSPI("6" + game);
  return;
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void settings() {
  if (gameleader) {
    int row = 0;

    for (;;) {
      tft.fillScreen(TFT_BLACK);  //update screen
      tft.drawString("settings", 160, 15, 2);

      for (int i = 0; i < 3; i++) {
        int y = 40 + i * 50;

        tft.drawRect(100, y, 40, 40, TFT_WHITE);
        tft.drawString("<", 120, y + 20, 2);

        tft.drawRect(270, y, 40, 40, TFT_WHITE);
        tft.drawString(">", 290, y + 20, 2);

        tft.drawString(setts[row + i].name, 50, y + 20, 2);

        // clang-format off
        String val = setts[row + i].type == "bool" ? (setts[row + i].boolval ? "on" : "off") : setts[row + i].type == "num" ? String(setts[row + i].numval) : setts[row + i].options[setts[row + i].optionsInd];
        // clang-format on

        tft.drawString(val, 205, y + 20, 2);
      }

      //Draw bottom buttons
      for (int i = 0; i < 4; i++) {
        int x = 5 + i * 80;
        tft.drawRect(x, 200, 70, 40, TFT_WHITE);
        tft.drawString((String[]){ "back", "up", "down", "start" }[i], x + 35, 220, 2);
      }

      for (;;) {  //detect presses
        PNR();

        if (!getTouch()) {
          if (nowX > 100 && nowX < 140 && nowY > 40 && nowY < 80) {  //top left
            editsetting(row, "left");
            break;
          } else if (nowX > 100 && nowX < 140 && nowY > 90 && nowY < 130) {  //middle left
            editsetting(row + 1, "left");
            break;
          } else if (nowX > 100 && nowX < 140 && nowY > 140 && nowY < 180) {  //bottom left
            editsetting(row + 2, "left");
            break;
          } else if (nowX > 270 && nowX < 310 && nowY > 40 && nowY < 80) {  //top right
            editsetting(row, "right");
            break;
          } else if (nowX > 270 && nowX < 310 && nowY > 90 && nowY < 130) {  //middle right
            editsetting(row + 1, "right");
            break;
          } else if (nowX > 270 && nowX < 310 && nowY > 140 && nowY < 180) {  //bottom right
            editsetting(row + 2, "right");
            break;
          } else if (nowX > 5 && nowX < 75 && nowY > 200 && nowY < 240) {  //back
            screen = "entercode";
            return;
          } else if (nowX > 85 && nowX < 155 && nowY > 200 && nowY < 240 && row > 0) {  //up
            row--;
            break;
          } else if (nowX > 165 && nowX < 235 && nowY > 200 && nowY < 240 && row + 3 < settsLen && setts[row + 3].name != "") {  //down
            row++;
            break;
          } else if (nowX > 245 && nowX < 315 && nowY > 200 && nowY < 240) {  //next
            broadcastsettings();

            delay(50);
            if (setts[1].boolval == 1) {
              broadcast(pickteams + game);
              screen = "teampage";
              return;

            } else {
              broadcast(startgame + game);
              screen = "gamepanel";
              return;
            }
          }
        }
      }
    }
  } else {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("waiting for leader to select settings.", 160, 120, 2);

    tft.drawRect(125, 200, 70, 40, TFT_WHITE);
    tft.drawString("back", 160, 220, 2);

    PNR();

    if (screen == "teampage") { return; }

    if (nowX > 125 && nowX < 195 && nowY > 200 && nowY < 240) {  //back
      screen = "entercode";
      return;
    }
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void editsetting(int i, String direction) {
  if (setts[i].type == "bool") {
    setts[i].boolval = !setts[i].boolval;

  } else if (setts[i].type == "num") {
    if (direction == "right" && setts[i].numval < setts[i].max) {
      setts[i].numval += setts[i].inc;

    } else if (direction == "left" && setts[i].numval > setts[i].min) {
      setts[i].numval -= setts[i].inc;
    }

  } else if (setts[i].type == "str") {
    if (direction == "right" && setts[i].optionsInd < optionsLen - 1 && setts[i].options[setts[i].optionsInd + 1] != "") {
      setts[i].optionsInd++;
      setts[i].strval = setts[i].options[setts[i].optionsInd];

    } else if (direction == "left" && setts[i].optionsInd > 0) {
      setts[i].optionsInd--;
      setts[i].strval = setts[i].options[setts[i].optionsInd];
    }
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void initsettings() {
  setts[0].name = "gamemode";
  setts[0].type = "str";
  setts[0].options[0] = "deathmatch";

  setts[1].name = "teams";
  setts[1].type = "bool";
  setts[1].boolval = false;

  setts[2].name = "health";
  setts[2].type = "num";
  setts[2].numval = 100;
  setts[2].min = 50;
  setts[2].max = 200;
  setts[2].inc = 25;

  setts[3].name = "rspwn time";
  setts[3].type = "num";
  setts[3].numval = 20;
  setts[3].min = 10;
  setts[3].max = 30;
  setts[3].inc = 5;

  setts[4].name = "time limit";
  setts[4].type = "num";
  setts[4].numval = 10;
  setts[4].min = 0;
  setts[4].max = 30;
  setts[4].inc = 1;
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void broadcastsettings() {
  broadcast(gamesettings + game
            + formatnum(setts[0].optionsInd, 2)   //index of gamemode
            + String(setts[1].boolval)            //teams on/off
            + formatnum(int(setts[2].numval), 3)  //health, 50-200
            + formatnum(int(setts[3].numval), 2)  //respawn time, 10-30
            + formatnum(int(setts[4].numval), 2)  //time limit, 0-30
  );
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void printSettings() {
  Serial.println("gamemode:     " + String(setts[0].options[setts[0].optionsInd]));
  Serial.println("teams:        " + String(setts[1].boolval));
  Serial.println("health:       " + String(setts[2].numval));
  Serial.println("respawn time: " + String(setts[3].numval));
  Serial.println("time limit:   " + String(setts[4].numval));
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void teampage() {
  if (gameleader) {
    myteam = keystrokes("enter 2-char team ID", 2, 2, "settings", 4);
  } else {
    myteam = keystrokes("enter 2-char team ID", 2, 2, "entercode", 4);
  }
  if (myteam == "") { return; }

  tft.fillScreen(TFT_BLACK);

  if (gameleader) {
    tft.drawRect(125, 100, 70, 40, TFT_WHITE);  //Box at screen center (y = 100 puts it centered vertically if 240px tall)
    tft.drawString("start", 160, 120, 2);       //Text at (160,120) = screen center

    PNR();

    if (nowX > 125 && nowX < 195 && nowY > 100 && nowY < 140) {  //start
      delay(50);
      broadcast(startgame + game);
      screen = "gamepanel";
      return;
    }

  } else {
    tft.drawString("waiting for leader to start game", 160, 120, 2);
    for (;;) {
      if (screen == "gamepanel") { return; }
      delay(10);
    }
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void gamepanel() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("in game. yay", 160, 120, 2);
  for (;;) {
    delay(1000);
  }
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void setup() {
  Serial.begin(115200);
  Serial.println("\nPRIMARY");

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  xTaskCreatePinnedToCore(pollSPI, "pollSPI", 2048, NULL, 1, NULL, 0);

  initsettings();

  if (!Wire.begin()) {
    Serial.println(F("i2c failed"));
    while (1) {}
  } else {
    Serial.println("i2c good");
  }

  pinMode(14, INPUT_PULLUP);
  attachInterrupt(14, touchInterrupt, CHANGE);

  tft.init();
  tft.setRotation(3);
  tft.setTextDatum(4);

  xTaskCreatePinnedToCore(touchtask, "touchtask", 8192, NULL, 1, NULL, 0);
  screen = "wifisetup";

  touchSettings(15, 0, 1);
}
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////\\\\////
void loop() {
  validatemsg();
  msgController();
}
