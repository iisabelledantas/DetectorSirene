#include "audio_capture_task.h"
#if LATENCY_INSTRUMENTATION_ENABLED
#include <esp_timer.h>
#endif

// Buffer deslizante (rolling window) mantido entre iterações para implementar o overlap (hop)
static float rollingBuffer[WINDOW_SIZE];
static bool bufferPrimed = false;   // true assim que o buffer for preenchido pela 1a vez

void i2sInit() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,   // INMP441 com L/R aterrado = canal esquerdo
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

// Lê HOP_SIZE novas amostras do I2S, converte de 24 bits crus para float normalizado [-1, 1]
static void readHopSamples(float *destination) {
    static int32_t rawBuffer[HOP_SIZE];
    size_t bytesRead = 0;

    i2s_read(I2S_PORT, (void *)rawBuffer, HOP_SIZE * sizeof(int32_t), &bytesRead, portMAX_DELAY);

    int samplesRead = bytesRead / sizeof(int32_t);
    for (int i = 0; i < samplesRead; i++) {
        int32_t sample24 = rawBuffer[i] >> I2S_SAMPLE_SHIFT;   // extrai os 24 bits válidos
        destination[i] = sample24 / I2S_NORM_FACTOR;            // normaliza para [-1.0, 1.0]
    }
    // Se por algum motivo leu menos que HOP_SIZE, preenche o resto com silêncio
    for (int i = samplesRead; i < HOP_SIZE; i++) {
        destination[i] = 0.0f;
    }
}

void audioCaptureTaskFn(void *pvParameters) {
    // static: eram variáveis locais de ~2-6KB que estouravam a stack da task (4096 bytes),
    // causando reset silencioso por watchdog (TG1WDT_SYS_RESET) antes de qualquer log aparecer.
    static float hopBuffer[HOP_SIZE];
    static AudioWindow window;

    i2sInit();

    // Preenche o buffer inicial completamente antes da primeira janela
    while (!bufferPrimed) {
        readHopSamples(hopBuffer);
        // Desloca o buffer e insere o novo hop no final
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

        // Overlap: descarta os HOP_SIZE mais antigos, desloca e adiciona os novos no final
        memmove(rollingBuffer, rollingBuffer + HOP_SIZE, (WINDOW_SIZE - HOP_SIZE) * sizeof(float));
        memcpy(rollingBuffer + (WINDOW_SIZE - HOP_SIZE), hopBuffer, HOP_SIZE * sizeof(float));

        memcpy(window.samples, rollingBuffer, WINDOW_SIZE * sizeof(float));
#if LATENCY_INSTRUMENTATION_ENABLED
        window.captureTimestampUs = esp_timer_get_time();   // marca t0 do pipeline para esta janela
#endif

        // Envia para a Task 2. Se a fila estiver cheia, descarta a janela mais antiga
        // (Task 2 está mais lenta que a captura - preferimos perder uma janela a bloquear a captura)
        if (xQueueSend(audioQueue, &window, 0) != pdTRUE) {
            AudioWindow discarded;
            xQueueReceive(audioQueue, &discarded, 0);   // libera espaço
            xQueueSend(audioQueue, &window, 0);
        }

        // Task de alta prioridade cede a CPU brevemente - o próprio i2s_read (bloqueante)
        // já limita a taxa desta task ao ritmo real do hardware, então não é necessário
        // um vTaskDelay adicional aqui.
    }
}
