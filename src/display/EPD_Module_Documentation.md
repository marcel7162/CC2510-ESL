# Dokumentation des EPD-Moduls (Electronic Paper Display)

Dieses Dokument beschreibt die Makros und Funktionen des Moduls `epd` (`epd.c` und `epd.h`), das die Ansteuerung des E-Paper-Displays übernimmt.

## Makros (`epd.h`)

Die Makros definieren die Hardware-Konfiguration, Pins und Display-Parameter.

### Pin- und Hardware-Definitionen
Diese Makros bilden abstrakte Namen auf die physischen Register des CC2510 ab.

*   **`B_PWR`, `B_CS`, `B_DC`, `B_BUSY`, `B_RESET`**: Bit-Position (0–7) im jeweiligen Port für jedes Steuersignal.
*   **`EPD_PWR` (P0_0)**: Versorgungspin des Displays.
*   **`EPD_CS` (P0_1)**: Chip Select für die SPI-Kommunikation.
*   **`EPD_DC` (P1_2)**: Data/Command. Legt fest, ob ein Befehl oder ein Datum gesendet wird.
*   **`EPD_BUSY` (P1_3)**: Eingang, zeigt an, ob das Display gerade beschäftigt ist.
*   **`EPD_RESET` (P2_0)**: Hardware-Reset-Pin.

### Zustandssteuerung (Low-Level)
Diese Makros vereinfachen das Umschalten der Steuerpins.

*   **`EPD_PWR_ON` / `EPD_PWR_OFF`**: Schaltet die Versorgung ein (`0`) oder aus (`1`). *Hinweis: nutzt offenbar Active-Low-Logik bzw. steuert einen PNP-/P-Kanal-MOSFET.*
*   **`EPD_RESET_ON` / `EPD_RESET_OFF`**: Aktiviert (`0`) oder deaktiviert (`1`) das Reset-Signal. Active Low.
*   **`EPD_SELECT` / `EPD_DESELECT`**: Aktiviert (`0`) oder deaktiviert (`1`) die SPI-Kommunikation mit dem Display.
*   **`EPD_CMDMODE`**: Setzt DC auf LOW (`0`) → es folgt ein **Befehl**.
*   **`EPD_DATAMODE`**: Setzt DC auf HIGH (`1`) → es folgen **Daten**.

### Display-Konfiguration
*   **`EPD_SIZE`**: Physische Displaygröße (z. B. 266 für 2,66 Zoll).
*   **`HRES` / `VRES`**: Horizontale/vertikale Auflösung, automatisch anhand von `EPD_SIZE` gesetzt.
*   **`BUFFER_SIZE`**: Größe in Bytes für ein komplettes Bild (`HRES / 8 * VRES`).

---

## Funktionen (`epd.c`)

### Initialisierung und Steuerung

#### `void epd_init()`
*   **Zweck:** Initialisiert das SPI-Peripherieteil (USART0) des CC2510 und die nötigen GPIOs. Schaltet dann das Display ein, führt Hardware- und Software-Reset durch und sendet Grundeinstellungen (z. B. Temperatursensor).
*   **Detail:** Setzt Baudrate, SPI-Modus (Master) und Pin-Richtungen. Muss als erste Funktion aufgerufen werden.

#### `void epdReset(uint32_t ms1, uint32_t ms2, uint32_t ms3, uint32_t ms4, uint32_t ms5)`
*   **Zweck:** Führt eine Hardware-Reset-Sequenz durch, indem der `RESET`-Pin getoggelt wird.
*   **Detail:** Erlaubt es, die Wartezeiten (in Millisekunden) zwischen den Zustandswechseln anzugeben — nützlich, um die vom Controller beim Einschalten geforderten Timings einzuhalten.

#### `void softReset()`
*   **Zweck:** Sendet einen Software-Reset-Befehl (`0x00`) an den Controller.
*   **Unterschied zu `epdReset`:** `epdReset` nutzt den physischen Reset-Pin, `softReset` eine digitale Instruktion. Bei der Initialisierung werden meist beide verwendet.

#### `void epd_waitBusy()`
*   **Zweck:** Blockiert die Programmausführung, bis das Display nicht mehr beschäftigt ist.
*   **Detail:** Überwacht den `EPD_BUSY`-Pin, mit einer kleinen Sicherheitsverzögerung am Ende. Nach zeitintensiven Befehlen (z. B. Refresh) zwingend aufzurufen, damit keine Daten gesendet werden, bevor das Display bereit ist.

#### `void DCDC_powerOn()`
*   **Zweck:** Sendet den Befehl (`0x04`) zum Einschalten des internen DC/DC-Wandlers.
*   **Detail:** Nötig, um die für die E-Tinte erforderlichen Hochspannungen zu erzeugen.

#### `void powerOff()`
*   **Zweck:** Schaltet den DC/DC-Wandler ab und versetzt Display und MCU-Pins in einen sicheren/sparsamen Zustand.
*   **Detail:** Nach jedem Display-Update aufzurufen, um Energie zu sparen und das Display zu schützen.

### Zeichnen und Aktualisieren

#### `void globalUpdate(const uint8_t * data1s, const uint8_t * data2s)`
*   **Zweck:** Sendet zwei vollständige Bildpuffer an das Display und löst dann die Aktualisierung aus.
*   **Detail:** E-Ink-Displays haben meist zwei Speicherpuffer (z. B. aktueller und nächster Zustand, oder Schwarz und Rot). Diese Funktion füllt beide und ruft `flushDisplay` auf.

#### `void epd_clearDisplay()`
*   **Zweck:** Löscht das Display (setzt es auf Weiß).
*   **Detail:** Sendet `0x00`-Bytes an den gesamten Displayspeicher (über `sendColor`) und aktualisiert dann. Schnelles Löschen ohne Bildpuffer im MCU-RAM.

#### `void flushDisplay()`
*   **Zweck:** Startet die Refresh-Sequenz (`0x12`).
*   **Detail:** Erst das lässt das Bild physisch wechseln. Davor werden nur Daten in den internen Speicher des Controllers geschrieben. Schaltet den DC/DC automatisch ein.

### Kommunikation (Low-Level)

#### `void sendIndexData(uint8_t index, const uint8_t *data, uint32_t len)`
*   **Zweck:** Sendet einen Befehl (`index`), gefolgt von einer Byte-Folge (`data`).
*   **Verwendung:** Zum Senden von Einstellungen oder Bildblöcken, die bereits in einem Array vorliegen.
*   **Besonderheit:** Wechselt automatisch zwischen Befehls- und Datenmodus und steuert Chip Select.

#### `void sendColor(uint8_t index, const uint8_t data, uint32_t len)`
*   **Zweck:** Sendet einen Befehl (`index`) und danach **dasselbe Byte** (`data`) `len`-mal wiederholt.
*   **Unterschied zu `sendIndexData`:** `sendIndexData` durchläuft ein Array verschiedener Daten; `sendColor` wiederholt einen einzigen Wert. Sehr effizient, um das Display einfarbig zu füllen (z. B. ganz weiß/schwarz), ohne MCU-RAM für einen Puffer zu verbrauchen.

#### `void sendCommand(uint8_t cmd)`
*   **Zweck:** Sendet ein einzelnes Byte als Befehl.
*   **Detail:** Setzt `EPD_DC` vor dem Senden auf LOW.

#### `void sendData(uint8_t data)`
*   **Zweck:** Sendet ein einzelnes Byte als Datum (Parameter eines Befehls).
*   **Detail:** Setzt `EPD_DC` vor dem Senden auf HIGH.
