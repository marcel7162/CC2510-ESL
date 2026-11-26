/*
 * Minimal 9.7" EPD-Test: initialisiert das Panel, liest OTP, zeichnet ein
 * Testbild auf beide COG-Hälften (Master links, Slave rechts) und refresht.
 * Kein Radio, kein NFC - nur SPI + Display.
 *
 * LED-Feedback (falls LED_BOOST-Pin P2.2 verbunden):
 *   blau         -> Boot / OTP lesen
 *   gruen        -> OTP OK, Testbild wird gerendert
 *   rot          -> OTP FEHLER (kein gültiges Panel?)
 *   magenta      -> Refresh läuft (~30-40 s)
 *   gruen blink  -> fertig, Display sollte Testbild zeigen
 *
 * Build:  make epd97-test    ->  epd97_test.bin
 * Flash:  python cc_write_flash.py -p COMx ../../firmware/epd97_test.bin
 */

#include <cc2510fx.h>
#include "epd.h"

extern void delay_ms(uint16_t ms);

#define BV(x)     (1 << (x))
#define LED_BOOST P2_2
#define LED_R     P1_5
#define LED_G     P1_7
#define LED_B     P1_6

static void led_init(void) {
    P1DIR |= BV(5) | BV(6) | BV(7);
    P2DIR |= BV(2);
    LED_BOOST = 1;
    LED_R = 0; LED_G = 0; LED_B = 0;
}

static void led(uint8_t r, uint8_t g, uint8_t b) {
    LED_R = r; LED_G = g; LED_B = b;
}

static void blink(uint8_t n) {
    uint8_t i;
    for (i = 0; i < n; i++) {
        LED_G = 1; delay_ms(200);
        LED_G = 0; delay_ms(200);
    }
}

void main(void) {
    led_init();
    led(0, 0, 1);  /* blau: Boot */

    epd_init();     /* SPI + Pins + OTP lesen + COG Reset */

    if (!epd_otp_ok()) {
        /* OTP nicht lesbar: Panel nicht angeschlossen oder CSS-Pin falsch */
        led(1, 0, 0);  /* rot: Fehler */
        epd_clear(WHITE);
        epd_text(10, 20, "OTP FEHLER", RED, 3);
        epd_text(10, 50, "Panel pruefen!", BLACK, 2);
        epd_text(10, 70, "CSS=P0.7?", BLACK, 2);
        /* trotzdem versuchen zu refreshen - falls OTP nur Muell enthält
         * und das Panel sonst OK ist, sieht man wenigstens was */
        for (;;) {
            led(1, 0, 0); delay_ms(500);
            led(0, 0, 0); delay_ms(500);
        }
    }

    led(0, 1, 0);  /* gruen: OTP OK */
    delay_ms(500);

    /* ---- Testbild aufbauen ---- */
    epd_clear(WHITE);

    /* Obere Leiste: links schwarz, rechts rot (zeigt ob beide COGs arbeiten) */
    epd_rect(0,   0, 480, 30, BLACK);     /* Master-Hälfte */
    epd_rect(480, 0, 480, 30, RED);       /* Slave-Hälfte */

    /* Titel auf der schwarzen/roten Leiste */
    epd_text(10,  8, "MASTER (links)", WHITE, 2);
    epd_text(490, 8, "SLAVE (rechts)", WHITE, 2);

    /* Grosse Schrift in der Mitte */
    epd_text(100, 80, "9.7 Zoll EPD Test", BLACK, 4);

    /* Schwarzes Rechteck auf Master-Seite */
    epd_rect(40, 180, 120, 80, BLACK);
    epd_text(50, 200, "BLK", WHITE, 2);

    /* Rotes Rechteck auf Master-Seite */
    epd_rect(200, 180, 120, 80, RED);
    epd_text(210, 200, "RED", WHITE, 2);

    /* Schwarzes Rechteck auf Slave-Seite */
    epd_rect(520, 180, 120, 80, BLACK);
    epd_text(530, 200, "BLK", WHITE, 2);

    /* Rotes Rechteck auf Slave-Seite */
    epd_rect(700, 180, 120, 80, RED);
    epd_text(710, 200, "RED", WHITE, 2);

    /* COG-Grenze markieren */
    epd_rect(478, 30, 4, 642, RED);

    /* Rahmen: Ecken-Marker */
    epd_rect(0,   0,   20, 20, BLACK);
    epd_rect(940, 0,   20, 20, BLACK);
    epd_rect(0,   652, 20, 20, BLACK);
    epd_rect(940, 652, 20, 20, BLACK);

    /* Untere Info-Zeile */
    epd_text(10, 640, "Pervasive E2969JS08  960x672  OTP OK", BLACK, 2);

    /* ---- Refresh (30-40 Sekunden!) ---- */
    led(1, 0, 1);  /* magenta: Refresh */
    epd_update();
    epd_sleep();

    /* Fertig */
    led(0, 0, 0);
    blink(5);

    /* Endlos-Schlaf */
    LED_BOOST = 0;
    for (;;) { delay_ms(60000); }
}
