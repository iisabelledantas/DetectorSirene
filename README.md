# Detector de Sirene

Sistema embarcado para detecção em tempo real de sirenes (ambulância, polícia, alarme aéreo) a
partir de áudio captado por microfone, rodando em um **ESP32** sobre **FreeRTOS**. O objetivo é
alertar visual e sonoramente (LED + buzzer) quando uma sirene é detectada nas proximidades, com a
opção de um modo silencioso acionado por botão físico.

## Como funciona

O firmware captura áudio continuamente por um microfone I2S (INMP441), extrai um vetor de 12
features (RMS, centroide espectral e 10 coeficientes MFCC) e roda a inferência de um modelo de
classificação binária (sirene vs. não-sirene) via **TensorFlow Lite Micro**, quantizado em `int8`.
Uma detecção só é confirmada após múltiplas janelas consecutivas positivas (debounce), disparando o
alerta (LED + buzzer) — que pode ser silenciado pelo botão de modo silencioso.

O pipeline completo é distribuído em **4 tasks do FreeRTOS** em 2 núcleos do ESP32 (captura →
features → detecção no Core 1, e o modo silencioso isolado no Core 0), comunicando-se por filas e
sincronizado por mutex.

## Estrutura do repositório

```
├── src/
│   ├── DetectorSirene/       # Firmware embarcado (ESP32 / Arduino / FreeRTOS)
│   ├── Modelo/                # Notebook de treino e exportação do modelo (UrbanSound8K)
│   └── TesteAutomatizado/     # Script de teste end-to-end (microfone real + firmware + modelo)
└── docs/                      # Relatório técnico detalhado (treino, arquitetura RTOS, testes)
```

## Stack técnica

- **Hardware:** ESP32, microfone I2S INMP441, LED, buzzer, botão de modo silencioso
- **Firmware:** Arduino-ESP32 / ESP-IDF, FreeRTOS, TensorFlow Lite Micro
- **Modelo:** rede densa treinada sobre o dataset [UrbanSound8K](https://urbansounddataset.weebly.com/urbansound8k.html), quantizada para INT8
- **Validação:** script Python de teste automatizado end-to-end, com métricas de acurácia e latência

## Documentação completa

O relatório técnico detalhado (pipeline de treino do modelo, arquitetura RTOS e análise de
latência, metodologia e resultados do teste automatizado) está publicado no GitHub Pages:

📄 **[iisabelledantas.github.io/DetectorSirene](https://iisabelledantas.github.io/DetectorSirene/)**

## Autoria

Projeto desenvolvido por Isabelle Dantas.
