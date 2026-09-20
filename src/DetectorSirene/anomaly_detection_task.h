#ifndef ANOMALY_DETECTION_TASK_H
#define ANOMALY_DETECTION_TASK_H

#include <Arduino.h>
#include "config.h"
#include "shared_types.h"

// Fila compartilhada (declarada em main)
extern QueueHandle_t featureQueue;

// Mutex protegendo o acesso aos periféricos de alerta (LED/buzzer) e a flag de modo silencioso
extern SemaphoreHandle_t peripheralMutex;
extern volatile bool silentModeEnabled;   // lida/escrita sempre sob peripheralMutex

void anomalyDetectionTaskFn(void *pvParameters);

#endif
