/*
 * ota.c
 */

#include "ota.h"
#include "metadata.h"
#include "main.h"
#include <string.h>

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

/* ── Ring buffer ──────────────────────────────────────────────────────── */
static volatile uint8_t  rb_buf[RING_BUF_SIZE];
static volatile uint32_t rb_head = 0;
static volatile uint32_t rb_tail = 0;

static inline void rb_push(uint8_t b)
{
    uint32_t next = (rb_head + 1U) & (RING_BUF_SIZE - 1U);
    if (next != rb_tail) { rb_buf[rb_head] = b; rb_head = next; }
}
static inline uint8_t rb_available(void) { return rb_head != rb_tail; }
static inline uint8_t rb_pop(void)
{
    uint8_t b = rb_buf[rb_tail];
    rb_tail = (rb_tail + 1U) & (RING_BUF_SIZE - 1U);
    return b;
}

/* ── CRC16-CCITT ──────────────────────────────────────────────────────── */
static uint16_t crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8U;
        for (uint8_t j = 0; j < 8U; j++)
            crc = (crc & 0x8000U) ? (crc << 1U) ^ 0x1021U : (crc << 1U);
    }
    return crc;
}

static uint32_t compute_crc32(uint32_t addr, uint32_t size)
{
    __HAL_RCC_CRC_CLK_ENABLE();
    CRC->CR = CRC_CR_RESET;

    uint32_t *src   = (uint32_t *)addr;
    uint32_t  words = size / 4U;
    for (uint32_t i = 0; i < words; i++)
        CRC->DR = src[i];

    uint32_t remaining = size % 4U;
    if (remaining > 0U)
    {
        uint32_t last = 0U;
        uint8_t *tail = (uint8_t *)(addr + words * 4U);
        for (uint32_t i = 0; i < remaining; i++)
            last |= ((uint32_t)tail[i] << (i * 8U));
        CRC->DR = last;
    }
    return CRC->DR;
}


/* ── Flash helpers ────────────────────────────────────────────────────── */
static uint8_t flash_erase_bank_b(void)
{
    FLASH_EraseInitTypeDef e = {0};
    uint32_t err = 0U;
    e.TypeErase    = FLASH_TYPEERASE_SECTORS;
    e.Sector       = FLASH_SECTOR_5;
    e.NbSectors    = 2U;
    e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e, &err);
    HAL_FLASH_Lock();
    return (st == HAL_OK) ? 1U : 0U;
}

static uint8_t flash_write_chunk(uint32_t offset, uint8_t *data, uint16_t len)
{
    uint32_t addr = BANK_B_ADDR + offset;
    HAL_FLASH_Unlock();
    for (uint16_t i = 0; i < len; i += 4U)
    {
        uint32_t word  = 0U;
        uint16_t bytes = ((len - i) >= 4U) ? 4U : (len - i);
        memcpy(&word, &data[i], bytes);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return 0U;
        }
        addr += 4U;
    }
    HAL_FLASH_Lock();
    return 1U;
}

static void write_metadata_ota(job_info_t *job)
{
    FLASH_EraseInitTypeDef e = {0};
    uint32_t err = 0U;
    e.TypeErase    = FLASH_TYPEERASE_SECTORS;
    e.Sector       = FLASH_SECTOR_2;
    e.NbSectors    = 1U;
    e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    HAL_FLASH_Unlock();
    HAL_FLASHEx_Erase(&e, &err);
    boot_metadata_t m = {0};
    m.boot_flag  = BOOT_FLAG_BANK_B;
    m.fw_version = job->fw_version;
    m.fw_crc32   = job->fw_crc32;
    m.fw_size    = job->fw_size;
    m.fw_bank    = BANK_B_ADDR;
    uint32_t addr = METADATA_BASE;
    uint32_t *src = (uint32_t *)&m;
    for (uint32_t i = 0; i < sizeof(boot_metadata_t) / 4U; i++)
    {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, src[i]);
        addr += 4U;
    }
    HAL_FLASH_Lock();
}


static void dbg(const char *s)
{
    HAL_UART_Transmit(&huart2,
                      (uint8_t *)s,
                      strlen(s),
                      HAL_MAX_DELAY);
}

