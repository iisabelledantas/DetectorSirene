#include "anomaly_detection_task.h"
#include "modelo_sirene.h"         
#include "normalizacao_params.h"   
#include <Chirale_TensorFlowLite.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#if LATENCY_INSTRUMENTATION_ENABLED
#include <esp_timer.h>
#endif

namespace {
    const tflite::Model *tfModel = nullptr;
    tflite::MicroInterpreter *interpreter = nullptr;
    TfLiteTensor *inputTensor = nullptr;
    TfLiteTensor *outputTensor = nullptr;

    alignas(16) uint8_t tensorArena[TENSOR_ARENA_SIZE];
}

static void setAlertPeripherals(bool suspectState, bool confirmedState) {
    xSemaphoreTake(peripheralMutex, portMAX_DELAY);

    bool silent = silentModeEnabled;

    if (confirmedState) {
        digitalWrite(LED_PIN, HIGH);
        digitalWrite(BUZZER_PIN, silent ? LOW : HIGH);
    } else if (suspectState) {
        static bool blinkState = false;
        blinkState = !blinkState;
        digitalWrite(LED_PIN, blinkState ? HIGH : LOW);
        digitalWrite(BUZZER_PIN, LOW);
    } else {
        digitalWrite(LED_PIN, LOW);
        digitalWrite(BUZZER_PIN, LOW);
    }

    xSemaphoreGive(peripheralMutex);
}

static bool setupModel() {
    tfModel = tflite::GetModel(modelo_sirene_tflite);
    if (tfModel->version() != TFLITE_SCHEMA_VERSION) {
        Serial.println("ERRO: Versao de schema do modelo incompativel!");
        return false;
    }

    static tflite::AllOpsResolver resolver;

    static tflite::MicroInterpreter static_interpreter(
        tfModel, resolver, tensorArena, TENSOR_ARENA_SIZE
    );

    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        Serial.println("ERRO: AllocateTensors falhou - aumente TENSOR_ARENA_SIZE");
        return false;
    }

    inputTensor = interpreter->input(0);
    outputTensor = interpreter->output(0);

    return true;
}

static void prepareInput(const FeatureVector &fv) {
    for (int i = 0; i < N_FEATURES; i++) {
        float normalized = (fv.values[i] - feature_mean[i]) / feature_std[i];
        int32_t quantized = (int32_t)roundf(normalized / INPUT_SCALE) + INPUT_ZERO_POINT;
        if (quantized > 127) quantized = 127;
        if (quantized < -128) quantized = -128;
        inputTensor->data.int8[i] = (int8_t)quantized;
    }
}

static float runInference() {
    interpreter->Invoke();
    int8_t outputQuantized = outputTensor->data.int8[0];
    float probability = (outputQuantized - OUTPUT_ZERO_POINT) * OUTPUT_SCALE;
    return probability;
}

#if LATENCY_INSTRUMENTATION_ENABLED

namespace {
    int64_t statCount = 0;
    int64_t sumEndToEndUs = 0;
    int64_t minEndToEndUs = INT64_MAX;
    int64_t maxEndToEndUs = 0;

    int64_t sumInferenceUs = 0;
    int64_t minInferenceUs = INT64_MAX;
    int64_t maxInferenceUs = 0;
}

static void recordLatencySample(int64_t captureToFeaturesUs, int64_t featuresToDetectUs,
                                 int64_t inferenceUs, int64_t endToEndUs) {
    statCount++;
    sumEndToEndUs += endToEndUs;
    if (endToEndUs < minEndToEndUs) minEndToEndUs = endToEndUs;
    if (endToEndUs > maxEndToEndUs) maxEndToEndUs = endToEndUs;

    sumInferenceUs += inferenceUs;
    if (inferenceUs < minInferenceUs) minInferenceUs = inferenceUs;
    if (inferenceUs > maxInferenceUs) maxInferenceUs = inferenceUs;

    Serial.printf(
        "lat_captura_features=%.3fms lat_features_deteccao=%.3fms lat_inferencia=%.3fms lat_fim_a_fim=%.3fms\n",
        captureToFeaturesUs / 1000.0f, featuresToDetectUs / 1000.0f,
        inferenceUs / 1000.0f, endToEndUs / 1000.0f
    );

    if (statCount >= LATENCY_STATS_WINDOW) {
        Serial.printf(
            "=== ESTATISTICAS DE LATENCIA (ultimas %lld amostras) ===\n"
            "  Fim-a-fim: min=%.3fms media=%.3fms max=%.3fms\n"
            "  Inferencia (Invoke): min=%.3fms media=%.3fms max=%.3fms\n"
            "==========================================================\n",
            (long long)statCount,
            minEndToEndUs / 1000.0f, (sumEndToEndUs / (float)statCount) / 1000.0f, maxEndToEndUs / 1000.0f,
            minInferenceUs / 1000.0f, (sumInferenceUs / (float)statCount) / 1000.0f, maxInferenceUs / 1000.0f
        );
        statCount = 0;
        sumEndToEndUs = 0; minEndToEndUs = INT64_MAX; maxEndToEndUs = 0;
        sumInferenceUs = 0; minInferenceUs = INT64_MAX; maxInferenceUs = 0;
    }
}
#endif

void anomalyDetectionTaskFn(void *pvParameters) {
    FeatureVector fv;
    int consecutivePositives = 0;

    pinMode(LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    Serial.println("Inicializando modelo TFLite (Chirale_TensorFlowLite)...");

    if (!setupModel()) {
        for (;;) {
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    Serial.println("Modelo inicializado com sucesso!");

    for (;;) {
        if (xQueueReceive(featureQueue, &fv, portMAX_DELAY) == pdTRUE) {

#if LATENCY_INSTRUMENTATION_ENABLED
            int64_t detectStartUs = esp_timer_get_time();
#endif
            prepareInput(fv);
#if LATENCY_INSTRUMENTATION_ENABLED
            int64_t inferenceStartUs = esp_timer_get_time();
#endif
            float probability = runInference();
#if LATENCY_INSTRUMENTATION_ENABLED
            int64_t nowUs = esp_timer_get_time();
            int64_t inferenceUs = nowUs - inferenceStartUs;
            int64_t captureToFeaturesUs = fv.featureTimestampUs - fv.captureTimestampUs;
            int64_t featuresToDetectUs = detectStartUs - fv.featureTimestampUs;
            int64_t endToEndUs = nowUs - fv.captureTimestampUs;
            recordLatencySample(captureToFeaturesUs, featuresToDetectUs, inferenceUs, endToEndUs);
#endif

            bool isPositive = probability >= DETECTION_THRESHOLD;

            if (isPositive) {
                consecutivePositives++;
            } else {
                consecutivePositives = 0;
            }

            bool suspectAlert = consecutivePositives >= SUSPECT_WINDOWS_COUNT;
            bool confirmedAlert = consecutivePositives >= CONFIRM_WINDOWS_COUNT;

            setAlertPeripherals(suspectAlert && !confirmedAlert, confirmedAlert);

            Serial.printf("prob=%.3f consecutive=%d confirmed=%d\n", probability, consecutivePositives, confirmedAlert);
        }
    }
}
