# NFC-Modul (NT3H2111)

Dieses Verzeichnis enthält die Treiber zur Ansteuerung des Chips **NT3H2111 (NTAG I2C Plus)** über den CC2510.
Der Treiber implementiert einen **Software-I2C-Bus (Bit-Banging)**, bedingt durch die spezielle Pin-Belegung der Hardware.

## Verdrahtung (Pinout)

| Signal | CC2510-Pin | Anmerkungen |
|--------|------------|-------------|
| **SDA** | P0.4 | geteilt mit UART TX |
| **SCL** | P0.6 | |
| **VCC** | P1.0 | schaltbare Versorgung (Power-Pin) |
| **FD**  | P1.1 | Field Detection (Feld-Interrupt) |

> ⚠️ **Warnung:** Da `P0.4` zwischen **NFC SDA** und **UART TX** geteilt wird, darf die UART nicht gleichzeitig mit dem NFC-Modul genutzt werden — oder die Pins müssen vor jeder Operation passend umkonfiguriert werden.

---

## 1. I2C-Treiber (`i2c.c` / `i2c.h`)

Implementiert das Low-Level-Protokoll.

### `void i2c_init(void)`
**Beschreibung:** Konfiguriert SDA und SCL als Ausgänge und setzt sie in den Ruhezustand (HIGH).
- **Verwendung:** Einmal am Anfang aufrufen, oder vor Transaktionen, falls die Pins zwischenzeitlich anderweitig (z. B. UART) genutzt wurden.

### `void i2c_start(void)`
**Beschreibung:** Erzeugt die **START**-Bedingung (SDA fällt, während SCL HIGH ist).
- **Zweck:** Beginn einer Transaktion.

### `void i2c_stop(void)`
**Beschreibung:** Erzeugt die **STOP**-Bedingung (SDA steigt, während SCL HIGH ist).
- **Zweck:** Transaktion beenden und Bus freigeben.

### `bool i2c_write_byte(uint8_t byte)`
**Beschreibung:** Sendet ein Byte (8 Bit) über den Bus.
- **Parameter:** `byte` — zu sendendes Datum.
- **Rückgabe:** `true` bei **ACK** (Datum korrekt empfangen), `false` bei **NACK**.

### `uint8_t i2c_read_byte(bool ack)`
**Beschreibung:** Liest ein Byte vom I2C-Bus.
- **Parameter:** `ack` — `true` sendet am Ende ein ACK (weitere Bytes gewünscht), `false` ein NACK (letztes Byte).
- **Rückgabe:** das gelesene Byte.

---

## 2. NFC-Treiber (`nfc.c` / `nfc.h`)

Implementiert die High-Level-Funktionen für den NT3H2111.

### `void nfc_init(void)`
**Beschreibung:**
1. Aktiviert die Versorgung des NFC-Chips (`P1.0` auf HIGH).
2. Wartet einige Millisekunden, bis der Chip hochfährt.
3. Initialisiert den I2C-Bus über `i2c_init()`.
- **Verwendung:** Vor jeder NFC-Operation zwingend aufrufen.

### `bool nfc_read_page(uint8_t page, uint8_t *buffer)`
**Beschreibung:** Liest eine komplette Seite (16 Byte) vom NFC-Tag.
- **Parameter:** `page` — Seitenadresse (ca. 0x00 bis 0xE2); `buffer` — Zeiger auf ein Array von mindestens 16 Byte.
- **Rückgabe:** `true` bei Erfolg (ACK), `false` bei Fehler.
- **Beispiel:**
```c
uint8_t data[16];
if (nfc_read_page(0, data)) {
    // data enthält die ersten 16 Byte des Speichers
}
```

### `bool nfc_write_page(uint8_t page, uint8_t *data)`
**Beschreibung:** Schreibt eine komplette Seite (16 Byte) auf den NFC-Tag.
- **Parameter:** `page` — Seitenadresse; `data` — Zeiger auf ein 16-Byte-Array.
- **Rückgabe:** `true`, wenn der Schreibvorgang korrekt bestätigt wurde.
- **Hinweis:** Enthält am Ende eine Verzögerung (`delay_ms(10)`), damit der Chip Zeit hat, die Daten in sein internes EEPROM zu schreiben.

### `bool nfc_erase_all(void)`
**Beschreibung:** Führt ein komplettes Löschen („Factory Reset") durch.
1. Schreibt `0x00` ins Konfigurationsregister (Seite `0x3A`), um Schutz/Passwörter zu entfernen.
2. Schreibt `0x00` in alle User-Speicherseiten (Seiten 4 bis `0x39`).
- **Rückgabe:** `true` bei Erfolg.
- **Laufzeit:** ca. 2,2 s.

### `bool nfc_read_session_reg(uint8_t *reg_val)`
**Beschreibung:** Platzhalter zum Lesen der Session-Register (Konfiguration). Aktuell nicht vollständig implementiert.
- **Rückgabe:** `false`.

---

## I2C-Adresse
Der Treiber nutzt die Standard-I2C-Adresse der NTAG-I2C-Familie:
- **Schreiben:** `0xAA` (0x55 << 1)
- **Lesen:** `0xAB` ((0x55 << 1) | 1)

## Nutzungshinweise
1. **Initialisierung:** Immer zuerst `nfc_init()` aufrufen.
2. **Watchdog:** Schreiboperationen enthalten Verzögerungen (10 ms). Beim Beschreiben vieler Seiten in einer Schleife den Watchdog Timer (WDT) ggf. „füttern".
3. **Konflikte:** Vor diesen Funktionen die UART deaktivieren, falls sie zuvor genutzt wurde.
