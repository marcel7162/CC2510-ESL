/*
 * Schneller 9.7" EPD-Streaming-Test: benutzt epd_img_begin/rle/show
 * (den gleichen Pfad wie Radio-Streaming), aber lokal ohne Funk.
 * Schreibt ein Testmuster:
 *   seg 0 (Master BW, LINKS): obere Haelfte schwarz, untere weiss
 *   seg 1 (Master RED, LINKS): obere 60 Zeilen rot
 *   seg 2 (Slave BW, RECHTS): Schachbrett
 *   seg 3 (Slave RED, RECHTS): untere 60 Zeilen rot
 * Dauert ~2 Sekunden Daten + ~40 Sekunden Refresh.
 */

#include <cc2510fx.h>
#include "epd.h"

extern void delay_ms(uint16_t ms);

#define BV(x) (1 << (x))

void main(void) {
    uint16_t row;
    uint8_t rle[2];

    /* minimal pin init (no LED) */
    epd_init();

    if (!epd_otp_ok()) {
        /* blink P1.5 (red LED if connected) forever */
        P1DIR |= BV(5);
        for (;;) { P1_5 = 1; delay_ms(300); P1_5 = 0; delay_ms(300); }
    }

    /* ---- Testbild per Streaming-API ---- */
    epd_img_begin();

    /* seg 0: Master BW (LINKE Haelfte) - 672 Zeilen a 60 Bytes
     * Obere 336 Zeilen = schwarz (0x00), untere 336 = weiss (0xFF) */
    for (row = 0; row < 336; row++) {
        rle[0] = 60; rle[1] = 0x00;
        epd_img_rle(rle, 2);
    }
    for (row = 0; row < 336; row++) {
        rle[0] = 60; rle[1] = 0xFF;
        epd_img_rle(rle, 2);
    }

    /* seg 1: Master RED (LINKE Haelfte) - 672 Zeilen
     * Obere 60 Zeilen = rot (0x00), Rest = kein rot (0xFF) */
    for (row = 0; row < 60; row++) {
        rle[0] = 60; rle[1] = 0x00;
        epd_img_rle(rle, 2);
    }
    for (row = 0; row < 612; row++) {
        rle[0] = 60; rle[1] = 0xFF;
        epd_img_rle(rle, 2);
    }

    /* seg 2: Slave BW (RECHTE Haelfte) - 672 Zeilen
     * Schachbrett: gerade Zeilen = 0xAA, ungerade = 0x55 */
    for (row = 0; row < 672; row++) {
        rle[0] = 60;
        rle[1] = (row & 1) ? 0x55 : 0xAA;
        epd_img_rle(rle, 2);
    }

    /* seg 3: Slave RED (RECHTE Haelfte) - 672 Zeilen
     * Untere 60 Zeilen = rot (0x00), Rest = kein rot (0xFF) */
    for (row = 0; row < 612; row++) {
        rle[0] = 60; rle[1] = 0xFF;
        epd_img_rle(rle, 2);
    }
    for (row = 0; row < 60; row++) {
        rle[0] = 60; rle[1] = 0x00;
        epd_img_rle(rle, 2);
    }

    /* Refresh (~40s) */
    if (epd_img_complete()) epd_img_show();
    epd_sleep();

    /* Endlos-Schlaf */
    for (;;) { delay_ms(60000); }
}
