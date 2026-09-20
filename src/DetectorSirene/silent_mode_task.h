#ifndef SILENT_MODE_TASK_H
#define SILENT_MODE_TASK_H

#include <Arduino.h>
#include "config.h"

extern SemaphoreHandle_t peripheralMutex;
extern volatile bool silentModeEnabled;

void silentModeTaskFn(void *pvParameters);

#endif
