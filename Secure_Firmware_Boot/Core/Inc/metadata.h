/*
 * metadata.h
 *
 *  Created on: May 29, 2026
 *      Author: Asus
 */

#ifndef METADATA_H
#define METADATA_H

#include <stdint.h>

/* ── Flash region addresses ──────────────────────────────────────────── */
#define BOOTLOADER_BASE     0x08000000UL   /* sectors 0–1  32 KB */
#define METADATA_BASE       0x08008000UL   /* sector  2    16 KB */
#define BANK_A_ADDR         0x0800C000UL   /* sectors 3–4  80 KB */
#define BANK_B_ADDR         0x08020000UL   /* sectors 5–6  256 KB */

#define BANK_A_MAX_SIZE     (80U  * 1024U)
#define BANK_B_MAX_SIZE     (256U * 1024U)

/* ── Boot flags ──────────────────────────────────────────────────────── */
#define BOOT_FLAG_BANK_A    0xAAAAAAAAUL   /* normal boot  */
#define BOOT_FLAG_BANK_B    0xBBBBBBBBUL   /* OTA pending  */
#define BOOT_FLAG_INVALID   0xFFFFFFFFUL   /* erased flash */

/* ── Metadata struct — lives at METADATA_BASE ────────────────────────── */
typedef struct
{
    uint32_t boot_flag;    /* BOOT_FLAG_BANK_A or BOOT_FLAG_BANK_B        */
    uint32_t fw_version;   /* firmware version number                      */
    uint32_t fw_crc32;     /* expected CRC32 of the OTA image              */
    uint32_t fw_size;      /* size of OTA image in bytes                   */
    uint32_t fw_bank;      /* last booted bank address                     */
    uint32_t reserved[3];  /* pad to 32 bytes — future use                 */
} boot_metadata_t;

/* Pointer to metadata in flash — read directly, write via write_metadata() */
#define METADATA  ((volatile boot_metadata_t *)METADATA_BASE)

#endif /* METADATA_H */
