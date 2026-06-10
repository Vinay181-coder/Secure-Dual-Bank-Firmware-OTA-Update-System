/*
 * metadata.h
 *
 *  Created on: May 29, 2026
 *      Author: Asus
 */

#ifndef METADATA_H
#define METADATA_H

#include <stdint.h>

#define BOOTLOADER_BASE     0x08000000UL
#define METADATA_BASE       0x08008000UL
#define BANK_A_ADDR         0x0800C000UL
#define BANK_B_ADDR         0x08020000UL

#define BANK_A_MAX_SIZE     (80U  * 1024U)
#define BANK_B_MAX_SIZE     (256U * 1024U)

#define BOOT_FLAG_BANK_A    0xAAAAAAAAUL
#define BOOT_FLAG_BANK_B    0xBBBBBBBBUL
#define BOOT_FLAG_INVALID   0xFFFFFFFFUL

typedef struct
{
    uint32_t boot_flag;
    uint32_t fw_version;
    uint32_t fw_crc32;
    uint32_t fw_size;
    uint32_t fw_bank;
    uint32_t reserved[3];
} boot_metadata_t;

#define METADATA  ((volatile boot_metadata_t *)METADATA_BASE)

#endif
