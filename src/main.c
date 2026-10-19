/*
 * ============================================================================
 *  CC2510-ESL  ·  main.c
 * ----------------------------------------------------------------------------
 *  Dieses main ist eine Test-/Demo-Umgebung. Es gibt (noch) keine fertige
 *  Anwendung, sondern mehrere unabhaengige Demos fuer die einzelnen Module
 *  (Display, RF, Flash, NFC, UART/COBS, Deep Sleep).
 *
 *  So waehlst du eine Demo aus:
 *    -> Setze das Makro DEMO weiter unten auf einen der DEMO_* Werte.
 *    -> Nur der passende Block wird kompiliert und ausgefuehrt.
 *
 *  Der gemeinsame Startcode (Takt, Timer, Interrupts, LED) laeuft immer zuerst;
 *  10x LED-Blinken signalisiert eine erfolgreiche Grundinitialisierung.
 * ============================================================================
 */

#include "display/epd.h"

#include "hal/clock.h"
#include "hal/hal.h"
#include "hal/isr.h"
#include "hal/led.h"
#include "hal/time.h"
#include "hal/uart.h"

#include "image_data/image.h"

#include "rf/rf.h"

#include "cobs/cobs.h"

#include "nfc/nfc.h"
#include "nfc/i2c.h"

#include "flash/flash.h"

/* --------------------------------------------------------------------------
 *  Verfuegbare Demos
 * ------------------------------------------------------------------------ */
#define DEMO_DISPLAY    1  /* Testbild + schwarzes Rechteck auf dem EPD      */
#define DEMO_DEEPSLEEP  2  /* Deep-Sleep-Test (aktuell nicht stromsparend)   */
#define DEMO_EPD_CYCLE  3  /* Bilder im Wechsel zeigen, dann schlafen        */
#define DEMO_FLASH      4  /* Bild aus externem SPI-Flash -> EPD streamen    */
#define DEMO_RF_TX      5  /* RF: Pakete senden                              */
#define DEMO_RF_RX      6  /* RF: Pakete empfangen und Bild/LED schalten     */
#define DEMO_COBS       7  /* UART mit COBS-Framing (PC-Kommunikation)       */
#define DEMO_UART       8  /* UART als einfaches Echo (Klartext)             */
#define DEMO_NFC_SCAN   9  /* NFC: I2C-Adresse des Chips suchen              */
#define DEMO_NFC_RW     10 /* NFC: Chip loeschen und beschreiben             */

/* --------------------------------------------------------------------------
 *  >>> HIER die gewuenschte Demo einstellen <<<
 * ------------------------------------------------------------------------ */
#define DEMO  DEMO_DISPLAY

/* Globale Puffer fuer Flash-Operationen (spart Stack) */
#define CHUNK_SIZE 32
static __xdata uint8_t buffer[CHUNK_SIZE];
static __xdata uint32_t g_addr;
static __xdata uint32_t g_i;
static __xdata uint16_t g_len;

