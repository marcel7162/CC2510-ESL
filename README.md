# CC2510-ESL — Firmware für elektronische Preisschilder

> **ℹ️ Fork-Hinweis:** Dies ist **nicht meine eigene Arbeit**, sondern ein **Fork von [Loryuwu/CC2510-ESL](https://github.com/Loryuwu/CC2510-ESL)**, das ich für meine eigenen Bedürfnisse **erweitere** — Ziel: die Tags als Display für **Home Assistant** (Wetter & Haus-Infos) über Funk zu nutzen. Die ursprüngliche Firmware stammt von Loryuwu und basiert ihrerseits auf der Arbeit von [@andrei-tatar](https://github.com/andrei-tatar/imagotag-hack). Alle Credits siehe [unten](#-credits).
>
> 🇪🇸 Spanische Originalversion des READMEs: [README.es.md](./README.es.md)

![ESLs](./img/ESLs.jpg)

Custom-Firmware für **elektronische Preisschilder (ESLs / Electronic Shelf Labels)** mit 2,2"- oder 4,2"-E-Paper-Display (einige 2,6"-Tags nutzen denselben MCU, aber nicht alle).
Herzstück ist der **CC2510** von Texas Instruments — ein MCU auf Basis des alten 8051 mit integriertem **2,4-GHz-RF-Transceiver**.

Das hier ist bewusst ein **Test- und Entwicklungsprojekt**, kein fertiges Produkt. Ziel ist, diese Tags zweckzuentfremden und als frei steuerbare E-Paper-Displays zu nutzen. Geplant sind separate Repos für den Access Point (AP), die Satelliten und die Steuersoftware; sobald die online sind, wird hier verlinkt.

> ℹ️ **Hinweis zur Dokumentation:** Alle Dateien im Ordner [`documentacion/`](./documentacion/) sowie die Modul-READMEs unter `src/*/` wurden mit KI-Unterstützung erstellt. Sie sind als **Orientierungshilfe** gedacht, nicht als zu 100 % verlässliche Quelle — im Zweifel gilt der Code.

---

## 📊 Projektstatus

| Modul | Status | Kann |
|-------|:------:|------|
| **LED** (RGB + weiß) | ✅ fertig | An/Aus/Toggle, Boost-Converter (TPS61071) |
| **EPD** (E-Paper) | ✅ fertig | Init, löschen, S/W- + Rot-Bilder, Teil-Refresh, Rechtecke, Streaming |
| **COBS / UART** | ✅ fertig | Serielles Protokoll mit COBS-Framing (P0_4/P0_5), inkl. Python-Skript |
| **NFC** (NT3H2111 via I2C) | ✅ funktioniert | Chip scannen, lesen, schreiben, löschen |
| **RF** (2,4 GHz) | ⚠️ teilweise | `rf_init/send/receive` vorhanden, hängt sich aber gelegentlich auf |
| **Flash** (W25X10 SPI) | ❗ in Arbeit | Bild aus externem Flash lesen → aufs Display streamen |
| **Deep Sleep** | ❌ nicht funktional | spart noch nicht wie gewünscht Strom |

> ⚠️ **Pin-Konflikte:** Der CC2510 hat zu wenige Pins. RGB-LED, SPI-Flash und UART/NFC teilen sich teils dieselben Leitungen — sie lassen sich daher **nicht gleichzeitig** nutzen. Je nach Aufgabe muss umgeschaltet werden (z. B. `#define RGB` auskommentieren, um die Flash zu verwenden). Details siehe [Pinout](#-pinout-des-cc2510).

---

## 📦 Voraussetzungen

**Software**
- [**SDCC**](https://sdcc.sourceforge.net/) — C-Compiler für 8-Bit-MCUs (8051), erzeugt das flashbare `.hex`.
- **Make** — automatisiert die Kompilierung über das mitgelieferte `Makefile`.
- **SmartRF Flash Programmer (v1, nicht v2)** — von TI kostenlos (Account nötig), flasht das `.hex` über den CC-Debugger.
- *Optional:* **SmartRF Studio** (RF-Parameter konfigurieren/exportieren), **SmartRF Packet Sniffer** (RF-Pakete mitlesen).

**Hardware**
- **CC-Debugger** zum Flashen (auf AliExpress ~10 $). Es gibt Bastellösungen mit Arduino Nano / ESP32 als Debugger — der echte CC-Debugger erspart aber viel Nervenkram.

![Debugger](./img/CCDebugger.jpg)

---

## 🚀 Kompilierung

Das `Makefile` erkennt die Plattform automatisch (Linux/macOS **und** Windows) — einfach im Projektverzeichnis:

```bash
make
```

Erfolgreich sieht das Ende so aus:

```text
packihx firmware.ihx > firmware.hex
packihx: read 829 lines, wrote 1624: OK.
```

Danach liegt ein neues `firmware.hex` im Hauptverzeichnis. Generierte Zwischendateien aufräumen:

```bash
make clean
```

---

## ⚡ Flashen

Zum Flashen den CC-Debugger mit 5 Drähten an den Chip anlöten: 2× Versorgung und 3× Daten (Clock, Data, Reset). Welche Lötpunkte dafür nötig sind, zeigen die Bilder.

![Anschluss](./img/Coneccion.jpg)

Ablauf im **SmartRF Flash Programmer (v1)**:
1. CC-Debugger an PC und ESL anschließen (LED am Debugger sollte grün sein).
2. Reiter „System-on-Chip" wählen → Gerät **CC2510** erscheint in der Liste.
3. Bei „Flash image" das erzeugte `firmware.hex` auswählen.
4. Unter „Actions" **„Erase, program and verify"** anhaken.
5. „Perform actions" klicken.

---

## 🔌 Pinout des CC2510

### Port 0
| Pin | Funktion |
|-----|----------|
| P0_0 | EPD Power Enable (active low über P-Kanal-FET) |
| P0_1 | EPD CS (SPI Chip Select) |
| P0_2 | Debug-Port |
| P0_3 | EPD SDI (SPI Serial Data In) |
| P0_4 | SDA zum NFC-Chip **und** UART TX |
| P0_5 | EPD CLK (SPI Clock) **und** UART RX |
| P0_6 | SCL zum NFC-Chip |
| P0_7 | *nicht verbunden* |

### Port 1
| Pin | Funktion |
|-----|----------|
| P1_0 | NFC VCC (versorgt offenbar NFC-Chip und SPI-Flash) |
| P1_1 | NFC FD (Field Detection) |
| P1_2 | EPD DC (Data/Command) |
| P1_3 | EPD BUSY |
| P1_4 | SPI-Flash CS |
| P1_5 | SPI-Flash CLK **und** rote LED (RGB) |
| P1_6 | SPI-Flash MOSI **und** blaue LED (RGB) |
| P1_7 | SPI-Flash MISO **und** grüne LED (RGB) |

### Port 2
| Pin | Funktion |
|-----|----------|
| P2_0 | EPD Reset |
| P2_1 | Debug Data **und** weiße LED |
| P2_2 | Debug Clock **und** LED-Boost-Enable (TPS61071) |

---

## 🗂️ Projektstruktur

```text
CC2510-ESL/
├── Makefile              # Build (plattformübergreifend)
├── README.md             # dieses Dokument
├── README.es.md          # spanisches Original
├── cobs_test.py          # PC-Skript für die COBS/UART-Kommunikation
├── firmware.hex          # fertiges Image zum Flashen
├── doc/                  # Datenblätter (CC2510, NFC, EPD, Flash, Boost)
├── documentacion/        # ausführliche Moduldoku (KI-erstellt)
├── gateway/              # ESP32-Gateway (PlatformIO): HA ⇄ Funk ⇄ Tag
├── img/                  # Bilder
└── src/
    ├── main.c            # Einstieg + Demo-Auswahl (siehe unten)
    ├── hal/              # Hardware-Abstraktion: clock, time, led, uart, isr
    ├── display/          # EPD-Treiber (E-Paper)
    ├── flash/            # SPI-Flash-Treiber (Winbond W25X10)
    ├── rf/               # 2,4-GHz-RF-Treiber
    ├── cobs/             # COBS-Framing
    ├── nfc/              # NFC (NT3H2111) + Software-I2C
    └── image_data/       # Testbilder als Byte-Arrays
```

### Demo auswählen
`src/main.c` ist eine Test-/Demo-Umgebung. Oben in der Datei legt das Makro `DEMO` fest, welcher Block kompiliert wird:

```c
#define DEMO  DEMO_DISPLAY   // DEMO_DISPLAY | DEMO_RF_TX | DEMO_RF_RX |
                             // DEMO_FLASH   | DEMO_NFC_SCAN | DEMO_NFC_RW |
                             // DEMO_COBS    | DEMO_UART | DEMO_EPD_CYCLE |
                             // DEMO_DEEPSLEEP
```

---

## 📡 Gateway (Home-Assistant-Anbindung)

Das Ziel dieses Forks: die Tags als Display für Home Assistant über Funk nutzen. Dafür gibt es ein **ESP32-Gateway** (PlatformIO) unter [`gateway/`](./gateway/):

```
Home Assistant ──(WiFi/MQTT)──► ESP32 ──(SPI)──► nRF24L01+PA+LNA ──(2,4 GHz)──► CC2510-Tag
```

Aufbau, Verdrahtung, Funk-Protokoll und offene Punkte stehen in der [Gateway-README](./gateway/README.md).

## ✨ Weiterführende Dokumentation

- [CC2510 — MCU + Radio (8051)](./doc/cc2510.pdf) — Herz des Projekts
- [NT3H2111 — NFC](./doc/NT3H2111_2211.pdf) — per I2C programmierbar
- [Pervasive Displays — 2.13"/4.17" EPD](./doc/Pervasives.pdf) — die verwendeten 3-Farben-Screens (Spectra Red „SE2xxxJS0E1") sind offenbar abgekündigt; im `doc/`-Ordner liegen zwei Treiber-Bibliotheken als `.zip`
- [TPS61071 — Boost-Converter](./doc/tps61071.pdf) — Versorgung der LEDs
- [W25X10CL — Winbond 1 Mbit NOR SPI-Flash](./doc/w25x10cl.pdf) — externe Flash per SPI
- [Ausführliche Moduldoku](./documentacion/01_Resumen_General.md) *(KI-erstellt, als Orientierung)*

---

## 🤝 Mitmachen & Hilfe

Dieses Projekt entsteht als Hobby — Elektronik aus Leidenschaft, ohne formale Ausbildung. Wer hier weiterkommt, Ideen hat oder helfen möchte, ist herzlich eingeladen, sich zu melden. Besonders das **RF-Modul** (gelegentliches Hängenbleiben) und **Deep Sleep** (zu hoher Verbrauch) brauchen noch Zuwendung.

> 💬 *Persönliche Notiz des ursprünglichen Autors (Loryuwu):* „Ich bin kein Experte und habe nie einen Programmierkurs belegt — was ich weiß, kommt aus Recherche, Lesen und Nachfragen. Wenn jemand das Projekt voranbringt, würde ich mich riesig über Austausch freuen."

---

## 📄 Lizenz

[MIT](./LICENSE) — mit Hinweis auf das zugrunde liegende, unlizenzierte Ursprungsprojekt von [@andrei-tatar](https://github.com/andrei-tatar/imagotag-hack). Details in der [LICENSE](./LICENSE)-Datei.

---

## 🙏 Credits

- **[Loryuwu](https://github.com/Loryuwu)** — ursprünglicher Autor dieser Firmware. Der Großteil des Codes und der Dokumentation stammt von ihm; dieses Repo ist nur ein Fork von **[Loryuwu/CC2510-ESL](https://github.com/Loryuwu/CC2510-ESL)**, den ich erweitere. (Loryuwu hat außerdem passende Repos für die Gegenseite: eine ESP32-Firmware und eine Desktop-App zum Bild-Senden.)
- **[@andrei-tatar](https://github.com/andrei-tatar/imagotag-hack)** — Grundlage, auf der Loryuwus Firmware aufbaut. Vielen Dank für die enorme Vorarbeit.
- **[@jirkabalhar](https://blog.jirkabalhar.cz/2023/12/hacking-sesimagotag-e-ink-price-tag)** — Blog über das Ansteuern des reinen Screens eines SES-imagotag-Tags.
