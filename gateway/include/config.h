/*
 * config.h  —  Zentrale Konfiguration des ESP32-Gateways.
 *
 * >>> HIER deine Werte eintragen. <<<
 *
 * WICHTIG — Funk-Parameter:
 * Die nRF24-Einstellungen MUESSEN exakt zu dem passen, womit dein CC2510-Tag
 * laeuft. Nur wenn Datenrate, Kanal, Adresse, CRC und Auto-ACK auf beiden
 * Seiten identisch sind, verstehen sich Gateway und Tag.
 *
 * Hinweis: Ein CC2510 im MSK-Modus (Repo-Standard in src/rf/rf.c) kann einen
 * nRF24 (GFSK) NICHT verstehen. Die CC2510-Seite muss dafuer auf GFSK + ein
 * nRF24-kompatibles Paketformat umgestellt sein. Trage unten die Werte deines
 * funktionierenden Aufbaus ein.
 */

#ifndef GATEWAY_CONFIG_H
#define GATEWAY_CONFIG_H

#include <RF24.h>

// ---------------------------------------------------------------------------
//  WLAN
// ---------------------------------------------------------------------------
#define WIFI_SSID      "DEIN_WLAN"
#define WIFI_PASSWORD  "DEIN_WLAN_PASSWORT"

// ---------------------------------------------------------------------------
//  MQTT (Home Assistant Broker, z. B. Mosquitto-Add-on)
// ---------------------------------------------------------------------------
#define MQTT_HOST      "192.168.1.10"   // IP/Hostname des Brokers
#define MQTT_PORT      1883
#define MQTT_USER      "esl-gateway"
#define MQTT_PASSWORD  "GEHEIM"
#define MQTT_CLIENT_ID "cc2510-esl-gateway"

// Topics
#define MQTT_TOPIC_IMAGE   "esl/tag1/image"   // eingehende Bilddaten (Framebuffer)
#define MQTT_TOPIC_STATUS  "esl/tag1/status"  // Status zurueck an HA
#define MQTT_TOPIC_CMD     "esl/tag1/cmd"      // Kommandos (z. B. "refresh")

// ---------------------------------------------------------------------------
//  Display-Aufloesung des Ziel-Tags (muss zu EPD_SIZE in src/display/epd.h passen)
//  2.13": 104x212 | 2.6": 152x296 | 4.2": 400x300
// ---------------------------------------------------------------------------
#define TAG_HRES        152
#define TAG_VRES        296
#define TAG_PLANE_SIZE  (TAG_HRES / 8 * TAG_VRES)   // Bytes pro Farbebene
#define TAG_IMAGE_SIZE  (TAG_PLANE_SIZE * 2)        // S/W + Rot

// ---------------------------------------------------------------------------
//  nRF24-Verdrahtung am ESP32 (SPI)
//  Standard-VSPI: SCK=18, MISO=19, MOSI=23. CE/CSN frei waehlbar.
// ---------------------------------------------------------------------------
#define NRF_CE_PIN     4
#define NRF_CSN_PIN    5

// ---------------------------------------------------------------------------
//  nRF24-Funkparameter  —  MUESSEN zur CC2510-Seite passen!
// ---------------------------------------------------------------------------
#define NRF_CHANNEL        76            // 0..125 (2400 + ch MHz). An CC2510 anpassen.
#define NRF_DATA_RATE      RF24_250KBPS  // RF24_250KBPS | RF24_1MBPS | RF24_2MBPS
#define NRF_PA_LEVEL       RF24_PA_HIGH  // mit PA+LNA: HIGH oder MAX
#define NRF_CRC_LENGTH     RF24_CRC_16   // RF24_CRC_8 | RF24_CRC_16 | RF24_CRC_DISABLED
#define NRF_AUTO_ACK       false         // ESB-Auto-ACK AUS: CC2510 kann es nicht,
                                         // ACK wird in Software gemacht (siehe protocol.h)
#define NRF_PAYLOAD_SIZE   32            // feste Payload (0 = dynamisch). 32 = Maximum.

// 5-Byte-Adressen (Pipes). An die CC2510-Seite anpassen.
static const uint8_t NRF_ADDR_TO_TAG[5]   = { 'E', 'S', 'L', '0', '1' }; // Gateway -> Tag
static const uint8_t NRF_ADDR_FROM_TAG[5] = { 'E', 'S', 'L', '0', '2' }; // Tag -> Gateway (ACKs)

#endif // GATEWAY_CONFIG_H
