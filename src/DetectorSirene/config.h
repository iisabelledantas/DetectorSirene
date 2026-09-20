#ifndef CONFIG_H
#define CONFIG_H

// ==================== PINOS ====================
#define I2S_WS_PIN    25   
#define I2S_SCK_PIN   26   
#define I2S_SD_PIN    22   
#define I2S_PORT      I2S_NUM_0

#define LED_PIN       2
#define BUZZER_PIN    4
#define SILENT_BUTTON_PIN 15

// ==================== PARÂMETROS DE ÁUDIO ====================
#define SAMPLE_RATE   16000
#define WINDOW_SIZE   1024     
#define HOP_SIZE      512       
#define N_MFCC        10
#define N_FFT         1024
#define N_FEATURES    (2 + N_MFCC)  

#define I2S_SAMPLE_SHIFT   8
#define I2S_NORM_FACTOR    8388608.0f   // 2^23

// ==================== DETECÇÃO / DEBOUNCE ====================
#define DETECTION_THRESHOLD       0.95f   
#define CONFIRM_WINDOWS_COUNT     10      
#define SUSPECT_WINDOWS_COUNT     5      

// ==================== BUFFERS / FILAS ====================
#define AUDIO_QUEUE_LENGTH     4    
#define FEATURE_QUEUE_LENGTH   4    

// ==================== TAMANHO DO TENSOR ARENA (TFLite Micro) ====================
#define TENSOR_ARENA_SIZE   (16 * 1024)  

// ==================== INSTRUMENTAÇÃO DE LATÊNCIA ====================
#define LATENCY_INSTRUMENTATION_ENABLED   1
#define LATENCY_STATS_WINDOW   50

#endif
