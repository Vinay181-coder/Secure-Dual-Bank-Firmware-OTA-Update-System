/*
 * ota.h
 */

#ifndef OTA_H
#define OTA_H

#include <stdint.h>

#define FRAME_SOF1          0xAAU
#define FRAME_SOF2          0x55U
#define FRAME_TYPE_JOB      0x01U
#define FRAME_TYPE_CHUNK    0x02U
#define FRAME_TYPE_DONE     0x03U
#define FRAME_TYPE_ACK      0x04U
#define FRAME_TYPE_NACK     0x05U
#define FRAME_MAX_PAYLOAD   256U
#define FRAME_OVERHEAD      8U
#define FRAME_MAX_SIZE      (FRAME_MAX_PAYLOAD + FRAME_OVERHEAD)
#define RING_BUF_SIZE       1024U

typedef struct __attribute__((packed))
{
    uint32_t fw_size;
    uint32_t fw_crc32;
    uint32_t fw_version;
} job_info_t;

typedef enum
{
    OTA_STATE_IDLE = 0,
    OTA_STATE_READY,
    OTA_STATE_RECEIVING,
    OTA_STATE_VERIFY,
    OTA_STATE_DONE,
    OTA_STATE_ERROR
} ota_state_t;

void        OTA_Init(void);
void        OTA_Process(void);
void        OTA_UART_RxISR(uint8_t b);  /* call from USART2 ISR */
ota_state_t OTA_GetState(void);

#endif /* OTA_H */
