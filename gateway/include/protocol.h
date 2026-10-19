/*
 * protocol.h  —  Funk-Protokoll zwischen Gateway und CC2510-Tag.
 *
 * Dieselbe Definition muss spaeter auf der Tag-Seite (CC2510-Firmware)
 * implementiert werden, damit beide Seiten dieselbe "Sprache" sprechen.
 *
 * Ablauf einer Bilduebertragung:
 *   1. IMG_START (pro Farbebene)  -> Tag: epd_stream_start(plane)
 *   2. n x IMG_DATA (fortlaufende Sequenznummer), jeweils vom Tag per ACK
 *      bestaetigt; fehlt ein ACK, sendet das Gateway den Chunk erneut.
 *   3. IMG_END   -> Tag: epd_stream_end()
 *   (Schritte 1-3 je einmal fuer die S/W-Ebene und die Rot-Ebene)
 *   4. COMMIT    -> Tag: epd_flushDisplay() + epd_powerOff()
 *
 * Jedes Paket passt in eine nRF24-Nutzlast (max. 32 Byte).
 */

#ifndef ESL_PROTOCOL_H
#define ESL_PROTOCOL_H

#include <stdint.h>

// Paket-Typen (erstes Byte der Funk-Nutzlast)
#define PKT_IMG_START  0x01  // [type][plane][len_hi][len_lo]
#define PKT_IMG_DATA   0x02  // [type][seq_hi][seq_lo][payload...]
#define PKT_IMG_END    0x03  // [type][plane]
#define PKT_COMMIT     0x04  // [type]
#define PKT_ACK        0x80  // Antwort vom Tag: [PKT_ACK][seq_hi][seq_lo]

// Farbebenen (entsprechen den EPD-Kommandos)
#define PLANE_BW   0x10      // Schwarz/Weiss
#define PLANE_RED  0x13      // Rot

// Groessen
#define RF_MAX_PAYLOAD     32                 // nRF24-Maximum
#define DATA_HEADER_LEN    3                  // [type][seq_hi][seq_lo]
#define DATA_CHUNK_LEN     (RF_MAX_PAYLOAD - DATA_HEADER_LEN)  // 29 Nutzbytes/Chunk

// Zuverlaessigkeit (Software-ACK, da der CC2510 kein ESB-Auto-ACK kann)
#define ACK_TIMEOUT_MS     30    // wie lange auf ein ACK warten
#define MAX_RETRIES        8     // Wiederholungen pro Chunk, bevor abgebrochen wird

#endif // ESL_PROTOCOL_H
