#include "config.h"
#include "shared_types.h"
#include "audio_capture_task.h"
#include "feature_extraction_task.h"
#include "anomaly_detection_task.h"
#include "silent_mode_task.h"

// ==================== RECURSOS COMPARTILHADOS ====================
QueueHandle_t audioQueue;
QueueHandle_t featureQueue;
SemaphoreHandle_t peripheralMutex;
volatile bool silentModeEnabled = false;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Detector de Sirene - inicializando...");

    audioQueue = xQueueCreate(AUDIO_QUEUE_LENGTH, sizeof(AudioWindow));
    featureQueue = xQueueCreate(FEATURE_QUEUE_LENGTH, sizeof(FeatureVector));
    peripheralMutex = xSemaphoreCreateMutex();

    if (audioQueue == NULL || featureQueue == NULL || peripheralMutex == NULL) {
        Serial.println("ERRO: falha ao criar filas/mutex. Reiniciando...");
        ESP.restart();
    }

    // Task 1 - Captura de áudio (ALTA prioridade, core dedicado ao I2S)
    xTaskCreatePinnedToCore(
        audioCaptureTaskFn, "AudioCapture",
        8192,               // 4096 causava stack overflow silencioso (watchdog reset) - ver audio_capture_task.cpp
        NULL,
        3,                  // prioridade alta
        NULL,
        1                   // core 1 (deixa core 0 livre para WiFi/BT se necessário)
    );

    // Task 2 - Extração de features (prioridade MÉDIA)
    xTaskCreatePinnedToCore(
        featureExtractionTaskFn, "FeatureExtraction",
        8192,               // FFT/MFCC usam mais stack
        NULL,
        2,                  // prioridade média
        NULL,
        1
    );

    // Task 3 - Detecção de anomalia (prioridade BAIXA)
    xTaskCreatePinnedToCore(
        anomalyDetectionTaskFn, "AnomalyDetection",
        8192,               // TFLite Micro usa stack considerável
        NULL,
        1,                  // prioridade baixa
        NULL,
        1
    );

    // Task 4 - Botão de modo silencioso (prioridade BAIXA)
    xTaskCreatePinnedToCore(
        silentModeTaskFn, "SilentMode",
        2048,
        NULL,
        1,
        NULL,
        0                   // core 0 - task leve, não compete com o pipeline de áudio
    );

    Serial.println("Todas as tasks criadas. Sistema em execucao.");
}

void loop() {
    // Toda a lógica roda nas tasks do FreeRTOS - loop() fica vazio.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
