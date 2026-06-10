#include "aws_iot.h"
#include "ota_bridge.h"
// Increase loop task stack size
SET_LOOP_TASK_STACK_SIZE(32 * 1024);
void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[BOOT] ESP32-S3 OTA Bridge starting");

    OTABridge_Init();   // init UART to STM32
    AWS_IoT_Init();     // connect WiFi + AWS
}

void loop()
{
    AWS_IoT_Loop();     // MQTT keep-alive + job handling
    delay(10);
}