#include "feature_extraction_task.h"
#include <arduinoFFT.h>       
#include "mel_filterbank.h"   
#include "dct_matrix.h"       
#if LATENCY_INSTRUMENTATION_ENABLED
#include <esp_timer.h>
#endif

#define N_FFT_BINS (N_FFT / 2 + 1)   

static float fftReal[N_FFT];
static float fftImag[N_FFT];
static float hannWindow[N_FFT];
static bool hannInitialized = false;

static void initHannWindow() {
    for (int i = 0; i < N_FFT; i++) {
        hannWindow[i] = 0.5f * (1.0f - cosf(2.0f * PI * i / (N_FFT - 1)));
    }
    hannInitialized = true;
}

static float computeRMS(const float *samples, int n) {
    float sumSquares = 0.0f;
    for (int i = 0; i < n; i++) {
        sumSquares += samples[i] * samples[i];
    }
    return sqrtf(sumSquares / n);
}

static void computeMagnitudeSpectrum(const float *samples, float *magnitudeOut) {
    if (!hannInitialized) initHannWindow();

    for (int i = 0; i < N_FFT; i++) {
        fftReal[i] = samples[i] * hannWindow[i];
        fftImag[i] = 0.0f;
    }

    ArduinoFFT<float> FFT(fftReal, fftImag, N_FFT, (float)SAMPLE_RATE);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    for (int i = 0; i < N_FFT_BINS; i++) {
        magnitudeOut[i] = fftReal[i]; 
    }
}

static float computeSpectralCentroid(const float *magnitude) {
    float weightedSum = 0.0f;
    float magnitudeSum = 0.0f;
    for (int k = 0; k < N_FFT_BINS; k++) {
        float freq = (float)k * SAMPLE_RATE / N_FFT;
        weightedSum += freq * magnitude[k];
        magnitudeSum += magnitude[k];
    }
    if (magnitudeSum < 1e-8f) return 0.0f;
    return weightedSum / magnitudeSum;
}

static void computeMFCC(const float *magnitude, float *mfccOut) {
    static float melEnergies[MEL_FILTERBANK_ROWS];  

    for (int m = 0; m < MEL_FILTERBANK_ROWS; m++) {
        float energy = 0.0f;
        for (int k = 0; k < N_FFT_BINS; k++) {
            energy += MEL_FILTERBANK[m][k] * (magnitude[k] * magnitude[k]);
        }
        melEnergies[m] = 10.0f * log10f(energy + 1e-6f);
    }

    for (int c = 0; c < N_MFCC; c++) {
        float sum = 0.0f;
        for (int m = 0; m < DCT_MATRIX_COLS; m++) {
            sum += DCT_MATRIX[c][m] * melEnergies[m];
        }
        mfccOut[c] = sum;
    }
}

void featureExtractionTaskFn(void *pvParameters) {
    AudioWindow window;
    static float magnitude[N_FFT_BINS];
    static float mfcc[N_MFCC];

    for (;;) {
        if (xQueueReceive(audioQueue, &window, portMAX_DELAY) == pdTRUE) {

            float rms = computeRMS(window.samples, WINDOW_SIZE);
            computeMagnitudeSpectrum(window.samples, magnitude);
            float centroid = computeSpectralCentroid(magnitude);
            computeMFCC(magnitude, mfcc);

            FeatureVector fv;
            fv.values[0] = rms;
            fv.values[1] = centroid;
            for (int i = 0; i < N_MFCC; i++) {
                fv.values[2 + i] = mfcc[i];
            }
#if LATENCY_INSTRUMENTATION_ENABLED
            fv.captureTimestampUs = window.captureTimestampUs;
            fv.featureTimestampUs = esp_timer_get_time();
#endif
            if (xQueueSend(featureQueue, &fv, 0) != pdTRUE) {
                FeatureVector discarded;
                xQueueReceive(featureQueue, &discarded, 0);
                xQueueSend(featureQueue, &fv, 0);
            }
        }
    }
}
