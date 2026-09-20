#include "audio_capture_task.h"
#if LATENCY_INSTRUMENTATION_ENABLED
#include <esp_timer.h>
#endif

static float rollingBuffer[WINDOW_SIZE];
static bool bufferPrimed = false;   

void i2sInit() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,   
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = HOP_SIZE,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD_PIN
    };

    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
}

static void readHopSamples(float *destination) {
    static int32_t rawBuffer[HOP_SIZE];
    size_t bytesRead = 0;

    i2s_read(I2S_PORT, (void *)rawBuffer, HOP_SIZE * sizeof(int32_t), &bytesRead, portMAX_DELAY);

    int samplesRead = bytesRead / sizeof(int32_t);
    for (int i = 0; i < samplesRead; i++) {
        int32_t sample24 = rawBuffer[i] >> I2S_SAMPLE_SHIFT;   
        destination[i] = sample24 / I2S_NORM_FACTOR;            
    }
    for (int i = samplesRead; i < HOP_SIZE; i++) {
        destination[i] = 0.0f;
    }
}

void audioCaptureTaskFn(void *pvParameters) {
    static float hopBuffer[HOP_SIZE];
    static AudioWindow window;

    i2sInit();

    while (!bufferPrimed) {
        readHopSamples(hopBuffer);
        memmove(rollingBuffer, rollingBuffer + HOP_SIZE, (WINDOW_SIZE - HOP_SIZE) * sizeof(float));
        memcpy(rollingBuffer + (WINDOW_SIZE - HOP_SIZE), hopBuffer, HOP_SIZE * sizeof(float));
        static int primeCount = 0;
        primeCount += HOP_SIZE;
        if (primeCount >= WINDOW_SIZE) {
            bufferPrimed = true;
        }
    }

    for (;;) {
        readHopSamples(hopBuffer);
        memmove(rollingBuffer, rollingBuffer + HOP_SIZE, (WINDOW_SIZE - HOP_SIZE) * sizeof(float));
        memcpy(rollingBuffer + (WINDOW_SIZE - HOP_SIZE), hopBuffer, HOP_SIZE * sizeof(float));

        memcpy(window.samples, rollingBuffer, WINDOW_SIZE * sizeof(float));
#if LATENCY_INSTRUMENTATION_ENABLED
        window.captureTimestampUs = esp_timer_get_time();   
#endif
        if (xQueueSend(audioQueue, &window, 0) != pdTRUE) {
            AudioWindow discarded;
            xQueueReceive(audioQueue, &discarded, 0);   // libera espaço
            xQueueSend(audioQueue, &window, 0);
        }
    }
}