/* ── UART TX helpers ──────────────────────────────────────────────────── */
static void send_ack(uint16_t seq)
{
    uint8_t buf[9];
    buf[0] = FRAME_SOF1;
    buf[1] = FRAME_SOF2;
    buf[2] = FRAME_TYPE_ACK;
    buf[3] = (uint8_t)(seq >> 8U);
    buf[4] = (uint8_t)(seq & 0xFFU);
    buf[5] = 0U;
    buf[6] = 0U;
    uint16_t crc = crc16(&buf[2], 5U);
    buf[7] = (uint8_t)(crc >> 8U);
    buf[8] = (uint8_t)(crc & 0xFF);
    HAL_UART_Transmit(&huart1, buf, 9U, 100U);
}

static void send_nack(uint16_t seq)
{
    uint8_t buf[9];
    buf[0] = FRAME_SOF1;
    buf[1] = FRAME_SOF2;
    buf[2] = FRAME_TYPE_NACK;
    buf[3] = (uint8_t)(seq >> 8U);
    buf[4] = (uint8_t)(seq & 0xFFU);
    buf[5] = 0U;
    buf[6] = 0U;
    uint16_t crc = crc16(&buf[2], 5U);
    buf[7] = (uint8_t)(crc >> 8U);
    buf[8] = (uint8_t)(crc & 0xFFU);
    HAL_UART_Transmit(&huart1, buf, 9U, 100U);
}

/* ── Frame parser ─────────────────────────────────────────────────────── */
typedef enum {
    PARSE_SOF1 = 0, PARSE_SOF2, PARSE_TYPE,
    PARSE_SEQ_H, PARSE_SEQ_L,
    PARSE_LEN_H, PARSE_LEN_L,
    PARSE_PAYLOAD, PARSE_CRC_H, PARSE_CRC_L
} parse_state_t;

typedef struct {
    parse_state_t state;
    uint8_t       type;
    uint16_t      seq;
    uint16_t      len;
    uint16_t      idx;
    uint8_t       payload[FRAME_MAX_PAYLOAD];
    uint8_t       crc_h;
} frame_parser_t;

static frame_parser_t parser;
static ota_state_t    ota_state    = OTA_STATE_IDLE;
static job_info_t     current_job  = {0};
static uint32_t       bytes_written = 0U;
static uint16_t       last_seq     = 0xFFFFU;

/* ── Public ───────────────────────────────────────────────────────────── */
void OTA_UART_RxISR(uint8_t b) { rb_push(b); }

void OTA_Init(void)
{
    memset(&parser, 0, sizeof(parser));
    ota_state     = OTA_STATE_IDLE;
    bytes_written = 0U;
    last_seq      = 0xFFFFU;
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
}

