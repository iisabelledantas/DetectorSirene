#include "config.h"
#include "shared_types.h"
#include "audio_capture_task.h"
#include "feature_extraction_task.h"
#include "anomaly_detection_task.h"
#include "silent_mode_task.h"

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

    xTaskCreatePinnedToCore(
        audioCaptureTaskFn, "AudioCapture",
        8192,               
        NULL,
        3,                 
        NULL,
        1                   
    );

    xTaskCreatePinnedToCore(
        featureExtractionTaskFn, "FeatureExtraction",
        8192,             
        NULL,
        2,                
        NULL,
        1
    );

    
    xTaskCreatePinnedToCore(
        anomalyDetectionTaskFn, "AnomalyDetection",
        8192,     
        NULL,
        1,       
        NULL,
        1
    );

    xTaskCreatePinnedToCore(
        silentModeTaskFn, "SilentMode",
        2048,
        NULL,
        1,
        NULL,
        0 
    );

    Serial.println("Todas as tasks criadas. Sistema em execucao.");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
