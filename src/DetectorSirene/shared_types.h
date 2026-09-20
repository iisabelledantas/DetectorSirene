#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H

#include <stdint.h>
#include "config.h"

typedef struct {
    float samples[WINDOW_SIZE];
#if LATENCY_INSTRUMENTATION_ENABLED
    int64_t captureTimestampUs; 
#endif
} AudioWindow;

typedef struct {
    float values[N_FEATURES];
#if LATENCY_INSTRUMENTATION_ENABLED
    int64_t captureTimestampUs;   
    int64_t featureTimestampUs;  
#endif
} FeatureVector;

#endif