static void handle_frame(void)
{
    switch (parser.type)
    {
        case FRAME_TYPE_JOB:
            if (parser.len != sizeof(job_info_t))
                { send_nack(parser.seq); break; }
            memcpy(&current_job, parser.payload, sizeof(job_info_t));
            if (current_job.fw_size == 0U || current_job.fw_size > BANK_B_MAX_SIZE)
                { send_nack(parser.seq); break; }
            if (!flash_erase_bank_b())
                { send_nack(parser.seq); break; }
            bytes_written = 0U;
            last_seq      = 0xFFFFU;
            ota_state     = OTA_STATE_READY;
            send_ack(parser.seq);
            break;

        case FRAME_TYPE_CHUNK:
            if (ota_state != OTA_STATE_READY && ota_state != OTA_STATE_RECEIVING)
                { send_nack(parser.seq); break; }
            if (parser.seq == last_seq)
                { send_ack(parser.seq); break; }
            if (!flash_write_chunk(bytes_written, parser.payload, parser.len))
                { ota_state = OTA_STATE_ERROR; send_nack(parser.seq); break; }
            bytes_written += parser.len;
            last_seq       = parser.seq;
            ota_state      = OTA_STATE_RECEIVING;
            send_ack(parser.seq);
            break;

        case FRAME_TYPE_DONE:
        {
            dbg("\r\nDONE received\r\n");

            if (ota_state != OTA_STATE_RECEIVING)
            {
                dbg("Wrong OTA state\r\n");
                send_nack(parser.seq);
                break;
            }

            ota_state = OTA_STATE_VERIFY;

            char msg[80];
            sprintf(msg, "bytes_written=%lu fw_size=%lu\r\n",
                    bytes_written, current_job.fw_size);
            dbg(msg);

            if (bytes_written != current_job.fw_size)
            {
                dbg("SIZE MISMATCH\r\n");
                ota_state = OTA_STATE_ERROR;
                send_nack(parser.seq);
                break;
            }

            /* ── Compute CRC32 using hardware unit ── */
            __HAL_RCC_CRC_CLK_ENABLE();
            CRC->CR = CRC_CR_RESET;

            uint32_t *src   = (uint32_t *)BANK_B_ADDR;
            uint32_t  words = current_job.fw_size / 4U;
            for (uint32_t i = 0; i < words; i++)
                CRC->DR = src[i];

            uint32_t remaining = current_job.fw_size % 4U;
            if (remaining > 0U)
            {
                uint32_t last = 0U;
                uint8_t *tail = (uint8_t *)(BANK_B_ADDR + words * 4U);
                for (uint32_t i = 0; i < remaining; i++)
                    last |= ((uint32_t)tail[i] << (i * 8U));
                CRC->DR = last;
            }
            uint32_t crc_actual = CRC->DR;

            sprintf(msg, "HW_CRC=0x%08lX Expected=0x%08lX\r\n",
                    crc_actual, current_job.fw_crc32);
            dbg(msg);

            /* ── Also compute first 16 bytes for debug ── */
            dbg("First 16 bytes of bank B: ");
            uint8_t *bptr = (uint8_t *)BANK_B_ADDR;
            for (int i = 0; i < 16; i++)
            {
                sprintf(msg, "%02X ", bptr[i]);
                dbg(msg);
            }
            dbg("\r\n");

            if (crc_actual != current_job.fw_crc32)
            {
                dbg("CRC MISMATCH\r\n");
                ota_state = OTA_STATE_ERROR;
                send_nack(parser.seq);
                break;
            }

            dbg("CRC OK — writing metadata\r\n");
            write_metadata_ota(&current_job);
            ota_state = OTA_STATE_DONE;
            send_ack(parser.seq);
            HAL_Delay(500U);
            NVIC_SystemReset();
            break;
        }

        default:
            send_nack(parser.seq);
            break;
    }
}

void OTA_Process(void)
{
    while (rb_available())
    {
        uint8_t b = rb_pop();
        switch (parser.state)
        {
            case PARSE_SOF1:
                if (b == FRAME_SOF1) parser.state = PARSE_SOF2;
                break;
            case PARSE_SOF2:
                parser.state = (b == FRAME_SOF2) ? PARSE_TYPE : PARSE_SOF1;
                break;
            case PARSE_TYPE:
                parser.type = b; parser.state = PARSE_SEQ_H;
                break;
            case PARSE_SEQ_H:
                parser.seq = (uint16_t)b << 8U; parser.state = PARSE_SEQ_L;
                break;
            case PARSE_SEQ_L:
                parser.seq |= b; parser.state = PARSE_LEN_H;
                break;
            case PARSE_LEN_H:
                parser.len = (uint16_t)b << 8U; parser.state = PARSE_LEN_L;
                break;
            case PARSE_LEN_L:
                parser.len |= b; parser.idx = 0U;
                if (parser.len > FRAME_MAX_PAYLOAD) { parser.state = PARSE_SOF1; break; }
                parser.state = (parser.len > 0U) ? PARSE_PAYLOAD : PARSE_CRC_H;
                break;
            case PARSE_PAYLOAD:
                parser.payload[parser.idx++] = b;
                if (parser.idx >= parser.len) parser.state = PARSE_CRC_H;
                break;
            case PARSE_CRC_H:
                parser.crc_h = b; parser.state = PARSE_CRC_L;
                break;
            case PARSE_CRC_L:
            {
                uint16_t crc_rx = ((uint16_t)parser.crc_h << 8U) | b;
                static uint8_t tmp[5 + FRAME_MAX_PAYLOAD];  // ← add static
                tmp[0] = parser.type;
                tmp[1] = (uint8_t)(parser.seq >> 8U);
                tmp[2] = (uint8_t)(parser.seq & 0xFFU);
                tmp[3] = (uint8_t)(parser.len >> 8U);
                tmp[4] = (uint8_t)(parser.len & 0xFFU);
                if (parser.len > 0U) memcpy(&tmp[5], parser.payload, parser.len);
                if (crc16(tmp, 5U + parser.len) == crc_rx) handle_frame();
                parser.state = PARSE_SOF1;
                break;

            }
            default:
                parser.state = PARSE_SOF1;
                break;
        }
    }
}

ota_state_t OTA_GetState(void) { return ota_state; }
