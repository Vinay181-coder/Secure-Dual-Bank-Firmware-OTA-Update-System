#ifndef OTA_BRIDGE_H
#define OTA_BRIDGE_H

#include <Arduino.h>

// ── Must match ota.h on STM32 exactly ─────────────────────────────────
#define FRAME_SOF1           0xAB    // was 0xAA — WRONG
#define FRAME_SOF2           0xCD    // was 0x55 — WRONG
#define FRAME_TYPE_JOB       0x10    // was 0x01 — WRONG
#define FRAME_TYPE_CHUNK     0x11    // was 0x02 — WRONG
#define FRAME_TYPE_DONE      0x12    // was 0x03 — WRONG
#define FRAME_TYPE_ACK       0x01    // was 0x04 — WRONG
#define FRAME_TYPE_NACK      0x02    // was 0x05 — WRONG
#define FRAME_MAX_PAYLOAD    512     // was 256 — align with STM32
#define ACK_TIMEOUT_MS       15000
#define MAX_RETRIES          5

#define UART_TX_PIN          17
#define UART_RX_PIN          18
#define UART_BAUD            115200

// 12 bytes — matches STM32 ota.c sizeof(job_info_t) check exactly
typedef struct {
    uint32_t fw_size;
    uint32_t fw_crc32;
    uint32_t fw_version;
} __attribute__((packed)) job_info_t;

void  OTABridge_Init(void);
bool  OTABridge_SendJob(uint32_t fw_size, uint32_t fw_crc32, uint32_t fw_version);
bool  OTABridge_SendChunk(uint16_t seq, uint8_t *data, uint16_t len);
bool  OTABridge_SendDone(uint16_t seq);

#endif