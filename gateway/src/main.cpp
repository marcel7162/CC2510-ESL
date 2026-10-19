/*
 * ESP32-Gateway fuer CC2510-ESL
 * -----------------------------------------------------------------------------
 *  Home Assistant  --(WiFi/MQTT)-->  ESP32  --(nRF24, Funk)-->  CC2510-Tag
 *
 *  Ablauf:
 *   1. Mit WLAN und MQTT-Broker verbinden.
 *   2. Auf MQTT_TOPIC_IMAGE warten: HA schickt den fertigen Framebuffer
 *      (TAG_PLANE_SIZE Bytes S/W, danach TAG_PLANE_SIZE Bytes Rot).
 *   3. Framebuffer per Funk-Chunk-Protokoll (protocol.h) an den Tag senden,
 *      jeder Chunk per Software-ACK bestaetigt.
 *
 *  HINWEIS: Dies ist ein Geruest. Offene Punkte sind mit TODO markiert; die
 *  Funk-Parameter in config.h muessen zur CC2510-Seite passen.
 * -----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <RF24.h>

#include "config.h"
#include "protocol.h"

// --- Globale Objekte --------------------------------------------------------
static RF24 radio(NRF_CE_PIN, NRF_CSN_PIN);
static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);

// Puffer fuer ein komplettes Bild (S/W + Rot). Fuer grosse Displays ggf.
// stattdessen streamen statt komplett im RAM zu halten.
static uint8_t imageBuf[TAG_IMAGE_SIZE];
static size_t  imageLen = 0;   // wie viele Bytes aktuell gueltig sind

// ===========================================================================
//  WLAN
// ===========================================================================
static void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WLAN verbinden");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
  }
  Serial.printf("\nWLAN OK, IP: %s\n", WiFi.localIP().toString().c_str());
}

// ===========================================================================
//  nRF24
// ===========================================================================
static void radioInit() {
  if (!radio.begin()) {
    Serial.println("FEHLER: nRF24 nicht gefunden (Verdrahtung/Stromversorgung pruefen)");
    return;
  }
  radio.setPALevel(NRF_PA_LEVEL);
  radio.setDataRate(NRF_DATA_RATE);
  radio.setChannel(NRF_CHANNEL);
  radio.setCRCLength(NRF_CRC_LENGTH);
  radio.setAutoAck(NRF_AUTO_ACK);         // aus: CC2510 kann kein ESB-Auto-ACK
  if (NRF_PAYLOAD_SIZE > 0) {
    radio.setPayloadSize(NRF_PAYLOAD_SIZE);
  } else {
    radio.enableDynamicPayloads();
  }
  radio.openWritingPipe(NRF_ADDR_TO_TAG);
  radio.openReadingPipe(1, NRF_ADDR_FROM_TAG);
  radio.stopListening();
  Serial.println("nRF24 initialisiert");
}

// Sendet ein Paket und wartet auf das Software-ACK des Tags.
// Gibt true zurueck, sobald das ACK mit passender Sequenznummer eintrifft.
static bool sendWithAck(const uint8_t *pkt, uint8_t len, uint16_t expectSeq) {
  for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++) {
    radio.stopListening();
    radio.write(pkt, len);          // mit AutoAck=off kehrt write() sofort zurueck

    // Auf ACK lauschen
    radio.startListening();
    uint32_t start = millis();
    while (millis() - start < ACK_TIMEOUT_MS) {
      if (radio.available()) {
        uint8_t ack[RF_MAX_PAYLOAD];
        radio.read(ack, sizeof(ack));
        if (ack[0] == PKT_ACK) {
          uint16_t seq = ((uint16_t)ack[1] << 8) | ack[2];
          if (seq == expectSeq) {
            radio.stopListening();
            return true;
          }
        }
      }
    }
    // kein (passendes) ACK -> erneut senden
  }
  radio.stopListening();
  return false;
}

// Sendet ein Steuerpaket ohne ACK-Erwartung (IMG_START/END/COMMIT).
// TODO: Auch Steuerpakete sollten bestaetigt werden, sobald die Tag-Seite steht.
static void sendControl(const uint8_t *pkt, uint8_t len) {
  radio.stopListening();
  radio.write(pkt, len);
}

// Sendet eine Farbebene in Chunks.
static bool sendPlane(uint8_t plane, const uint8_t *data, uint16_t planeLen) {
  // IMG_START
  uint8_t start[4] = { PKT_IMG_START, plane,
                       (uint8_t)(planeLen >> 8), (uint8_t)(planeLen & 0xFF) };
  sendControl(start, sizeof(start));
  delay(5);

  // IMG_DATA-Chunks
  uint16_t seq = 0;
  for (uint16_t off = 0; off < planeLen; off += DATA_CHUNK_LEN, seq++) {
    uint8_t chunkLen = (planeLen - off >= DATA_CHUNK_LEN) ? DATA_CHUNK_LEN
                                                          : (planeLen - off);
    uint8_t pkt[RF_MAX_PAYLOAD];
    pkt[0] = PKT_IMG_DATA;
    pkt[1] = (uint8_t)(seq >> 8);
    pkt[2] = (uint8_t)(seq & 0xFF);
    memcpy(&pkt[DATA_HEADER_LEN], &data[off], chunkLen);

    if (!sendWithAck(pkt, DATA_HEADER_LEN + chunkLen, seq)) {
      Serial.printf("ABBRUCH: Chunk %u (Ebene 0x%02X) nicht bestaetigt\n", seq, plane);
      return false;
    }
  }

  // IMG_END
  uint8_t end[2] = { PKT_IMG_END, plane };
  sendControl(end, sizeof(end));
  delay(5);
  return true;
}

// Komplettes Bild senden: S/W-Ebene, Rot-Ebene, dann Commit (Refresh).
static void sendImage() {
  if (imageLen < TAG_IMAGE_SIZE) {
    Serial.printf("Bild unvollstaendig (%u/%u Bytes) - nicht gesendet\n",
                  (unsigned)imageLen, (unsigned)TAG_IMAGE_SIZE);
    return;
  }
  Serial.println("Sende Bild an Tag...");
  bool ok = sendPlane(PLANE_BW,  &imageBuf[0],              TAG_PLANE_SIZE);
  ok = ok && sendPlane(PLANE_RED, &imageBuf[TAG_PLANE_SIZE], TAG_PLANE_SIZE);

  if (ok) {
    uint8_t commit[1] = { PKT_COMMIT };
    sendControl(commit, sizeof(commit));
    Serial.println("Bild gesendet, Commit/Refresh ausgeloest.");
    mqtt.publish(MQTT_TOPIC_STATUS, "ok");
  } else {
    mqtt.publish(MQTT_TOPIC_STATUS, "error");
  }
}

// ===========================================================================
//  MQTT
// ===========================================================================
static void onMqtt(char *topic, uint8_t *payload, unsigned int length) {
  if (strcmp(topic, MQTT_TOPIC_IMAGE) == 0) {
    // HA liefert den rohen Framebuffer (S/W + Rot).
    // Achtung MQTT-Groesse: grosse Bilder ggf. in Haeppchen uebertragen oder
    // per HTTP ziehen (siehe README). Hier: ein Topic = ein komplettes Bild.
    if (length > sizeof(imageBuf)) length = sizeof(imageBuf);
    memcpy(imageBuf, payload, length);
    imageLen = length;
    Serial.printf("Bild empfangen: %u Bytes\n", length);
    sendImage();
  } else if (strcmp(topic, MQTT_TOPIC_CMD) == 0) {
    if (length >= 7 && strncmp((char *)payload, "refresh", 7) == 0) {
      uint8_t commit[1] = { PKT_COMMIT };
      sendControl(commit, sizeof(commit));
    }
    // TODO: weitere Kommandos
  }
}

static void mqttConnect() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqtt);
  mqtt.setBufferSize(MQTT_MAX_PACKET_SIZE);
  while (!mqtt.connected()) {
    Serial.print("MQTT verbinden...");
    if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println(" OK");
      mqtt.subscribe(MQTT_TOPIC_IMAGE);
      mqtt.subscribe(MQTT_TOPIC_CMD);
      mqtt.publish(MQTT_TOPIC_STATUS, "online");
    } else {
      Serial.printf(" Fehler rc=%d, neuer Versuch in 2s\n", mqtt.state());
      delay(2000);
    }
  }
}

// ===========================================================================
//  setup / loop
// ===========================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nCC2510-ESL Gateway startet...");
  wifiConnect();
  radioInit();
  mqttConnect();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) wifiConnect();
  if (!mqtt.connected()) mqttConnect();
  mqtt.loop();
}
