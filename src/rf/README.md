# Funkmodul (RF) für den CC2510

Dieses Modul implementiert eine grundlegende drahtlose Kommunikation über den im CC2510 integrierten 2,4-GHz-Transceiver.

## Konfiguration
Standardparameter des Moduls:
- **Frequenz:** 2433 MHz
- **Datenrate:** 250 kBaud
- **Modulation:** MSK
- **Sendeleistung:** 0 dBm

## Dateien
- `rf.h`: Header mit Funktionsprototypen und Definitionen.
- `rf.c`: Implementierung von Initialisierung, Senden und Empfangen.

## Verwendung

### 1. Initialisierung
Vor jeder RF-Funktion muss das Modul mit `rf_init()` initialisiert werden. Das setzt die Radio-Register auf die nötigen Werte.

```c
#include "rf/rf.h"

void main(void) {
    // ... andere Initialisierungen ...
    rf_init();
    // ...
}
```

### 2. Pakete senden
Zum Senden dient `rf_send_packet`. Die Funktion ist blockierend und wartet, bis das Paket übertragen wurde.

```c
uint8_t daten[] = "Hallo Welt";
rf_send_packet(daten, sizeof(daten) - 1); // ohne das Nullbyte senden
```

### 3. Pakete empfangen
Zum Empfangen dient `rf_receive_packet`. Die Funktion prüft, ob ein vollständiges Paket eingetroffen ist.

```c
uint8_t buffer[32];
uint8_t laenge = rf_receive_packet(buffer, 32);

if (laenge > 0) {
    // Ein Paket wurde empfangen
    // 'buffer' enthält die Daten
    // 'laenge' ist die Größe der empfangenen Daten
}
```

**Interner Ablauf:**
1.  Prüft, ob das Radio im RX-Modus ist. Falls nicht, wird RX aktiviert (`SRX`).
2.  Prüft das Interrupt-Flag `IRQ_DONE` im Register `RFIF` (Bit 4), das den Abschluss einer Operation (hier: Empfang) anzeigt.
3.  Wenn ein Paket vorliegt:
    -   liest die Länge aus dem ersten Byte,
    -   liest bei gültiger Länge die restlichen Daten aus dem Register `RFD`,
    -   liest (und verwirft vorerst) die Status-Bytes (RSSI und LQI), die die Hardware automatisch anhängt,
    -   gibt die Länge der Nutzdaten zurück.
4.  Bei zu großer Länge wird der RX-FIFO geleert (`SFRX`) und der Empfang neu gestartet.

## Hinweise
- Die Empfangsfunktion ist für periodisches Aufrufen (Polling) in der Hauptschleife gedacht.
- Stelle sicher, dass `HAL_ENABLE_INTERRUPTS()` aktiv ist, falls du künftig Interrupts nutzen willst — diese einfache Implementierung arbeitet aber per Flag-Polling.
