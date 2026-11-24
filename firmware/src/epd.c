#include <cc2510fx.h>
#include "epd.h"

#define BV(x) (1 << (x))

#define EPD_PWR   P0_0
#define EPD_CS    P0_1
#define EPD_DC    P1_2
#define EPD_BUSY  P1_3
#define EPD_RESET P2_0

#ifdef PANEL_97
/*
 * Slave-COG chip select (right panel half). The 9.7" board (RFRTx0368) is NOT
 * yet traced: P0.7 is unused on the small tags and is only the first guess.
 * Verify with a multimeter against the panel FPC (CSS / "CS_S" pin) and
 * change these two lines if needed. All other EPD pins are assumed to match
 * the 2.66"/4.2" tags - verify them too (see doc in PROJECT.md).
 */
#define EPD_CSS   P0_7
#define EPD_CSS_DIR_OUT() (P0DIR |= BV(7))
#endif

#if defined(PANEL_97)
/* geometry handled in the PANEL_97 block at the end of this file */
#elif defined(PANEL_42)
#define HW_W   400
#define HW_H   300
#define HBYTES 50
#else
#define HW_W   152
#define HW_H   296
#define HBYTES 19
#endif

// ========== 5x7 Font (ASCII 32-126, column-major, bit0=top) ==========
static const __code uint8_t font5x7[95 * 5] = {
    0x00,0x00,0x00,0x00,0x00, // 32 space
    0x00,0x00,0x5F,0x00,0x00, // 33 !
    0x00,0x07,0x00,0x07,0x00, // 34 "
    0x14,0x7F,0x14,0x7F,0x14, // 35 #
    0x24,0x2A,0x7F,0x2A,0x12, // 36 $
    0x23,0x13,0x08,0x64,0x62, // 37 %
    0x36,0x49,0x55,0x22,0x50, // 38 &
    0x00,0x05,0x03,0x00,0x00, // 39 '
    0x00,0x1C,0x22,0x41,0x00, // 40 (
    0x00,0x41,0x22,0x1C,0x00, // 41 )
    0x08,0x2A,0x1C,0x2A,0x08, // 42 *
    0x08,0x08,0x3E,0x08,0x08, // 43 +
    0x00,0x50,0x30,0x00,0x00, // 44 ,
    0x08,0x08,0x08,0x08,0x08, // 45 -
    0x00,0x60,0x60,0x00,0x00, // 46 .
    0x20,0x10,0x08,0x04,0x02, // 47 /
    0x3E,0x51,0x49,0x45,0x3E, // 48 0
    0x00,0x42,0x7F,0x40,0x00, // 49 1
    0x42,0x61,0x51,0x49,0x46, // 50 2
    0x21,0x41,0x45,0x4B,0x31, // 51 3
    0x18,0x14,0x12,0x7F,0x10, // 52 4
    0x27,0x45,0x45,0x45,0x39, // 53 5
    0x3C,0x4A,0x49,0x49,0x30, // 54 6
    0x01,0x71,0x09,0x05,0x03, // 55 7
    0x36,0x49,0x49,0x49,0x36, // 56 8
    0x06,0x49,0x49,0x29,0x1E, // 57 9
    0x00,0x36,0x36,0x00,0x00, // 58 :
    0x00,0x56,0x36,0x00,0x00, // 59 ;
    0x00,0x08,0x14,0x22,0x41, // 60 <
    0x14,0x14,0x14,0x14,0x14, // 61 =
    0x41,0x22,0x14,0x08,0x00, // 62 >
    0x02,0x01,0x51,0x09,0x06, // 63 ?
    0x32,0x49,0x79,0x41,0x3E, // 64 @
    0x7E,0x11,0x11,0x11,0x7E, // 65 A
    0x7F,0x49,0x49,0x49,0x36, // 66 B
    0x3E,0x41,0x41,0x41,0x22, // 67 C
    0x7F,0x41,0x41,0x22,0x1C, // 68 D
    0x7F,0x49,0x49,0x49,0x41, // 69 E
    0x7F,0x09,0x09,0x09,0x01, // 70 F
    0x3E,0x41,0x41,0x51,0x32, // 71 G
    0x7F,0x08,0x08,0x08,0x7F, // 72 H
    0x00,0x41,0x7F,0x41,0x00, // 73 I
    0x20,0x40,0x41,0x3F,0x01, // 74 J
    0x7F,0x08,0x14,0x22,0x41, // 75 K
    0x7F,0x40,0x40,0x40,0x40, // 76 L
    0x7F,0x02,0x04,0x02,0x7F, // 77 M
    0x7F,0x04,0x08,0x10,0x7F, // 78 N
    0x3E,0x41,0x41,0x41,0x3E, // 79 O
    0x7F,0x09,0x09,0x09,0x06, // 80 P
    0x3E,0x41,0x51,0x21,0x5E, // 81 Q
    0x7F,0x09,0x19,0x29,0x46, // 82 R
    0x46,0x49,0x49,0x49,0x31, // 83 S
    0x01,0x01,0x7F,0x01,0x01, // 84 T
    0x3F,0x40,0x40,0x40,0x3F, // 85 U
    0x1F,0x20,0x40,0x20,0x1F, // 86 V
    0x3F,0x40,0x38,0x40,0x3F, // 87 W
    0x63,0x14,0x08,0x14,0x63, // 88 X
    0x07,0x08,0x70,0x08,0x07, // 89 Y
    0x61,0x51,0x49,0x45,0x43, // 90 Z
    0x00,0x7F,0x41,0x41,0x00, // 91 [
    0x02,0x04,0x08,0x10,0x20, // 92 backslash
    0x00,0x41,0x41,0x7F,0x00, // 93 ]
    0x04,0x02,0x01,0x02,0x04, // 94 ^
    0x40,0x40,0x40,0x40,0x40, // 95 _
    0x00,0x01,0x02,0x04,0x00, // 96 `
    0x20,0x54,0x54,0x54,0x78, // 97 a
    0x7F,0x48,0x44,0x44,0x38, // 98 b
    0x38,0x44,0x44,0x44,0x20, // 99 c
    0x38,0x44,0x44,0x48,0x7F, // 100 d
    0x38,0x54,0x54,0x54,0x18, // 101 e
    0x08,0x7E,0x09,0x01,0x02, // 102 f
    0x08,0x14,0x54,0x54,0x3C, // 103 g
    0x7F,0x08,0x04,0x04,0x78, // 104 h
    0x00,0x44,0x7D,0x40,0x00, // 105 i
    0x20,0x40,0x44,0x3D,0x00, // 106 j
    0x7F,0x10,0x28,0x44,0x00, // 107 k
    0x00,0x41,0x7F,0x40,0x00, // 108 l
    0x7C,0x04,0x18,0x04,0x78, // 109 m
    0x7C,0x08,0x04,0x04,0x78, // 110 n
    0x38,0x44,0x44,0x44,0x38, // 111 o
    0x7C,0x14,0x14,0x14,0x08, // 112 p
    0x08,0x14,0x14,0x18,0x7C, // 113 q
    0x7C,0x08,0x04,0x04,0x08, // 114 r
    0x48,0x54,0x54,0x54,0x20, // 115 s
    0x04,0x3F,0x44,0x40,0x20, // 116 t
    0x3C,0x40,0x40,0x20,0x7C, // 117 u
    0x1C,0x20,0x40,0x20,0x1C, // 118 v
    0x3C,0x40,0x30,0x40,0x3C, // 119 w
    0x44,0x28,0x10,0x28,0x44, // 120 x
    0x0C,0x50,0x50,0x50,0x3C, // 121 y
    0x44,0x64,0x54,0x4C,0x44, // 122 z
    0x00,0x08,0x36,0x41,0x00, // 123 {
    0x00,0x00,0x7F,0x00,0x00, // 124 |
    0x00,0x41,0x36,0x08,0x00, // 125 }
    0x08,0x04,0x08,0x10,0x08, // 126 ~
};

