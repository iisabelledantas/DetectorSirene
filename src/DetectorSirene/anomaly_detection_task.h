#ifndef ANOMALY_DETECTION_TASK_H
#define ANOMALY_DETECTION_TASK_H

#include <Arduino.h>
#include "config.h"
#include "shared_types.h"

extern QueueHandle_t featureQueue;

extern SemaphoreHandle_t peripheralMutex;
extern volatile bool silentModeEnabled;   

void anomalyDetectionTaskFn(void *pvParameters);

#endif
