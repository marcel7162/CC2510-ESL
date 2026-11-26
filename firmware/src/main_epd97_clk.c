/*
 * 9.7" Takt-Umschaltungs-Test: wie display97.bin erst XOSC, dann RC fuer EPD.
 * Wenn hier Streifen kommen, ist die Takt-Umschaltung das Problem.
 */

#include <cc2510fx.h>
#include "epd.h"

extern void delay_ms(uint16_t ms);

#define CLKCON_OSC 0x40

static void select_xosc(void) {
    uint16_t t = 60000;
    CLKCON &= (uint8_t)~CLKCON_OSC;
    while ((CLKCON & CLKCON_OSC) && --t) { }
}

static void select_rc(void) {
    uint16_t t = 60000;
    CLKCON |= CLKCON_OSC;
    while (!(CLKCON & CLKCON_OSC) && --t) { }
}

#define FULL_RUNS  632
#define LAST_RUN   120

static void fill(uint8_t val) {
    uint16_t i;
    uint8_t rle[2];
    rle[1] = val;
    for (i = 0; i < FULL_RUNS; i++) {
        rle[0] = 255;
        epd_img_rle(rle, 2);
    }
    rle[0] = LAST_RUN;
    epd_img_rle(rle, 2);
}

void main(void) {
    /* Simuliere was display97.bin macht: erst XOSC, dann RC fuer EPD */
    select_xosc();
    delay_ms(100);
    select_rc();

    /* Weiss */
    epd_img_begin();
    fill(0xFF);
    if (epd_img_complete()) epd_img_show();

    delay_ms(3000);

    /* Nochmal XOSC -> RC wie beim zweiten epd_render() Aufruf */
    select_xosc();
    delay_ms(100);
    select_rc();

    /* Schwarz */
    epd_img_begin();
    fill(0x00);
    if (epd_img_complete()) epd_img_show();

    for (;;) { delay_ms(60000); }
}