// ========== Draw List ==========
#define MAX_ITEMS 20
#define ITEM_RECT 1
#define ITEM_TEXT 2
#define ITEM_ICON 3

typedef struct {
    uint8_t type;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint8_t color;
    const char *text;
    uint8_t scale;
    const uint8_t __code *bmp;   /* ITEM_ICON: 1bpp row-major bitmap */
} DrawItem;

static __xdata DrawItem items[MAX_ITEMS];
static uint8_t item_count;
static uint8_t bg_color;

// ========== Delay ==========
void delay_ms(uint16_t ms) {
    uint16_t i;
    while (ms--) {
        for (i = 0; i < 1600; i++)
            __asm nop __endasm;
    }
}

// ========== SPI ==========
static void spi_tx(uint8_t d) {
    U0DBUF = d;
    while (U0CSR & 0x01) {}
}

static void cmd(uint8_t c) {
    EPD_DC = 0;
    EPD_CS = 0;
    spi_tx(c);
    EPD_CS = 1;
    EPD_DC = 1;
}

static void dat(uint8_t d) {
    EPD_CS = 0;
    spi_tx(d);
    EPD_CS = 1;
}

#ifndef PANEL_97
static uint8_t wait_busy(void) {
    uint16_t t = 30000;
    while (EPD_BUSY == 0 && --t)
        delay_ms(1);
    return t > 0;
}
#endif

