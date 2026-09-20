#ifndef AUDIO_CAPTURE_TASK_H
#define AUDIO_CAPTURE_TASK_H

#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
#include "shared_types.h"

// Fila compartilhada: Task 1 -> Task 2 (declarada em main, extern aqui)
extern QueueHandle_t audioQueue;

void i2sInit();
void audioCaptureTaskFn(void *pvParameters);

#endif
