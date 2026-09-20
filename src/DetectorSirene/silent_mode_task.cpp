#include "silent_mode_task.h"

void silentModeTaskFn(void *pvParameters) {
    pinMode(SILENT_BUTTON_PIN, INPUT_PULLUP);

    bool lastRawState = HIGH;
    TickType_t lastChangeTick = 0;
    const TickType_t debounceTicks = pdMS_TO_TICKS(50);

    for (;;) {
        bool rawState = digitalRead(SILENT_BUTTON_PIN);
        TickType_t now = xTaskGetTickCount();

        if (rawState != lastRawState && (now - lastChangeTick) > debounceTicks) {
            lastChangeTick = now;
            lastRawState = rawState;

            if (rawState == LOW) {
                xSemaphoreTake(peripheralMutex, portMAX_DELAY);
                silentModeEnabled = !silentModeEnabled;
                xSemaphoreGive(peripheralMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));   
    }
}