// ========== Pixel Rendering ==========
static uint8_t font_pixel(const char *s, uint8_t scale,
                          uint16_t px, uint8_t py) {
    uint8_t pitch = 6 * scale;
    uint8_t ci, ic, fc, fr;
    uint8_t ch;
    uint8_t idx;

    ci = (uint8_t)(px / pitch);
    idx = 0;
    while (idx < ci && s[idx]) idx++;
    if (!s[idx]) return 0;

    ic = (uint8_t)(px % pitch);
    if (ic >= 5 * scale) return 0;

    fc = ic / scale;
    fr = py / scale;
    if (fr >= 7) return 0;

    ch = s[idx];
    if (ch < 32 || ch > 126) return 0;

    return (font5x7[(ch - 32) * 5 + fc] >> fr) & 1;
}

static uint8_t get_color(uint16_t sx, uint16_t sy) {
    uint8_t color = bg_color;
    uint8_t i;

    for (i = 0; i < item_count; i++) {
        DrawItem *it = &items[i];
        if (sx < it->x || sy < it->y) continue;
        if (sx >= it->x + it->w || sy >= it->y + it->h) continue;

        if (it->type == ITEM_RECT) {
            color = it->color;
        } else if (it->type == ITEM_TEXT) {
            uint16_t px = sx - it->x;
            uint8_t py = sy - it->y;
            if (font_pixel(it->text, it->scale, px, py))
                color = it->color;
        } else if (it->type == ITEM_ICON) {
            uint16_t col = sx - it->x;
            uint16_t row = sy - it->y;
            uint8_t  bpr = (uint8_t)((it->w + 7) >> 3);
            uint8_t  b = it->bmp[row * bpr + (col >> 3)];
            if (b & (0x80 >> (col & 7)))
                color = it->color;
        }
    }
    return color;
}

// ========== Public API ==========

/*
 * Power up and configure the panel hardware WITHOUT touching the draw list.
 * Use this to wake the panel from deep sleep (epd_sleep) before epd_update().
 * Leaving the panel powered between updates makes 3-colour e-ink smear (the
 * red pigment drifts), so the normal cycle is: epd_wake -> draw -> epd_update
 * -> epd_sleep.
 */
