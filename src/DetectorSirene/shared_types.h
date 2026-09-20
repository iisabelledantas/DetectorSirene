#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H

#include <stdint.h>
#include "config.h"

// Janela de áudio pronta, enviada da Task 1 (Captura) para a Task 2 (Features)
typedef struct {
    float samples[WINDOW_SIZE];
#if LATENCY_INSTRUMENTATION_ENABLED
    int64_t captureTimestampUs;   // esp_timer_get_time() no instante em que a janela ficou pronta
#endif
} AudioWindow;

// Vetor de features, enviado da Task 2 (Features) para a Task 3 (Detecção)
// Ordem DEVE bater com o treino: [RMS, spectral_centroid, mfcc_0, ..., mfcc_9]
typedef struct {
    float values[N_FEATURES];
#if LATENCY_INSTRUMENTATION_ENABLED
    int64_t captureTimestampUs;    // propagado desde a Task 1, para medir latência fim-a-fim
    int64_t featureTimestampUs;    // esp_timer_get_time() quando as features ficaram prontas
#endif
} FeatureVector;

#endif
