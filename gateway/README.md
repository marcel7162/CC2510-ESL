# ESP32-Gateway für CC2510-ESL

Bindet die CC2510-E-Paper-Tags über Funk an **Home Assistant** an:

```
Home Assistant  ──(WiFi / MQTT)──►  ESP32  ──(SPI)──►  nRF24L01+PA+LNA  ──(2,4 GHz)──►  CC2510-Tag
```

> ⚠️ **Status: Gerüst.** WiFi-, MQTT- und nRF24-Grundgerüst plus das Chunk-Sende-Protokoll stehen. Noch offen: die passende **Tag-Gegenseite** in der CC2510-Firmware (empfängt die Pakete und streamt sie aufs EPD) sowie das Rendern/Liefern der Bilder auf HA-Seite. Siehe [Offene Punkte](#offene-punkte).

## Voraussetzungen
- **PlatformIO** (VS Code-Erweiterung oder CLI).
- **ESP32-DevKit**.
- **nRF24L01+PA+LNA** auf einem Adapter-Board mit eigenem 3,3-V-Regler **und** Stützkondensator (Elko). Der ESP32-Onboard-Regler ist für PA+LNA zu schwach.
- Ein **MQTT-Broker**, den Home Assistant erreicht (z. B. das Mosquitto-Add-on).

## Verdrahtung ESP32 ↔ nRF24 (VSPI)

| nRF24 | ESP32 | Hinweis |
|-------|-------|---------|
| VCC | 3,3 V | **nicht** 5 V; eigener Regler empfohlen |
| GND | GND | |
| CE  | GPIO 4 | `NRF_CE_PIN` in `config.h` |
| CSN | GPIO 5 | `NRF_CSN_PIN` in `config.h` |
| SCK | GPIO 18 | VSPI |
| MOSI | GPIO 23 | VSPI |
| MISO | GPIO 19 | VSPI |
| IRQ | — | nicht genutzt |

## Einrichten
1. `gateway/` in PlatformIO öffnen.
2. **`include/config.h` ausfüllen** — WLAN, MQTT und vor allem die **nRF24-Funkparameter**.
3. Bauen & flashen:
   ```bash
   pio run -t upload
   pio device monitor
   ```

## ⚠️ Wichtig: Funk muss zur CC2510-Seite passen
Gateway und Tag verstehen sich nur, wenn **Datenrate, Kanal, Adresse, CRC und Auto-ACK auf beiden Seiten identisch** sind. Trage in `config.h` genau die Werte ein, mit denen dein Aufbau stabil läuft.

Besonderheit: Ein **CC2510 im MSK-Modus** (so steht es aktuell in `src/rf/rf.c`) kann einen **nRF24 (GFSK) nicht** verstehen. Für diese Anbindung muss die CC2510-Seite auf **GFSK + ein nRF24-kompatibles Paketformat** umgestellt sein. Ebenso ist das ESB-Auto-ACK des nRF24 deaktiviert (`NRF_AUTO_ACK=false`), weil der CC2510 es nicht beherrscht — die Bestätigung läuft stattdessen als **Software-ACK** (siehe `protocol.h`).

## Funk-Protokoll
Definiert in [`include/protocol.h`](./include/protocol.h) — dieselbe Definition muss die Tag-Firmware implementieren.

| Paket | Aufbau | Bedeutung |
|-------|--------|-----------|
| `IMG_START` | `[0x01][plane][len_hi][len_lo]` | Start einer Farbebene → Tag: `epd_stream_start(plane)` |
| `IMG_DATA` | `[0x02][seq_hi][seq_lo][payload…]` | Datenchunk (per ACK bestätigt) |
| `IMG_END` | `[0x03][plane]` | Ebene fertig → Tag: `epd_stream_end()` |
| `COMMIT` | `[0x04]` | Anzeige aktualisieren → `epd_flushDisplay()` |
| `ACK` | `[0x80][seq_hi][seq_lo]` | Bestätigung vom Tag |

`plane`: `0x10` = S/W, `0x13` = Rot (= EPD-Kommandos). Ein Chunk trägt bis zu 29 Nutzbytes (nRF24-Payload 32 − 3 Header).

## Bild-Lieferung von Home Assistant
Das Gateway erwartet auf `esl/tag1/image` den **fertigen Framebuffer** (erst `TAG_PLANE_SIZE` Bytes S/W, dann `TAG_PLANE_SIZE` Bytes Rot). Das Rendern (Wetter, Haus-Infos → 1-bit-Bild in Tag-Auflösung) passiert **auf HA-Seite**, z. B. per Python/Pillow-Skript oder Add-on.

> Hinweis zur MQTT-Größe: Ein komplettes Bild kann mehrere KB groß sein. Für größere Displays entweder in mehreren MQTT-Nachrichten übertragen oder das Bild per **HTTP** von HA ziehen, statt alles in eine MQTT-Nachricht zu packen.

## Offene Punkte
- [ ] **Tag-Gegenseite** in der CC2510-Firmware: Protokoll empfangen und via `epd_stream_*` aufs Display streamen, ACKs senden.
- [ ] CC2510-RF ggf. auf **GFSK/nRF24-kompatibel** umstellen (falls noch MSK).
- [ ] **Steuerpakete** (START/END/COMMIT) ebenfalls bestätigen (aktuell nur IMG_DATA).
- [ ] HA-seitiges **Rendering** + Bild-Lieferung (MQTT-Häppchen oder HTTP).
- [ ] Fenster-/Burst-ACK statt Stop-and-Wait für mehr Durchsatz (optional).