#ifndef PANEL_97
void epd_wake(void) {
    P0DIR |= BV(0) | BV(1) | BV(3) | BV(5);
    P1DIR |= BV(2);
    P1DIR &= ~BV(3);
    P2DIR |= BV(0);

    PERCFG &= ~0x01;
    U0CSR = 0;
    U0GCR = BV(5) | 17;
    U0BAUD = 0;
    U0CSR |= BV(6);
    P0SEL |= BV(3) | BV(5);

    EPD_CS = 1;
    EPD_PWR = 0;
    delay_ms(200);

    EPD_RESET = 0;
    delay_ms(10);
    EPD_RESET = 1;
    delay_ms(10);

    cmd(0x06);
    dat(0x17); dat(0x17); dat(0x17);

    cmd(0x04);
    wait_busy();

#ifdef PANEL_42
    /* IL0398 (4.2" 400x300 BWR): 1-byte panel setting, 4-byte resolution */
    cmd(0x00);
    dat(0x0F);

    cmd(0x61);
    dat((uint8_t)(HW_W >> 8)); dat((uint8_t)(HW_W & 0xFF));
    dat((uint8_t)(HW_H >> 8)); dat((uint8_t)(HW_H & 0xFF));
#else
    /* IL0373 (2.66" 296x152 BWR): 2-byte panel setting, 3-byte resolution */
    cmd(0x00);
    dat(0x0F); dat(0x0D);

    cmd(0x61);
    dat(HW_W); dat(HW_H >> 8); dat(HW_H & 0xFF);
#endif

    cmd(0x50);
    dat(0x77);
}

void epd_init(void) {
    epd_wake();
    item_count = 0;
    bg_color = WHITE;
}
#endif /* !PANEL_97 */

void epd_clear(uint8_t color) {
    bg_color = color;
    item_count = 0;
}

void epd_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color) {
    DrawItem *it;
    if (item_count >= MAX_ITEMS) return;
    it = &items[item_count++];
    it->type = ITEM_RECT;
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->color = color;
    it->text = (void *)0;
    it->scale = 0;
}

void epd_text(uint16_t x, uint16_t y, const char *s, uint8_t color, uint8_t scale) {
    DrawItem *it;
    uint8_t len = 0;
    const char *p = s;
    if (item_count >= MAX_ITEMS) return;
    while (*p++) len++;
    it = &items[item_count++];
    it->type = ITEM_TEXT;
    it->x = x;
    it->y = y;
    it->w = (uint16_t)len * 6 * scale;
    it->h = 7 * scale;
    it->color = color;
    it->text = s;
    it->scale = scale;
}

void epd_icon(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
              const uint8_t __code *bmp, uint8_t color) {
    DrawItem *it;
    if (item_count >= MAX_ITEMS) return;
    it = &items[item_count++];
    it->type = ITEM_ICON;
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->color = color;
    it->text = (void *)0;
    it->scale = 0;
    it->bmp = bmp;
}

#ifndef PANEL_97
void epd_update(void) {
    uint16_t r;
    uint8_t bc, bit;

    // B/W channel
    cmd(0x10);
    for (r = 0; r < HW_H; r++) {
        for (bc = 0; bc < HBYTES; bc++) {
            uint8_t val = 0xFF;
            for (bit = 0; bit < 8; bit++) {
#ifdef PANEL_42
                uint16_t sx = (uint16_t)(bc * 8 + bit);
                uint16_t sy = r;
#else
                uint16_t sx = r;
                uint16_t sy = (uint16_t)((HW_W - 1) - (bc * 8 + bit));
#endif
                if (get_color(sx, sy) == BLACK)
                    val &= ~(0x80 >> bit);
            }
            dat(val);
        }
    }

    // Red channel
    cmd(0x13);
    for (r = 0; r < HW_H; r++) {
        for (bc = 0; bc < HBYTES; bc++) {
            uint8_t val = 0xFF;
            for (bit = 0; bit < 8; bit++) {
#ifdef PANEL_42
                uint16_t sx = (uint16_t)(bc * 8 + bit);
                uint16_t sy = r;
#else
                uint16_t sx = r;
                uint16_t sy = (uint16_t)((HW_W - 1) - (bc * 8 + bit));
#endif
                if (get_color(sx, sy) == RED)
                    val &= ~(0x80 >> bit);
            }
            dat(val);
        }
    }

    // Refresh
    cmd(0x12);
    delay_ms(100);
    wait_busy();
}

// ========== Full-frame streaming (RLE) ==========
// The gateway renders the whole image and streams it here RLE-compressed, so
// the CC needs no framebuffer (only ~4KB RAM) and never needs re-flashing for
// new content. Bytes are written straight into the panel RAM as they arrive:
// first the B/W plane (PLANE_BYTES bytes after cmd 0x10), then the red plane
// (after cmd 0x13), then a refresh. RLE token stream = [runlen][value] pairs.

