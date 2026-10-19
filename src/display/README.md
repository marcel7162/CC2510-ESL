# Befehle des Display-Controllers (Serie UC81xx)

Dieses Dokument beschreibt alle Befehle, die der E-Paper-Display-Controller (Serie UC81xx, z. B. UC8151/UC8176) unterstützt. Die Befehle werden über den SPI-Bus gesendet, wobei der Pin **DC** (Data/Command) für den Befehlsindex auf LOW (`0`) und für dessen Parameter auf HIGH (`1`) liegt.

## Schnellübersicht

| Hex  | Befehl (Name) | Parameter | Beschreibung |
| :--- | :--- | :---: | :--- |
| `0x00` | **PSR** (Panel Setting) | 2 | Auflösung, Farbmodus usw. |
| `0x01` | **PWR** (Power Setting) | 5 | Spannungen VGH, VGL, VDH, VDL. |
| `0x02` | **POF** (Power OFF) | 0 | Schaltet DC/DC ab und stoppt die PWM-Ausgabe. |
| `0x03` | **PFS** (Power OFF Sequence) | 1 | Abfallzeit der Spannungen. |
| `0x04` | **PON** (Power ON) | 0 | Schaltet DC/DC ein, aktiviert die Spannungsausgänge. |
| `0x05` | **PMES** (Power ON Measure) | 0 | Misst interne Spannungen (Selbstdiagnose). |
| `0x06` | **BTST** (Booster Soft Start) | 3 | Sanftanlauf der Booster. |
| `0x07` | **DSLP** (Deep Sleep) | 1 | Ultra-Niedrigverbrauchsmodus. |
| `0x10` | **DTM1** (Data Start Transmission 1) | Buffer | Sendet das erste Frame (Schwarz/Weiß). |
| `0x11` | **DRF** (Display Refresh) | 0 | Startet den physischen Refresh des Displays. |
| `0x12` | **DSP** (Display Start) | 0 | Ähnlich wie 0x11, startet die Anzeige. |
| `0x13` | **DTM2** (Data Start Transmission 2) | Buffer | Sendet das zweite Frame (Rot/Farbe). |
| `0x20` | **LUTC** (VCOM LUT) | variabel | Look-Up-Table für VCOM-Spannungen. |
| `0x21` | **LUTW** (White LUT) | variabel | LUT für Übergänge nach Weiß. |
| `0x22` | **LUTB** (Black LUT) | variabel | LUT für Übergänge nach Schwarz. |
| `0x23` | **LUTR** (Red LUT) | variabel | LUT für Übergänge nach Rot. |
| `0x24` | **LUTV** (VBD LUT) | variabel | LUT für den Rand (Border). |
| `0x30` | **OSC** (PLL Control) | 1 | Frequenz des internen Oszillators. |
| `0x40` | **TSC** (Temperature Sensor) | 1 | Liest/wählt den Temperatursensor. |
| `0x50` | **CDI** (VCOM and Data Interval) | 1 | VCOM- und Rand-Datenintervall. |
| `0x60` | **TCON** (TCON Setting) | 1 | Verzögerungen S2G und G2S. |
| `0x61` | **TRES** (Resolution Setting) | 3 | Auflösung des Displays. |
| `0x82` | **VDCS** (VCOM DC Setting) | 1 | DC-Spannung von VCOM. |
| `0xE0` | **CASCADE** (Cascade Setting) | 1 | Kaskadenmodus für mehrere Displays. |
| `0xE5` | **TSSET** (Temperature Set) | 1 | Setzt die Arbeitstemperatur manuell. |

---

## Details der wichtigsten Befehle

### `0x00` PSR (Panel Setting)
Konfiguriert grundlegende Panel-Eigenschaften.
- **Byte 1**: `[RES1 RES0] [REG] [KW/R] [UD] [SHL] [SHD_N] [RST_N]`
    - `RES[1:0]`: Auflösung (z. B. 00=160x296, 01=128x296, 11=TRES legt fest).
    - `REG`: 0=extern, 1=intern (LDO).
    - `KW/R`: 0=S/W/R (3 Farben), 1=S/W (2 Farben).
    - `UD`: 0=Scan oben→unten, 1=unten→oben.
    - `SHL`: 0=Scan links→rechts, 1=rechts→links.
    - `SHD_N`: 0=Booster AUS, 1=Booster AN.
    - `RST_N`: 0=Soft-Reset, 1=Normal.

