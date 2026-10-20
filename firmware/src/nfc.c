#include <cc2510fx.h>
#include "nfc.h"

#define BV(x) (1 << (x))

extern void delay_ms(uint16_t ms);

#define SDA_BIT 4          /* P0.4 */
#define SCL_BIT 6          /* P0.6 */
#define NFC_VCC_BIT 0      /* P1.0 (also powers SPI flash) */

#define NTAG_ADDR 0x55     /* 7-bit I2C address */

/* ---------- bit-banged I2C (open-drain emulated via board pull-ups) ------- */

static void i2c_delay(void) {
    volatile uint8_t i;
    for (i = 0; i < 60; i++) { __asm nop __endasm; }
}

static void sda_hi(void) { P0DIR &= (uint8_t)~BV(SDA_BIT); }
static void sda_lo(void) { P0DIR |= BV(SDA_BIT); P0_4 = 0; }
static void scl_hi(void) { P0DIR &= (uint8_t)~BV(SCL_BIT); }
static void scl_lo(void) { P0DIR |= BV(SCL_BIT); P0_6 = 0; }
static uint8_t sda_read(void) {
    P0DIR &= (uint8_t)~BV(SDA_BIT);
    return P0_4 ? 1 : 0;
}

static void i2c_start(void) {
    sda_hi(); scl_hi(); i2c_delay();
    sda_lo(); i2c_delay();
    scl_lo(); i2c_delay();
}
static void i2c_stop(void) {
    sda_lo(); scl_lo(); i2c_delay();
    scl_hi(); i2c_delay();
    sda_hi(); i2c_delay();
}
static uint8_t i2c_write(uint8_t b) {
    uint8_t i, nack;
    for (i = 0; i < 8; i++) {
        if (b & 0x80) sda_hi(); else sda_lo();
        i2c_delay();
        scl_hi(); i2c_delay();
        scl_lo(); i2c_delay();
        b <<= 1;
    }
    sda_hi(); i2c_delay();
    scl_hi(); i2c_delay();
    nack = sda_read();
    scl_lo(); i2c_delay();
    return nack ? 0 : 1;      /* 1 = ACK */
}
static uint8_t i2c_read(uint8_t send_ack) {
    uint8_t i, b = 0;
    sda_hi();
    for (i = 0; i < 8; i++) {
        b <<= 1;
        scl_hi(); i2c_delay();
        if (sda_read()) b |= 1;
        scl_lo(); i2c_delay();
    }
    if (send_ack) sda_lo(); else sda_hi();
    i2c_delay();
    scl_hi(); i2c_delay();
    scl_lo(); i2c_delay();
    sda_hi();
    return b;
}

/* ---------- public API ---------- */

void nfc_init(void) {
    P1DIR |= BV(NFC_VCC_BIT);
    P1_0 = 1;                 /* power the NFC chip */
    delay_ms(50);             /* boot time */

    P0SEL &= (uint8_t)~(BV(SDA_BIT) | BV(SCL_BIT));   /* GPIO */
    sda_hi(); scl_hi();
    i2c_delay();
}

uint8_t nfc_read_block(uint8_t block, uint8_t *buf16) {
    uint8_t i;

    i2c_start();
    if (!i2c_write((uint8_t)(NTAG_ADDR << 1))) { i2c_stop(); return 0; }
    i2c_write(block);
    i2c_start();                                   /* repeated start */
    if (!i2c_write((uint8_t)((NTAG_ADDR << 1) | 1))) { i2c_stop(); return 0; }
    for (i = 0; i < 16; i++)
        buf16[i] = i2c_read(i < 15 ? 1 : 0);       /* ACK x15, NACK last */
    i2c_stop();
    return 1;
}

uint8_t nfc_write_block(uint8_t block, const uint8_t *buf16) {
    uint8_t i;

    i2c_start();
    if (!i2c_write((uint8_t)(NTAG_ADDR << 1))) { i2c_stop(); return 0; }
    i2c_write(block);
    for (i = 0; i < 16; i++)
        i2c_write(buf16[i]);
    i2c_stop();

    delay_ms(10);             /* EEPROM commit (~4ms typ) */
    return 1;
}

uint8_t nfc_read_uid(uint8_t *uid7) {
    uint8_t b[16];
    if (!nfc_read_block(0, b)) return 0;
    uid7[0] = b[0];
    uid7[1] = b[1];
    uid7[2] = b[2];
    uid7[3] = b[4];
    uid7[4] = b[5];
    uid7[5] = b[6];
    uid7[6] = b[7];
    return (b[0] == 0x04) ? 1 : 0;   /* NXP manufacturer code */
}

uint8_t nfc_read_label(uint8_t *lbl4) {
    uint8_t b[16];
    if (!nfc_read_block(0x0C, b)) return 0;
    /* ID word at bytes 12..15, little-endian -> present big-endian. */
    lbl4[0] = b[15];
    lbl4[1] = b[14];
    lbl4[2] = b[13];
    lbl4[3] = b[12];
    return 1;
}