#define PLANE_BYTES ((uint16_t)HW_H * HBYTES)   // 296*19 = 5624 per plane

static uint16_t img_pos;
static uint8_t  rle_have_len;   // did we read a run-length byte yet?
static uint8_t  rle_run;        // pending run length

static void img_putbyte(uint8_t v) {
    if (img_pos == PLANE_BYTES) cmd(0x13);       // boundary -> red plane
    if (img_pos < 2 * PLANE_BYTES) { dat(v); img_pos++; }
}

void epd_img_begin(void) {
    epd_wake();
    cmd(0x10);                 // start B/W plane
    img_pos = 0;
    rle_have_len = 0;
}

void epd_img_rle(const uint8_t *buf, uint8_t n) {
    uint8_t i, k;
    for (i = 0; i < n; i++) {
        if (!rle_have_len) {
            rle_run = buf[i];
            rle_have_len = 1;
        } else {
            for (k = 0; k < rle_run; k++) img_putbyte(buf[i]);
            rle_have_len = 0;
        }
    }
}

/* True once both planes (2 * PLANE_BYTES) have been streamed in full. Lets
 * the caller refuse to refresh a truncated frame (keep the old image instead
 * of showing a half-written one). */
uint8_t epd_img_complete(void) {
    return img_pos == 2 * PLANE_BYTES;
}

void epd_img_show(void) {
    cmd(0x12);                 // refresh
    delay_ms(100);
    wait_busy();
    epd_sleep();               // deep sleep -> no red smear, image retained
}

void epd_sleep(void) {
    cmd(0x02);
    wait_busy();
    cmd(0x07);
    dat(0xA5);
    EPD_PWR = 1;
}

#else /* PANEL_97 */

/* =====================================================================
 * 9.7" Pervasive Displays E2969JS08 (Spectra BWR, "large" family, G2.1 COG)
 * ---------------------------------------------------------------------
 * Label on the panel: TE2969JS084 / TC097SC1B8-SK, Imagotag PCB RFRTx0368.
 * Sequence after the Pervasive app note "EPD970 Spectra with G2.1" as
 * implemented in PDLS_EXT3_Basic_Global (doc/), screen eScreen_EPD_969_JS_08
 * (FAMILY_LARGE, DRIVER_8):
 *
 *   - two COGs (master = left half, slave = right half), each with its own
 *     chip select; both share SCK/SDA/DC/RESET/BUSY
 *   - waveform/voltage parameters live in the COG's OTP (80 bytes) and are
 *     read once over 3-wire SPI (SDA bidirectional -> bit-banged)
 *   - the external boost converter is switched by the COG itself (reg 0x09),
 *     the MCU only runs the soft-start table from OTP
 *
 * Geometry (landscape, 960 x 672): every COG holds 672 lines x 60 bytes
 * (480 px). Frame order on the wire, also the order the gateway streams:
 *   [master black][master red][slave black][slave red]   4 x 40320 bytes
 * Byte layout per sub-frame is row-major, MSB = leftmost pixel.
 * Pervasive polarity is 1 = ink; the gateway/draw list use 0 = ink like the
 * small panels, so every byte is inverted on the way out.
 * ===================================================================== */

#define SUB_LINES 672
#define SUB_HB    60                          /* 480 px / 8 */
#define SUB_BYTES ((uint16_t)SUB_LINES * SUB_HB)  /* 40320 */
#define N_SEG     4

#define SEL_M    0
#define SEL_S    1
#define SEL_BOTH 2

#define OTP_LEN  80
static __xdata uint8_t otp[OTP_LEN];
static __xdata uint8_t otp_ok;

static __xdata uint8_t  img_seg;   /* current sub-frame 0..3, 4 = done */
static __xdata uint16_t img_off;   /* byte offset inside the sub-frame */
static __xdata uint8_t  rle_have_len;
static __xdata uint8_t  rle_run;