void main(void) {
  init_clock();            // Von internem auf externen Oszillator wechseln
  time_init();             // Timer fuer delay_ms() / millis() initialisieren

  HAL_ENABLE_INTERRUPTS(); // Globale Interrupts aktivieren
  LED_INIT;                // LED-Pins initialisieren

  for (uint8_t i = 0; i < 10; i++) {
    LED_TOGGLE;
    delay_ms(50);
  }  // Blinken = erfolgreiche Grundinitialisierung

  delay_ms(500);

/* ========================================================================== */
#if DEMO == DEMO_DISPLAY
/* ========================================================================== */
  LED_B_ON;
  epd_init();

  // 1. Normales Bild zeigen
  epd_setBWMode(0); // Normaler Modus
  // epd_sendIndexData(0x10, image1, BUFFER_SIZE);
  epd_sendColor(0x10, 0x55, BUFFER_SIZE);
  epd_sendColor(0x13, 0x00, BUFFER_SIZE);
  epd_flushDisplay();
  LED_B_OFF;

  delay_ms(2000); // Pause, um das Bild zu sehen

  // 2. Teil-Refresh testen
  LED_B_ON;
  epd_setBWMode(1); // S/W-Modus fuer schnellen Refresh
  epd_drawBlackRect(32, 32, 64, 64); // Schwarzes Testrechteck (auf 8 ausgerichtet)
  LED_B_OFF;

  epd_powerOff();
  while (1);

/* ========================================================================== */
#elif DEMO == DEMO_DEEPSLEEP
/* ========================================================================== */
  LED_B_ON;
  delay_ms(1000);
  LED_B_OFF;
  delay_ms(5000);
  LED_B_ON;
  delay_ms(100);
  LED_B_OFF;

  // sleep_for_minutes(1);
  sleep_ms(500);
  // deep_sleep_seconds(2);
  LED_G_ON;

  for (uint8_t i = 0; i < 3; i++)
  {
    LED_B_ON;
    delay_ms(100);
    LED_B_OFF;
    delay_ms(100);
  }
  LED_B_OFF;
  LED_G_OFF;

  while (1);

/* ========================================================================== */
#elif DEMO == DEMO_EPD_CYCLE
/* ========================================================================== */
  // Variable zum Wechseln zwischen den Bildern.
  // static, damit der Wert ueber Resets erhalten bleibt (bei Deep Sleep bleibt
  // der RAM in PM2 erhalten).
  static uint8_t current_image = 0;

  while (1) {
    // 1. Display initialisieren
    epd_init();
    LED_B_ON; // Kurzer visueller Indikator

    // 2. Puffer je nach aktuellem Bild aktualisieren
    switch (current_image) {
        case 0:
            epd_sendIndexData(0x10, image1, BUFFER_SIZE);
            epd_sendColor(0x13, 0x00, BUFFER_SIZE); // image1 = S/W, Rot-Kanal leer
            break;
        case 1:
            epd_sendIndexData(0x10, image2, BUFFER_SIZE);
            epd_sendColor(0x13, 0x00, BUFFER_SIZE);
            break;
        case 2:
            epd_sendIndexData(0x10, image3, BUFFER_SIZE);
            epd_sendColor(0x13, 0x00, BUFFER_SIZE);
            break;
    }

    // 3. Display refreshen
    epd_flushDisplay();
    LED_B_OFF;
    // 4. Display abschalten (Deep Sleep des EPD)
    epd_powerOff();

    // 5. Naechstes Bild vorbereiten
    current_image += 1;
    if (current_image >= 3) {
      current_image = 0;
    }

    // 6. MCU schlafen legen (hier ist irgendwo ein Fehler: 1 Sekunde entspricht
    //    real etwa einer halben Minute)
    // deep_sleep_seconds(120*6); // 120 == 1 Stunde, mal x Stunden (hier 6)
    delay_ms(5000);
  }

/* ========================================================================== */
#elif DEMO == DEMO_FLASH
/* ========================================================================== */
//  rf_init();
  LED_BOOST_OFF;
  spi_flash_init();

  uint16_t flash_id = spi_flash_read_id();

  // ID pruefen (Winbond = 0xEF)
  if ((flash_id >> 8) == 0xEF) {
      // Kurzer visueller Erfolgshinweis
      LED_BOOST_ON;
      LED_G_ON; delay_ms(100); LED_G_OFF;
      LED_BOOST_OFF; // Boost aus, um LED-Flackern waehrend des Lesens zu vermeiden

      // EPD initialisieren
      epd_init();

      // -------------- S/W-KANAL (0x10) AUS DEM FLASH --------------
      // In Chunks aus dem Flash lesen und an das EPD senden
      epd_stream_start(0x10);

      g_addr = 0; // Startadresse des Bildes im Flash
      // uint32_t total_bytes = BUFFER_SIZE;

      for (g_i = 0; g_i < BUFFER_SIZE; g_i += CHUNK_SIZE) {
          g_len = CHUNK_SIZE;
          if ((g_i + g_len) > BUFFER_SIZE) {
              g_len = BUFFER_SIZE - g_i;
          }

          // 1. Aus dem Flash lesen (USART1)
          spi_flash_read(g_addr, buffer, g_len);
          g_addr += g_len;

          // Farben invertieren (Test)
          //for(uint8_t k=0; k<g_len; k++) buffer[k] = ~buffer[k];

          // 2. An das EPD schreiben (USART0) - EPD-Transaktion nicht schliessen
          epd_stream_data(buffer, g_len);
      }
      epd_stream_end(); // Ende des Kommandos 0x10

      // -------------- ROT-KANAL (0x13) AUS DEM FLASH --------------
      epd_stream_start(0x13);

      // Weiterlesen im Flash, wo wir aufgehoert haben (oder Rot-Start erzwingen)
      g_addr = BUFFER_SIZE;

      for (g_i = 0; g_i < BUFFER_SIZE; g_i += CHUNK_SIZE) {
          g_len = CHUNK_SIZE;
          if ((g_i + g_len) > BUFFER_SIZE) {
              g_len = BUFFER_SIZE - g_i;
          }

          // 1. Aus dem Flash lesen (USART1)
          spi_flash_read(g_addr, buffer, g_len);
          g_addr += g_len;

          // Farben bei Bedarf invertieren (standardmaessig auskommentiert)
          // for(uint8_t k=0; k<g_len; k++) buffer[k] = ~buffer[k];

          // 2. An das EPD schreiben (USART0)
          epd_stream_data(buffer, g_len);
      }
      epd_stream_end(); // Ende des Kommandos 0x13

      // Refreshen und abschalten
      epd_flushDisplay();
      epd_powerOff();

      // Flash und Pins deaktivieren
      spi_flash_disable();
      FLASH_POWER_OFF;

      // Endgueltiger Erfolg
      LED_BOOST_ON;
      while(1) {
         LED_G_ON; delay_ms(200); LED_G_OFF; delay_ms(2000);
      }

  } else {
      // ID-Fehler
      FLASH_POWER_OFF;
      LED_BOOST_ON;
      while(1) {
        LED_R_TOGGLE; delay_ms(100);
      }
  }

/* ========================================================================== */
#elif DEMO == DEMO_RF_TX
/* ========================================================================== */
  delay_ms(500);
  LED_B_ON;
  rf_init();

  static __xdata uint8_t data[] = "\nHallo Welt per RF!\nWie geht es der Welt?";
  uint8_t data_length = sizeof(data) - 1; // -1, um das Nullbyte nicht zu senden
  delay_ms(50);
  LED_R_OFF;
  LED_G_OFF;
  LED_B_OFF;
  LED_OFF;

  for (uint8_t i = 0; i < 10; i++) {
    LED_TOGGLE;
    delay_ms(50);
  } // Blinken = erfolgreiche RF-Initialisierung

  while (1)
  {
    LED_B_ON;
    rf_send_packet(data, data_length);
    LED_B_OFF;
    delay_ms(500);

    for(uint8_t i = 0; i < 5; i++) {
      LED_G_ON;
      delay_ms(50);
      LED_G_OFF;
      delay_ms(50);
    }

    delay_ms(1000);  // Pause zwischen den Versuchen
  }

/* ========================================================================== */
#elif DEMO == DEMO_RF_RX
/* ========================================================================== */
  rf_init();
  epd_init();

  // Puffer fuer empfangene Daten
  static __xdata uint8_t rx_buffer[64];

  // Blinken zum Start
  for (uint8_t i = 0; i < 5; i++) {
    LED_B_ON;
    delay_ms(50);
    LED_B_OFF;
    delay_ms(50);
  }

  while (1) {
    // Versuchen, ein Paket zu empfangen
    uint8_t len = rf_receive_packet(rx_buffer, 63);

    if (len > 0) {
      // Pruefen, ob das Paket mit "LED" beginnt
      if (len >= 3 && rx_buffer[0] == 'L' && rx_buffer[1] == 'E' && rx_buffer[2] == 'D') {
        epd_powerOn();
        if (rx_buffer[4] == 'R') {
          LED_R_ON;
          epd_sendIndexData(0x10, image1, BUFFER_SIZE);
          epd_sendColor(0x13, 0x00, BUFFER_SIZE);
          LED_R_OFF;
        } else if (rx_buffer[4] == 'G') {
          LED_G_ON;
          epd_sendIndexData(0x10, image2, BUFFER_SIZE);
          epd_sendColor(0x13, 0x00, BUFFER_SIZE);
          LED_G_OFF;
        } else if (rx_buffer[4] == 'B')  {
          LED_B_ON;
          epd_sendIndexData(0x10, image3, BUFFER_SIZE);
          epd_sendColor(0x13, 0x00, BUFFER_SIZE);
          LED_B_OFF;
        }
        epd_flushDisplay();
        epd_powerOff();
      } else {
        for (uint8_t i = 0; i < 5; i++) {
          LED_R_ON;
          delay_ms(50);
          LED_R_OFF;
          delay_ms(50);
        }
      }
    }
  }

/* ========================================================================== */
#elif DEMO == DEMO_COBS
/* ========================================================================== */
  uart_init(); // UART fuer Debug initialisieren
  CobsState __xdata cobsState;
  cobs_init(&cobsState);
  static __xdata uint8_t data[] = "\nHallo Welt per UART!\nWie geht es der Welt?";
  uint8_t data_length = sizeof(data) - 1; // -1, um das Nullbyte nicht zu senden

  uint32_t __xdata next = 0;
  while (1) {
    if (cobs_handle(&cobsState)) {
      LED_G_ON;
      delay_ms(500);
      cobs_send(cobsState.packet, cobsState.packet_size);
      LED_G_OFF;
    }

    if (millis() >= next) {
      cobs_send_str("\n[Cobs]:");
      cobs_send(data, data_length);
      uart_send_str("\n[UART]:");
      uart_send(data, data_length);
      next += 5000;
      LED_B_TOGGLE;
    }
  }

/* ========================================================================== */
#elif DEMO == DEMO_UART
/* ========================================================================== */
  uart_init();

  uart_send_str("\r\nSystem gestartet: Warte auf UART-Text...\r\n");

  uint8_t rx_byte;
  while (1) {
    if (uart_available()) {
        if (uart_read_byte(&rx_byte)) {
            // Empfangenes Zeichen zuruecksenden (Echo)
            uart_send_byte(rx_byte);

            // Bei Enter die LED blinken lassen
            if (rx_byte == '\r') {
                uart_send_byte('\n'); // Visueller Zeilenumbruch
                for (uint8_t i = 0; i < 10; i++) {
                  LED_G_TOGGLE;
                  delay_ms(50);
                }
            }
        }
    }
  }

/* ========================================================================== */
#elif DEMO == DEMO_NFC_SCAN
/* ========================================================================== */
  nfc_init();
  P1_0 = 1;

  // I2C-Scanner: durchsucht die Adressen 1 bis 127
  uint8_t found_addr = nfc_scan();

  if (found_addr != 0) {
      // GEFUNDEN! Adresse per LED-Blinken ausgeben.
      while(1) {
         // Oberes Nibble (obere 4 Bit) - ROT
         uint8_t High = (found_addr >> 4);
         if (High == 0) {
             LED_B_ON; delay_ms(500); LED_B_OFF; // Langes Blau = 0
         } else {
             for(uint8_t k=0; k<High; k++) {
                 LED_R_ON; delay_ms(300); LED_R_OFF; delay_ms(300);
             }
         }

         delay_ms(1000); // Pause zwischen den Nibbles

         // Unteres Nibble (untere 4 Bit) - GRUEN
         uint8_t Low = (found_addr & 0x0F);
         if (Low == 0) {
             LED_B_ON; delay_ms(500); LED_B_OFF; // Langes Blau = 0
         } else {
             for(uint8_t k=0; k<Low; k++) {
                 LED_G_ON; delay_ms(300); LED_G_OFF; delay_ms(300);
             }
         }

         delay_ms(3000); // Lange Pause vor der Wiederholung
      }
  } else {
      // Nichts gefunden
      while(1) {
          LED_R_ON; delay_ms(100); LED_R_OFF; delay_ms(100);
      }
  }

  while (1);

/* ========================================================================== */
#elif DEMO == DEMO_NFC_RW
/* ========================================================================== */
  nfc_init();

  // 1. Sicheres Loeschen (nur User-Speicher: I2C-Seite 1..0x39)
  // Blau blinkt waehrend des Loeschens
  LED_B_ON;
  nfc_erase_all(); // Jetzt sicher!
  LED_B_OFF;
  delay_ms(500);

  // 2. "HelOwOrld ^~^" auf I2C-Seite 1 schreiben (NFC-Block 4 - erste User-Seite)
  static __xdata uint8_t hello_msg[16] = "HelOwOrld ^~^";

  // Auf Seite 1 schreiben
  if (nfc_write_page(1, hello_msg)) {
      // Schreiben OK, verifizieren?
      static __xdata uint8_t read_back[16];
      nfc_read_page(1, read_back);

      // Pruefen, ob das erste Zeichen 'H' ist
      if (read_back[0] == 'H') {
          // ERFOLG! Gruener Herzschlag
          while(1) {
              LED_G_ON; delay_ms(100); LED_G_OFF; delay_ms(100);
              LED_G_ON; delay_ms(100); LED_G_OFF; delay_ms(800);
          }
      } else {
           // Schreiben bestaetigt, aber Lesen weicht ab -> Gruen/Rot abwechselnd
           while(1) {
              LED_G_ON; delay_ms(200); LED_G_OFF;
              LED_R_ON; delay_ms(200); LED_R_OFF;
          }
      }
  } else {
      // Schreiben per NACK abgelehnt -> ROT schnell
      while(1) {
          LED_R_ON; delay_ms(50); LED_R_OFF; delay_ms(50);
      }
  }

  while (1);

#endif
}
