#include "ota_bridge.h"

// Serial2 on ESP32-S3
#define STM32_SERIAL    Serial2

// ── CRC16-CCITT — must match STM32 implementation exactly ──────────────
static uint16_t crc16(uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

// ── Build and send a frame ─────────────────────────────────────────────
static bool send_frame(uint8_t type, uint16_t seq, uint8_t *payload, uint16_t len)
{
    // ── Change these two to static ─────────────────────────────────
    static uint8_t hdr_payload[5 + FRAME_MAX_PAYLOAD];
    static uint8_t frame[9 + FRAME_MAX_PAYLOAD];
    // ───────────────────────────────────────────────────────────────

    hdr_payload[0] = type;
    hdr_payload[1] = (uint8_t)(seq >> 8);
    hdr_payload[2] = (uint8_t)(seq & 0xFF);
    hdr_payload[3] = (uint8_t)(len >> 8);
    hdr_payload[4] = (uint8_t)(len & 0xFF);
    if (len > 0 && payload != nullptr)
        memcpy(&hdr_payload[5], payload, len);

    uint16_t crc = crc16(hdr_payload, 5 + len);

    frame[0] = FRAME_SOF1;
    frame[1] = FRAME_SOF2;
    frame[2] = type;
    frame[3] = (uint8_t)(seq >> 8);
    frame[4] = (uint8_t)(seq & 0xFF);
    frame[5] = (uint8_t)(len >> 8);
    frame[6] = (uint8_t)(len & 0xFF);
    if (len > 0 && payload != nullptr)
        memcpy(&frame[7], payload, len);
    frame[7 + len] = (uint8_t)(crc >> 8);
    frame[8 + len] = (uint8_t)(crc & 0xFF);

    STM32_SERIAL.write(frame, 9 + len);
    STM32_SERIAL.flush();
    return true;
}

// ── Wait for ACK from STM32 ────────────────────────────────────────────
static bool wait_ack(uint16_t expected_seq)
{
    static uint8_t buf[8];   // ← static
    uint8_t idx = 0;
    uint32_t start = millis();

    while ((millis() - start) < ACK_TIMEOUT_MS)
    {
        if (STM32_SERIAL.available())
        {
            uint8_t b = STM32_SERIAL.read();
            buf[idx++] = b;

            if (idx >= 8)
            {
                if (buf[0] == FRAME_SOF1 && buf[1] == FRAME_SOF2)
                {
                    uint8_t  type = buf[2];
                    uint16_t seq  = ((uint16_t)buf[3] << 8) | buf[4];
                    if (seq == expected_seq)
                    {
                        if (type == FRAME_TYPE_ACK)  return true;
                        if (type == FRAME_TYPE_NACK) return false;
                    }
                }
                memmove(buf, buf + 1, 7);
                idx = 7;
            }
        }
    }
    return false;
}
// ── Public functions ───────────────────────────────────────────────────

void OTABridge_Init(void)
{
    STM32_SERIAL.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    delay(2000);  // wait for STM32 to boot

    // ── Listen for STM32 boot message ─────────────────────────
    Serial.print("[BRIDGE] Waiting for STM32: ");
    uint32_t t = millis();
    while (millis() - t < 3000)
    {
        if (STM32_SERIAL.available())
            Serial.print((char)STM32_SERIAL.read());
    }
    Serial.println();
    // ──────────────────────────────────────────────────────────

    Serial.println("[BRIDGE] UART initialized");
}

bool OTABridge_SendJob(uint32_t fw_size,
                       uint32_t fw_crc32,
                       uint32_t fw_version)
{
    job_info_t job;
    job.fw_size    = fw_size;
    job.fw_crc32   = fw_crc32;
    job.fw_version = fw_version;

    for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++)
    {
        Serial.printf("[BRIDGE] Sending JOB frame (attempt %d)\n", attempt + 1);
        send_frame(FRAME_TYPE_JOB, 0,
                   (uint8_t *)&job, sizeof(job_info_t));

        if (wait_ack(0))
        {
            Serial.println("[BRIDGE] JOB ACK received");
            return true;
        }
        Serial.println("[BRIDGE] JOB NACK/timeout, retrying...");
        delay(500);
    }
    return false;
}

bool OTABridge_SendChunk(uint16_t seq,
                         uint8_t  *data,
                         uint16_t  len)
{
    for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++)
    {
        send_frame(FRAME_TYPE_CHUNK, seq, data, len);

        if (wait_ack(seq))
            return true;

        Serial.printf("[BRIDGE] Chunk %d NACK/timeout, retry %d\n",
                      seq, attempt + 1);
        delay(200);
    }
    return false;
}

bool OTABridge_SendDone(uint16_t seq)
{
    /* Flush stale bytes before sending DONE */
    delay(200);
    while (STM32_SERIAL.available())
        STM32_SERIAL.read();

    /* Send CRC32 as payload so DONE frame has non-zero length */
    uint8_t crc_payload[4];
    crc_payload[0] = 0x00;
    crc_payload[1] = 0x00;
    crc_payload[2] = 0x00;
    crc_payload[3] = 0x00;

    for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++)
    {
        Serial.println("[BRIDGE] Sending DONE frame");
        send_frame(FRAME_TYPE_DONE, seq, crc_payload, 4);

        if (wait_ack(seq))
        {
            Serial.println("[BRIDGE] DONE ACK — STM32 will reset");
            return true;
        }
        Serial.printf("[BRIDGE] DONE attempt %d failed, retrying\n", attempt + 1);
        delay(1000);
    }
    return false;
}