/* ~10 us per unit at the RC clock (delay_ms uses 1600 nops / ms) */
static void delay_10us(uint8_t n) {
    uint8_t i;
    while (n--)
        for (i = 0; i < 16; i++)
            __asm nop __endasm;
}

static uint8_t wait_busy(void) {
    /* Spectra 9.7" refresh takes ~30-40 s; allow 120 s. LOW = busy. */
    uint16_t t = 1200;
    while (EPD_BUSY == 0 && --t)
        delay_ms(100);
    return t > 0;
}

static void desel(void) {
    EPD_CS  = 1;
    EPD_CSS = 1;
}

static void sel(uint8_t s) {
    EPD_CS  = (s == SEL_S) ? 1 : 0;
    EPD_CSS = (s == SEL_M) ? 1 : 0;
    delay_10us(50);                     /* 500 us, as in PDLS for large */
}

static void cmd8(uint8_t c, uint8_t d, uint8_t s) __reentrant {
    EPD_DC = 0;
    sel(s);
    spi_tx(c);
    EPD_DC = 1;
    spi_tx(d);
    desel();
}

static void cmdn(uint8_t c, const __xdata uint8_t *d, uint8_t n, uint8_t s) __reentrant {
    EPD_DC = 0;
    sel(s);
    spi_tx(c);
    EPD_DC = 1;
    while (n--) spi_tx(*d++);
    desel();
}

/* ---------- 3-wire bit-bang (OTP read): SCK = P0.5, SDA = P0.3 ---------- */
#define BB_SCK P0_5
#define BB_SDA P0_3

static void bb_dly(void) {
    uint8_t i;
    for (i = 0; i < 4; i++)
        __asm nop __endasm;
}

static void bb_write(uint8_t v) {
    uint8_t i;
    P0DIR |= BV(3);
    for (i = 0; i < 8; i++) {
        BB_SDA = (v & 0x80) ? 1 : 0;
        v <<= 1;
        bb_dly();
        BB_SCK = 1;
        bb_dly();
        BB_SCK = 0;
    }
}

static uint8_t bb_read(void) {
    uint8_t i, v = 0;
    P0DIR &= ~BV(3);
    for (i = 0; i < 8; i++) {
        BB_SCK = 1;
        bb_dly();
        v = (uint8_t)(v << 1) | (BB_SDA ? 1 : 0);
        BB_SCK = 0;
        bb_dly();
    }
    return v;
}

static void bb_cmd(uint8_t c, uint8_t n, uint8_t d0, uint8_t d1) __reentrant {
    EPD_DC = 0;
    EPD_CS = 0;
    bb_write(c);
    EPD_DC = 1;
    if (n > 0) bb_write(d0);
    if (n > 1) bb_write(d1);
    EPD_CS = 1;
}

static void cog_reset(void) {
    /* b_reset(200, 20, 200, 200, 5) for the large family */
    delay_ms(200);
    EPD_RESET = 1;
    delay_ms(20);
    EPD_RESET = 0;
    delay_ms(200);
    EPD_RESET = 1;
    delay_ms(200);
    desel();
    delay_ms(5);
}

static void spi_pins_hw(void) {
    BB_SCK = 0;
    P0DIR |= BV(3) | BV(5);
    P0SEL |= BV(3) | BV(5);
}

/* Read the 80-byte OTP block of the master COG (DRIVER_8 sequence). */
static void cog_read_otp(void) {
    uint8_t i;

    cog_reset();
    EPD_CSS = 1;                        /* slave stays out of it */

    P0SEL &= ~(BV(3) | BV(5));          /* SCK/SDA -> GPIO */
    BB_SCK = 0;

    bb_cmd(0xA1, 1, 0x00, 0);
    bb_cmd(0xA7, 1, 0x80, 0);
    bb_cmd(0xA3, 2, 0x00, 0x40);

    EPD_DC = 0;
    EPD_CS = 0;
    bb_write(0xA8);
    delay_ms(5);
    EPD_DC = 1;
    (void)bb_read();                    /* dummy */
    for (i = 0; i < OTP_LEN; i++)
        otp[i] = bb_read();
    EPD_CS = 1;

    spi_pins_hw();
    otp_ok = (otp[0] == 0xA5);
}