### `0x10` und `0x13` Bildübertragung
- **`0x10` (DTM1)**: Empfängt `(Breite / 8) * Höhe` Bytes. Jedes Bit `1` = Weiß, `0` = Schwarz.
- **`0x13` (DTM2)**: Bei 3-Farben-Displays definiert dieser Puffer den Rot-Anteil.

### `0x50` CDI (VCOM and Data Interval Setting)
Entscheidend für das Aussehen des Rands und den Refresh.
- **Byte 1**: `[VBD1 VBD0] [DDX1 DDX0] [CDI3 CDI2 CDI1 CDI0]`
    - `VBD`: Randfarbe (S/W/R).
    - `DDX`: Datensteuerung (invertiert oder nicht).
    - `CDI`: Intervallzeit zwischen Frames.

---

## Nutzung in diesem Projekt

In [`epd.c`](./epd.c) werden diese Befehle über Komfortfunktionen umgesetzt:

1.  **Initialisierung**: `epd_init()` sendet `0x00`, `0xE0`, `0xE5` u. a., um das Panel vorzubereiten.
2.  **Löschen**: `epd_clearDisplay()` nutzt `0x10` und `0x13` und sendet Nullen, um den Speicher zu leeren.
3.  **Aktualisierung**: `epd_flushDisplay()` ruft `0x04` (Power ON) auf, sendet `0x11` (Refresh), wartet auf den `BUSY`-Pin und ruft schließlich `0x02` (Power OFF) auf.
4.  **Streaming**: `epd_stream_start(index)` beginnt eine Bildübertragung (mit `0x10` oder `0x13`) und erlaubt, die Daten stückweise zu senden.

## Sicherheits- und Timing-Hinweise
- Nach Befehlen wie `0x04` (Power ON), `0x11`/`0x12` (Refresh) und `0x00` (Reset) stets den **BUSY**-Pin prüfen (`0` = beschäftigt).
- Spannungsbefehle (`0x01`, `0x82`) können das Display beschädigen, wenn sie außerhalb der Herstellerbereiche gesetzt werden.

---

## Teil-Refresh (Partial Refresh)

Um nur einen Teil des Displays zu aktualisieren (z. B. eine Uhr oder einen Zähler), ohne dass das ganze Display blinkt, dient der Modus **Partial Window**.

### Konzeptionelle Schritte:
1.  **Teilmodus aktivieren**: Befehl `0x91` (Partial In) senden.
2.  **Fenster definieren**: Befehl `0x90` (Partial Window) mit den Koordinaten senden:
    - `HRST` (Horizontal Start), `HRED` (Horizontal End)
    - `VRST` (Vertical Start), `VRED` (Vertical End)
    - `PT_SCAN` (Scan-Modus)
3.  **Daten senden**: Nur die Bytes des betreffenden Bereichs über `0x10` (bzw. den Datenübertragungsbefehl) senden.
4.  **Refreshen**: `0x11` (Display Refresh) senden.
5.  **Teilmodus verlassen**: `0x92` (Partial Out) senden.

### Das „Flicker"-Problem
Standardmäßig führt `0x11`/`0x12` einen vollständigen Reinigungszyklus (Weiß-Schwarz-Weiß) durch, um Geisterbilder zu vermeiden. Für eine „saubere" Aktualisierung (ohne Blinken) braucht der Controller eine spezifische **LUT (Look-Up Table)** in seinen Registern.

> [!NOTE]
> Bei vielen 3-Farben-Displays (S/W/R) ist ein Teil-Refresh nicht oder nur in Schwarz-Weiß möglich, da das rote Pigment sehr lange, aggressive Spannungszyklen benötigt, die die gesamte Paneloberfläche betreffen.

### Beispielimplementierung (C):
```c
void epd_setPartialWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    uint8_t params[9];

    // Koordinaten setzen (Beispiel für UC8176)
    params[0] = x >> 8; params[1] = x & 0xF8; // HRST (X muss Vielfaches von 8 sein)
    params[2] = (x + w - 1) >> 8; params[3] = (x + w - 1) | 0x07; // HRED
    params[4] = y >> 8; params[5] = y & 0xFF; // VRST
    params[6] = (y + h - 1) >> 8; params[7] = (y + h - 1) & 0xFF; // VRED
    params[8] = 0x01; // PT_SCAN

    epd_sendIndexData(0x90, params, 9);
}
```
