#include "aws_iot.h"
#include "aws_certs.h"
#include "ota_bridge.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static WiFiClientSecure  tls_client;
static PubSubClient      mqtt_client(tls_client);

static char     pending_job_id[64]  = {0};
static char     pending_url[2048]   = {0};
static uint32_t pending_crc32       = 0;
static uint32_t pending_size        = 0;
static uint32_t pending_version     = 0;
static bool     job_pending         = false;

/* ── Global — not on stack ───────────────────────────────────────────── */
static StaticJsonDocument<4096> doc;
static uint8_t chunk[FRAME_MAX_PAYLOAD];

static void report_job_status(const char *job_id, const char *status)
{
    char topic[128];
    snprintf(topic, sizeof(topic), TOPIC_JOB_UPDATE, job_id);
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"status\":\"%s\"}", status);
    mqtt_client.publish(topic, payload);
    Serial.printf("[AWS] Job %s → %s\n", job_id, status);
}

static HTTPClient http;   // ← global, not on stack

static void perform_ota(void)
{
    Serial.println("[OTA] Starting firmware download");
    report_job_status(pending_job_id, "IN_PROGRESS");

    if (!OTABridge_SendJob(pending_size, pending_crc32, pending_version))
    {
        Serial.println("[OTA] STM32 rejected job");
        report_job_status(pending_job_id, "FAILED");
        return;
    }

    http.begin(pending_url);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    Serial.println("[OTA] Sending HTTP GET...");
    int http_code = http.GET();
    Serial.printf("[OTA] HTTP code: %d\n", http_code);

    if (http_code != HTTP_CODE_OK)
    {
        Serial.printf("[OTA] HTTP GET failed: %d\n", http_code);
        report_job_status(pending_job_id, "FAILED");
        http.end();
        return;
    }

    WiFiClient *stream  = http.getStreamPtr();
    uint32_t   total    = pending_size;
    uint32_t   received = 0;
    uint16_t   seq      = 1;

    Serial.printf("[OTA] Firmware size: %u bytes\n", total);

    while (received < total)
    {
        uint16_t to_read = (uint16_t)min((uint32_t)FRAME_MAX_PAYLOAD,
                                          total - received);
        uint16_t actual  = 0;
        uint32_t t_start = millis();

        while (actual < to_read && (millis() - t_start) < 5000)
        {
            if (stream->available())
                chunk[actual++] = stream->read();
            else
                delay(1);
        }

        if (actual == 0)
        {
            Serial.println("[OTA] Stream timeout");
            report_job_status(pending_job_id, "FAILED");
            http.end();
            return;
        }

        if (!OTABridge_SendChunk(seq, chunk, actual))
        {
            Serial.printf("[OTA] Chunk %d failed\n", seq);
            report_job_status(pending_job_id, "FAILED");
            http.end();
            return;
        }

        received += actual;
        seq++;

        Serial.printf("[OTA] Progress: %u / %u bytes\n", received, total);
    }

    http.end();
    Serial.println("[OTA] All chunks sent");

    if (!OTABridge_SendDone(seq))
    {
        Serial.println("[OTA] DONE frame failed");
        report_job_status(pending_job_id, "FAILED");
        return;
    }

    report_job_status(pending_job_id, "SUCCEEDED");
    Serial.println("[OTA] Complete — STM32 is rebooting");
}

static void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
    Serial.printf("[MQTT] Message on topic: %s\n", topic);

    doc.clear();
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err)
    {
        Serial.print("[MQTT] JSON parse error: ");
        Serial.println(err.c_str());
        return;
    }

    if (!doc.containsKey("execution"))
    {
        Serial.println("[MQTT] No pending job");
        return;
    }

    const char *job_id  = doc["execution"]["jobId"];
    const char *url     = doc["execution"]["jobDocument"]["firmwareUrl"];
    uint32_t    crc32   = doc["execution"]["jobDocument"]["crc32"];
    uint32_t    size    = doc["execution"]["jobDocument"]["size"];
    uint32_t    version = doc["execution"]["jobDocument"]["version"];

    Serial.printf("[MQTT] job_id:  %s\n", job_id  ? job_id  : "NULL");
    Serial.printf("[MQTT] size:    %u\n", size);
    Serial.printf("[MQTT] crc32:   %u\n", crc32);
    Serial.printf("[MQTT] version: %u\n", version);

    if (!job_id || !url || size == 0)
    {
        Serial.println("[MQTT] Invalid job document");
        return;
    }

    strncpy(pending_job_id, job_id, sizeof(pending_job_id) - 1);
    strncpy(pending_url,    url,    sizeof(pending_url)    - 1);
    pending_crc32   = crc32;
    pending_size    = size;
    pending_version = version;
    job_pending     = true;

    Serial.printf("[MQTT] Job received: %s v%u size=%u\n",
                  job_id, version, size);
}

static void wifi_connect(void)
{
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WiFi] Connected. IP: %s\n",
                  WiFi.localIP().toString().c_str());
}

static bool aws_connect(void)
{
    tls_client.setCACert(AWS_ROOT_CA);
    tls_client.setCertificate(AWS_DEVICE_CERT);
    tls_client.setPrivateKey(AWS_PRIVATE_KEY);

    mqtt_client.setServer(AWS_ENDPOINT, AWS_PORT);
    mqtt_client.setCallback(mqtt_callback);
    mqtt_client.setBufferSize(4096);

    Serial.print("[AWS] Connecting to IoT Core");
    while (!mqtt_client.connected())
    {
        if (mqtt_client.connect(THING_NAME))
        {
            Serial.println("\n[AWS] Connected");
            mqtt_client.subscribe(TOPIC_JOB_NOTIFY);
            mqtt_client.subscribe(TOPIC_JOB_GET_RESP);
            mqtt_client.publish(TOPIC_JOB_GET, "{}");
            return true;
        }
        Serial.print(".");
        delay(1000);
    }
    return false;
}

void AWS_IoT_Init(void)
{
    wifi_connect();
    aws_connect();
}

void AWS_IoT_Loop(void)
{
    if (!mqtt_client.connected())
    {
        Serial.println("[AWS] Reconnecting...");
        aws_connect();
    }
    mqtt_client.loop();
    if (job_pending)
    {
        job_pending = false;
        perform_ota();
    }
}