#ifndef CONFIG_H
#define CONFIG_H

// ==================== PINOS ====================
#define I2S_WS_PIN    25   // LRCLK / WS
#define I2S_SCK_PIN   26   // BCLK
#define I2S_SD_PIN    22   // SD (dados)
#define I2S_PORT      I2S_NUM_0

#define LED_PIN       2
#define BUZZER_PIN    4
#define SILENT_BUTTON_PIN 15

// ==================== PARÂMETROS DE ÁUDIO ====================
// ATENÇÃO: estes valores DEVEM ser idênticos aos usados no treino (Colab, Célula 5)
#define SAMPLE_RATE   16000
#define WINDOW_SIZE   1024      // amostras por janela (~64ms a 16kHz)
#define HOP_SIZE      512       // overlap entre janelas
#define N_MFCC        10
#define N_FFT         1024
#define N_FEATURES    (2 + N_MFCC)  // RMS + centroid + MFCCs = 12

// Conversão de amostra crua I2S (24 bits alinhados à esquerda em 32 bits) para float [-1, 1]
#define I2S_SAMPLE_SHIFT   8
#define I2S_NORM_FACTOR    8388608.0f   // 2^23

// ==================== DETECÇÃO / DEBOUNCE ====================
#define DETECTION_THRESHOLD       0.5f   // ajuste conforme necessidade de recall/precisão
#define CONFIRM_WINDOWS_COUNT     3      // nº de janelas consecutivas positivas para confirmar alerta
#define SUSPECT_WINDOWS_COUNT     1      // nº de janelas positivas para alerta "suspeito" (pisca LED)

// ==================== BUFFERS / FILAS ====================
#define AUDIO_QUEUE_LENGTH     4     // janelas de áudio pendentes para a Task 2
#define FEATURE_QUEUE_LENGTH   4     // vetores de features pendentes para a Task 3

// ==================== TAMANHO DO TENSOR ARENA (TFLite Micro) ====================
#define TENSOR_ARENA_SIZE   (16 * 1024)   // 4KB causava abort() no AllocateTensors; 16KB é seguro

// ==================== INSTRUMENTAÇÃO DE LATÊNCIA ====================
// Habilita timestamps (esp_timer_get_time) em cada estágio do pipeline e impressão
// de estatísticas de latência via Serial. Desative (0) para reduzir overhead/verbosidade.
#define LATENCY_INSTRUMENTATION_ENABLED   1
// A cada quantas inferências imprimir um resumo (min/max/média) de latência
#define LATENCY_STATS_WINDOW   50

#endif
