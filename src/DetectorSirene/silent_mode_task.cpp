#include "silent_mode_task.h"

// Task de baixíssima prioridade: só existe para demonstrar mais um ponto de
// concorrência real (leitura de GPIO + escrita em uma flag compartilhada com a Task 3).
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

            // Botão ativo em LOW (INPUT_PULLUP): alterna no toque (borda de descida)
            if (rawState == LOW) {
                xSemaphoreTake(peripheralMutex, portMAX_DELAY);
                silentModeEnabled = !silentModeEnabled;
                xSemaphoreGive(peripheralMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));   // polling leve - task de baixa prioridade
    }
}