/*
 * Power up the panel, read OTP (once; kept in XDATA, which survives PM2),
 * reset the COGs. No draw-list side effects.
 */
void epd_wake(void) {
    P0DIR |= BV(0) | BV(1) | BV(3) | BV(5);
    P1DIR |= BV(2);
    P1DIR &= ~BV(3);
    P2DIR |= BV(0);
    EPD_CSS_DIR_OUT();

    PERCFG &= ~0x01;
    U0CSR = 0;
    U0GCR = BV(5) | 17;
    U0BAUD = 0;
    U0CSR |= BV(6);
    P0SEL |= BV(3) | BV(5);

    desel();
    EPD_RESET = 0;
    EPD_PWR = 0;

    if (!otp_ok)
        cog_read_otp();
    cog_reset();                        /* COG_LargeCJ_initial (DRIVER_8) */
}

void epd_init(void) {
    epd_wake();
    item_count = 0;
    bg_color = WHITE;
}

/* ---------- frame streaming into the two COGs ---------- */

static void seg_open(uint8_t seg) {
    uint8_t s = (seg >= 2) ? SEL_S : SEL_M;
    cmdn(0x12, &otp[0x13], 3, s);       /* RAM_RW */
    EPD_DC = 0;
    sel(s);
    spi_tx((seg & 1) ? 0x11 : 0x10);    /* 0x10 = black frame, 0x11 = red */
    EPD_DC = 1;                         /* CS stays low for the data */
}

static void frame_start(void) {
    cmdn(0x13, &otp[0x16], 6, SEL_BOTH);   /* DUW  */
    cmdn(0x90, &otp[0x0C], 4, SEL_BOTH);   /* DRFW */
    img_seg = 0;
    img_off = 0;
    rle_have_len = 0;
    seg_open(0);
}

static void img_putbyte(uint8_t v) {
    if (img_seg >= N_SEG) return;
    spi_tx((uint8_t)~v);                /* 0 = ink -> Pervasive 1 = ink */
    if (++img_off == SUB_BYTES) {
        desel();
        img_off = 0;
        if (++img_seg < N_SEG) seg_open(img_seg);
    }
}

/* OTP delay byte: bit7 = unit ms, else unit 10 us; bits 0..6 = count */
static void dly_otp(uint8_t d) {
    if (d & 0x80) delay_ms(d & 0x7F);
    else          delay_10us(d & 0x7F);
}

#define TEMP_INDEX 0x82                     /* 25 degC * 2 + 0x50 */

