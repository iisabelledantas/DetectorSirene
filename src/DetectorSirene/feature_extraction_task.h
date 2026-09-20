#ifndef FEATURE_EXTRACTION_TASK_H
#define FEATURE_EXTRACTION_TASK_H

#include <Arduino.h>
#include "config.h"
#include "shared_types.h"

// Filas compartilhadas (declaradas em main)
extern QueueHandle_t audioQueue;
extern QueueHandle_t featureQueue;

void featureExtractionTaskFn(void *pvParameters);

#endif
