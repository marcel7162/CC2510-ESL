# SPI-Flash-Modul für den CC2510

Dieses Modul stellt eine Schnittstelle zu SPI-Flash-Speichern (z. B. Winbond W25X10CL) über den CC2510 bereit. Es nutzt das Peripherieteil **USART1** im SPI-Modus (Alternativlage 2).

## Pin-Belegung

Das Modul setzt folgende Verdrahtung voraus:

| Flash-Signal | CC2510-Pin | Funktion |
|--------------|------------|----------|
| **CS**   | P1_4 | Chip Select (GPIO-Ausgang) |
| **CLK**  | P1_5 | SPI-Takt (USART1 Alt 2) |
| **MOSI** | P1_6 | Master Out Slave In (USART1 Alt 2) |
| **MISO** | P1_7 | Master In Slave Out (USART1 Alt 2) |

## Funktionsbeschreibung

### `void spi_flash_init(void)`
Initialisiert USART1 als SPI-Master und konfiguriert die nötigen Pins.
- Setzt P1_5, P1_6 und P1_7 als Peripherie.
- Setzt P1_4 als Ausgang (CS) und auf HIGH (abgewählt).
- Legt Baudrate und SPI-Modus fest (CPOL=0, CPHA=0).

### `uint16_t spi_flash_read_id(void)`
Liest Hersteller- und Geräte-ID.
- **Rückgabe:** 16-Bit-Wert, High-Byte = Hersteller-ID, Low-Byte = Geräte-ID.

### `void spi_flash_read(uint32_t addr, uint8_t *buf, uint16_t len)`
Liest Daten aus dem Flash.
- `addr`: 24-Bit-Startadresse.
- `buf`: Zeiger auf den Puffer für die gelesenen Daten.
- `len`: Anzahl zu lesender Bytes.
- **Hinweis:** Wartet vor Beginn, bis der Flash nicht mehr beschäftigt ist.

### `void spi_flash_write(uint32_t addr, const uint8_t *buf, uint16_t len)`
Schreibt Daten in den Flash. Führt automatisch Write Enable und Page Program aus.
- `addr`: 24-Bit-Startadresse.
- `buf`: Zeiger auf die zu schreibenden Daten.
- `len`: Anzahl zu schreibender Bytes.
- **Hinweis:** Geschrieben wird seitenweise. Achte darauf, keine Seitengrenzen zu überschreiten, oder behandle die Paginierung in einer höheren Schicht.

### `void spi_flash_sector_erase(uint32_t addr)`
Löscht einen 4-KB-Sektor.
- `addr`: Adresse innerhalb des zu löschenden Sektors.

### `void spi_flash_chip_erase(void)`
Löscht den gesamten Chip-Inhalt.
- **Achtung:** Kann mehrere Sekunden dauern.

### `void spi_flash_power_down(void)`
Versetzt den Flash in den Energiesparmodus.

### `void spi_flash_release_power_down(void)`
Holt den Flash aus dem Energiesparmodus zurück.

## Anwendungsbeispiele

### Initialisierung und Prüfung

```c
#include "flash/flash.h"

void main(void) {
    // System und Flash-Modul initialisieren
    spi_flash_init();

    // ID auslesen, um die Kommunikation zu prüfen
    uint16_t flash_id = spi_flash_read_id();

    // Winbond W25X10CL sollte etwa 0xEF10 zurückgeben
    if (flash_id == 0xEF10) {
        // Kommunikation erfolgreich
    }
}
```

### Lesen und Schreiben

```c
uint8_t daten[] = {0x10, 0x20, 0x30, 0x40};
uint8_t lesepuffer[4];
uint32_t adresse = 0x000000;

// 1. Sektor vor dem Schreiben löschen (nötig, falls nicht leer)
spi_flash_sector_erase(adresse);

// 2. Daten schreiben
spi_flash_write(adresse, daten, 4);

// 3. Zur Kontrolle zurücklesen
spi_flash_read(adresse, lesepuffer, 4);

// lesepuffer sollte jetzt {0x10, 0x20, 0x30, 0x40} enthalten
```

### Energieverwaltung

```c
// Flash nach Gebrauch in den Energiesparmodus versetzen
spi_flash_power_down();

// ... später ...

// Flash vor erneuter Nutzung aufwecken
spi_flash_release_power_down();
// Laut Datenblatt vor weiteren Befehlen ein paar Mikrosekunden warten
```
