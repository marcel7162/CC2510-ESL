#ifndef NFC_H
#define NFC_H

#include <stdint.h>

/*
 * Driver for the NXP NT3H2111 (NTAG I2C plus 2k) on the ESL board.
 * Bit-banged I2C on P0.4 (SDA) / P0.6 (SCL), chip powered via P1.0.
 *
 * Memory is addressed in 16-byte I2C blocks:
 *   block 0       = UID / lock / Capability Container (read-only identity)
 *   block 1..     = user memory (safe to read/write)
 * We reserve I2C block 1 for PriceTag config (magic + assigned address).
 */

#define NFC_CFG_BLOCK  1          /* user-memory block for our config */
#define NFC_MAGIC0     0x50       /* 'P' */
#define NFC_MAGIC1     0x54       /* 'T' */

void    nfc_init(void);

/* Read/write one 16-byte block. Returns 1 on success (chip ACKed). */
uint8_t nfc_read_block(uint8_t block, uint8_t *buf16);
uint8_t nfc_write_block(uint8_t block, const uint8_t *buf16);

/* Read the 7-byte factory UID (NXP layout: block0[0,1,2,4,5,6,7]).
 * Returns 1 on success and when buf[0]==0x04 (NXP manufacturer code). */
uint8_t nfc_read_uid(uint8_t *uid7);

/* Read the 4-byte printed label ID (SES-imagotag asset id). Stored as a
 * little-endian word at I2C block 0x0C bytes 12..15; returned big-endian so
 * lbl4 reads the same as the printed label (e.g. B0 DD 52 AC). Best-effort:
 * this is SES-specific data, not guaranteed on every unit. Returns 1 on a
 * successful I2C read. */
uint8_t nfc_read_label(uint8_t *lbl4);

#endif
