/*
 * Minimal 9.7" Schwarz/Weiss-Test ueber den RLE-Streaming-Pfad.
 * Dauert ~1s Daten + ~40s Refresh statt 5 Min Pixel-Rendering.
 * Kein Radio, kein NFC.
 *
 * Build: make epd97-bw   ->  epd97_bw.bin
 */

#include <cc2510fx.h>
#include "epd.h"

extern void delay_ms(uint16_t ms);

/* 4 sub-frames x 40320 = 161280 Bytes.  161280 / 255 = 632 Rest 120. */
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
    /* 1. Weiss: alle Bytes 0xFF -> invertiert 0x00 -> kein Ink */
    epd_img_begin();
    fill(0xFF);
    if (epd_img_complete()) epd_img_show();

    delay_ms(3000);

    /* 2. Schwarz: alle Bytes 0x00 -> invertiert 0xFF -> alles Ink */
    epd_img_begin();
    fill(0x00);
    if (epd_img_complete()) epd_img_show();

    for (;;) { delay_ms(60000); }
}