static void cog_refresh_and_off(void) __reentrant {
    uint8_t stage, i;
    static __xdata uint8_t ph[2];

    desel();

    /* --- COG_LargeCJ_update, DRIVER_8 --- */
    cmd8(0x05, 0x7D, SEL_BOTH);
    delay_ms(200);
    cmd8(0x05, 0x00, SEL_BOTH);
    delay_ms(10);
    cmd8(0xC2, 0x3F, SEL_BOTH);
    delay_ms(1);
    cmd8(0xD8, otp[0x1D], SEL_BOTH);        /* MS_SYNC */
    cmd8(0xD6, otp[0x1E], SEL_BOTH);        /* BVSS */
    cmd8(0xA7, 0x10, SEL_BOTH); delay_ms(100);
    cmd8(0xA7, 0x00, SEL_BOTH); delay_ms(100);

    ph[0] = 0x00; ph[1] = otp[0x12];        /* OSC */
    cmdn(0x03, ph, 2, SEL_BOTH);

    for (i = 0; i < 2; i++) {               /* master, then slave */
        uint8_t s = i ? SEL_S : SEL_M;
        cmd8(0x44, 0x00, s);
        cmd8(0x45, 0x80, s);
        cmd8(0xA7, 0x10, s); delay_ms(100);
        cmd8(0xA7, 0x00, s); delay_ms(100);
        cmd8(0x44, 0x06, s);
        cmd8(0x45, TEMP_INDEX, s);
        cmd8(0xA7, 0x10, s); delay_ms(100);
        cmd8(0xA7, 0x00, s); delay_ms(100);
    }

    cmd8(0x60, otp[0x0B], SEL_BOTH);        /* TCON */
    cmd8(0x61, otp[0x1C], SEL_M);           /* STV_DIR, master only */
    cmd8(0x01, otp[0x10], SEL_BOTH);        /* DCTL */
    cmd8(0x02, otp[0x11], SEL_BOTH);        /* VCOM */

    /* --- DC/DC soft-start, 4 stages from OTP (offset 0x20 for DRIVER_8) --- */
    for (stage = 0; stage < 4; stage++) {
        __xdata uint8_t *p = &otp[0x20 + 8 * stage];
        uint8_t rep = p[0] & 0x7F;
        if (p[0] & 0x80) {                  /* format 1: ramp PHL/PHH */
            ph[0] = p[1];
            ph[1] = p[2];
            for (i = 0; i < rep; i++) {
                cmd8(0x09, p[5], SEL_BOTH);         /* BST_SW_a */
                ph[0] += p[3];                      /* PHL_VAR */
                ph[1] += p[4];                      /* PHH_VAR */
                cmdn(0x51, ph, 2, SEL_BOTH);
                cmd8(0x09, p[6], SEL_BOTH);         /* BST_SW_b */
                dly_otp(p[7]);
            }
        } else {                            /* format 2: switch only */
            for (i = 0; i < rep; i++) {
                cmd8(0x09, p[1], SEL_BOTH);
                dly_otp(p[3]);
                cmd8(0x09, p[2], SEL_BOTH);
                dly_otp(p[4]);
            }
        }
    }

    /* --- display refresh --- */
    wait_busy();
    cmd8(0x15, 0x3C, SEL_BOTH);

    /* --- COG_LargeCJ_powerOff, 969_JS_08 --- */
    delay_ms(100);
    wait_busy();
    cmd8(0x09, 0x7F, SEL_BOTH);
    delay_ms(20);
    cmd8(0x05, 0x7D, SEL_BOTH);
    cmd8(0x09, 0x00, SEL_BOTH);
    delay_ms(200);
}

void epd_update(void) __reentrant {
    uint8_t seg, bc, bit;
    uint16_t r;

    if (!otp_ok) return;                /* no valid OTP -> never drive DC/DC */
    frame_start();
    for (seg = 0; seg < N_SEG; seg++) {
        uint16_t x0 = (seg >= 2) ? 480 : 0;
        uint8_t ink = (seg & 1) ? RED : BLACK;
        for (r = 0; r < SUB_LINES; r++) {
            for (bc = 0; bc < SUB_HB; bc++) {
                uint8_t val = 0xFF;
                for (bit = 0; bit < 8; bit++) {
                    if (get_color(x0 + (uint16_t)bc * 8 + bit, r) == ink)
                        val &= ~(0x80 >> bit);
                }
                img_putbyte(val);
            }
        }
    }
    cog_refresh_and_off();
}

void epd_img_begin(void) {
    epd_wake();
    if (otp_ok) frame_start();
    else        img_seg = N_SEG;        /* swallow the stream, show nothing */
    rle_have_len = 0;
}

void epd_img_rle(const uint8_t *buf, uint8_t n) {
    uint8_t i, k;
    for (i = 0; i < n; i++) {
        if (!rle_have_len) {
            rle_run = buf[i];
            rle_have_len = 1;
        } else {
            for (k = 0; k < rle_run; k++) img_putbyte(buf[i]);
            rle_have_len = 0;
        }
    }
}

uint8_t epd_img_complete(void) {
    return otp_ok && img_seg == N_SEG;
}

void epd_img_show(void) {
    cog_refresh_and_off();
    epd_sleep();
}

void epd_sleep(void) {
    desel();
    EPD_PWR = 1;                        /* panel supply off */
    EPD_RESET = 0;                      /* don't back-feed the COG */
}

/* 1 = OTP of the master COG read and valid (first byte 0xA5). */
uint8_t epd_otp_ok(void) { return otp_ok; }

#endif /* PANEL_97 */
