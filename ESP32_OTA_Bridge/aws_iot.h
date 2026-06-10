#ifndef AWS_IOT_H
#define AWS_IOT_H

// ── Wi-Fi ──────────────────────────────────────────────────────────────
#define WIFI_SSID        "VINAY"
#define WIFI_PASSWORD    "IOT123456"

// ── AWS IoT Core endpoint ──────────────────────────────────────────────
#define AWS_ENDPOINT     "a2d695axuhbw0-ats.iot.ap-south-1.amazonaws.com"
#define AWS_PORT         8883

// ── Thing name ─────────────────────────────────────────────────────────
#define THING_NAME       "STM32_OTA_Device"

// ── MQTT topics ────────────────────────────────────────────────────────
#define TOPIC_JOB_NOTIFY    "$aws/things/" THING_NAME "/jobs/notify-next"
#define TOPIC_JOB_GET       "$aws/things/" THING_NAME "/jobs/$next/get"
#define TOPIC_JOB_GET_RESP  "$aws/things/" THING_NAME "/jobs/$next/get/accepted"
#define TOPIC_JOB_UPDATE    "$aws/things/" THING_NAME "/jobs/%s/update"

// ── Certificates ───────────────────────────────────────────────────────
extern const char AWS_ROOT_CA[];
extern const char AWS_DEVICE_CERT[];
extern const char AWS_PRIVATE_KEY[];

// ── Function declarations ──────────────────────────────────────────────
void AWS_IoT_Init(void);   // ← ADD THIS
void AWS_IoT_Loop(void);   // ← ADD THIS

#endif