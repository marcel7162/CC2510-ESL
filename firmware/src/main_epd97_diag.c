/*
 * 9.7" EPD OTP-Dump: Liest alle 80 OTP-Bytes ueber den funktionierenden
 * 2-Versuch-Weg (1. Versuch schlaegt fehl, Power-Cycle, 2. Versuch klappt).
 *
 * XDATA-Layout (ab 0xF000):
 *   diag[0]     = 0xAA  Marker Start
 *   diag[1]     = otp[0] vom 2. Versuch (soll 0xA5 sein)
 *   diag[2]     = 0x55  Marker Ende
 *   otp_full[80] ab 0xF020 = kompletter OTP-Dump vom 2. Versuch
 *
 * Auslesen:  python cc_diag97.py COM15 --run 20 --diag
 */

#include <cc2510fx.h>
#include <stdint.h>

static void delay_ms(uint16_t ms) {
    uint16_t i;
    while (ms--) {
        for (i = 0; i < 1600; i++)
            __asm nop __endasm;
    }
}

#define BV(x) (1 << (x))

static __xdata uint8_t diag[32];
static __xdata uint8_t otp_full[80];

#define EPD_PWR   P0_0
#define EPD_CS    P0_7
#define EPD_DC    P1_2
#define EPD_BUSY  P1_3
#define EPD_RESET P2_0
#define EPD_CSS   P0_1
#define BB_SCK    P0_5
#define BB_SDA    P0_3

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

static void otp_attempt(void) {
    EPD_CSS = 1;
    P0SEL &= ~(BV(3) | BV(5));
    BB_SCK = 0;

    EPD_DC = 0; EPD_CS = 0;
    bb_write(0xA1); EPD_DC = 1; bb_write(0x00); EPD_CS = 1;
    EPD_DC = 0; EPD_CS = 0;
    bb_write(0xA7); EPD_DC = 1; bb_write(0x80); EPD_CS = 1;
    EPD_DC = 0; EPD_CS = 0;
    bb_write(0xA3); EPD_DC = 1; bb_write(0x00); bb_write(0x40); EPD_CS = 1;

    EPD_DC = 0; EPD_CS = 0;
    bb_write(0xA8);
    delay_ms(10);
    EPD_DC = 1;
    (void)bb_read();
}

void main(void) {
    uint8_t i;

    for (i = 0; i < 32; i++) diag[i] = 0xCC;
    for (i = 0; i < 80; i++) otp_full[i] = 0xCC;

    diag[0] = 0xAA;

    /* GPIO */
    P0DIR |= BV(0) | BV(1) | BV(3) | BV(5) | BV(7);
    P1DIR |= BV(2);
    P1DIR &= ~BV(3);
    P2DIR |= BV(0);

    /* HW SPI */
    PERCFG &= ~0x01;
    U0CSR = 0;
    U0GCR = BV(5) | 17;
    U0BAUD = 0;
    U0CSR |= BV(6);
    P0SEL |= BV(3) | BV(5);

    /* ---- 1. Versuch (schlaegt fehl, aber weckt den COG) ---- */
    EPD_CS = 1; EPD_CSS = 1;
    EPD_RESET = 0;
    EPD_PWR = 0;
    delay_ms(500);

    delay_ms(200);
    EPD_RESET = 1;
    delay_ms(20);
    EPD_RESET = 0;
    delay_ms(200);
    EPD_RESET = 1;
    delay_ms(200);
    EPD_CS = 1; EPD_CSS = 1;
    delay_ms(5);

    otp_attempt();
    diag[3] = bb_read();   /* otp[0] 1. Versuch */
    EPD_CS = 1;
    P0SEL |= BV(3) | BV(5);

    /* ---- Power-Cycle ---- */
    EPD_PWR = 1;
    delay_ms(200);
    EPD_PWR = 0;
    delay_ms(500);

    /* ---- Reset ---- */
    EPD_RESET = 0;
    delay_ms(500);
    EPD_RESET = 1;
    delay_ms(500);

    /* ---- 2. Versuch: alle 80 Bytes lesen ---- */
    otp_attempt();
    for (i = 0; i < 80; i++)
        otp_full[i] = bb_read();
    EPD_CS = 1;
    P0SEL |= BV(3) | BV(5);

    diag[1] = otp_full[0];
    diag[2] = 0x55;
    diag[4] = otp_full[1];

    for (;;) { delay_ms(60000); }
}
