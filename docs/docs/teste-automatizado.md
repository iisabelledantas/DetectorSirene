---
id: teste-automatizado
title: Teste Automatizado e Validação End-to-End
sidebar_label: Teste Automatizado
sidebar_position: 3
description: Metodologia e resultados do script de teste automatizado que valida o sistema físico completo (microfone real + firmware + modelo quantizado).
---

## Motivação

A acurácia de 88% reportada na [etapa de treinamento](./treino-modelo.md) mede o desempenho do
**modelo** contra o dataset em Python — ela não captura erros de captura de áudio, ruído do
microfone físico, ou divergências entre a extração de features em Python e em C. Para validar o
**sistema completo** (microfone real → pipeline embarcado → modelo quantizado rodando no ESP32),
foi construído um script de teste automatizado.

## Metodologia

O script (`teste_automatizado.py`) roda no computador, não no ESP32, e:

1. Toca, um a um, arquivos de áudio de teste rotulados como `sirene` ou `nao_sirene` pelos
   alto-falantes do computador, posicionados a uma distância fixa do microfone INMP441.
2. Em paralelo, lê a porta serial do ESP32 em uma thread separada, capturando as linhas
   `prob=... confirmed=...` e as linhas de latência do firmware, cada uma com timestamp local.
3. Para cada clipe, verifica se houve uma confirmação de alerta (`confirmed=1`) dentro da janela de
   tempo daquele áudio (duração do clipe + margem de 2s para o debounce de 3 janelas confirmar).
4. Classifica o resultado como verdadeiro positivo (TP), falso negativo (FN), falso positivo (FP)
   ou verdadeiro negativo (TN), e agrega acurácia, precisão e recall end-to-end.
5. Insere uma pausa de silêncio (3s) entre clipes para o contador de debounce
   (`consecutivePositives`) resetar antes do próximo teste.

Os clipes de teste usados vieram de bancos de efeitos sonoros gratuitos (sirenes de ambulância,
polícia e alarme aéreo; sons urbanos como trânsito, música ambiente e ruído de rua) — não são os
mesmos áudios usados no treinamento do modelo.

## Resultados

Execução com 10 clipes (5 de sirene, 5 de não-sirene), já com o firmware instrumentado
(seção [Instrumentação de latência](./arquitetura-rtos.md#instrumentação-de-latência)):

| Métrica | Valor |
|---|---|
| Acurácia end-to-end | **70.0%** |
| Precisão | 66.7% |
| Recall | 80.0% |
| Verdadeiros positivos (TP) | 4 |
| Falsos negativos (FN) | 1 |
| Falsos positivos (FP) | 2 |
| Verdadeiros negativos (TN) | 3 |
| Tempo médio até confirmar detecção | 3.02s (mín. 0.46s, máx. 8.89s) |
| Latência média fim-a-fim do pipeline | **4.93 ms** (praticamente constante) |

### Detalhamento por clipe

| Arquivo | Rótulo real | Detectou? | Classificação | Prob. máxima | Latência méd. fim-a-fim |
|---|---|---|---|---|---|
| air-raid-siren-sound-effect | sirene | Sim | TP | 0.984 | 4.93 ms |
| police-siren-sound-effect | sirene | Sim | TP | 0.996 | 4.93 ms |
| medical-ambulance-siren | sirene | **Não** | **FN** | 0.965 | 4.93 ms |
| police-siren (cinematic) | sirene | Sim | TP | 0.996 | 4.93 ms |
| police-siren-cinematic-hd | sirene | Sim | TP | 0.996 | 4.93 ms |
| car-street-noise | não-sirene | Sim | **FP** | 0.973 | 4.93 ms |
| sunflower-street-drumloop | não-sirene | Não | TN | 0.883 | 4.93 ms |
| street-ambience | não-sirene | Sim | **FP** | 0.969 | 4.93 ms |
| street-music-cafe-atmo | não-sirene | Não | TN | 0.984 | 4.93 ms |
| street-ambience-traffic | não-sirene | Não | TN | 0.953 | 4.93 ms |

## Análise

### Comparação com a primeira rodada (antes da instrumentação de latência)

Este projeto rodou o teste automatizado **duas vezes**, em sessões diferentes, com exatamente os
mesmos 10 áudios: uma vez com o firmware anterior (recall 100%, precisão 71.4%) e esta, com o
firmware já instrumentado (recall 80.0%, precisão 66.7%). A diferença está concentrada em um único
clipe que trocou de TP para FN (`medical-ambulance-siren`) — **a probabilidade máxima nesse clipe
continuou alta (0.965)**, ou seja, o modelo detectou o padrão de sirene momentaneamente, mas a
janela de 3 detecções consecutivas exigida pelo debounce (`CONFIRM_WINDOWS_COUNT`) não chegou a se
sustentar dentro do tempo do clipe nesta rodada.

Isso não é um efeito da instrumentação de latência em si (que não altera o cálculo de features nem
o modelo) — é uma variação esperada entre execuções reais em ambiente acústico não controlado:
pequenas diferenças de posicionamento do alto-falante, volume ou ruído de fundo entre uma sessão de
teste e outra podem empurrar um clipe "de fronteira" (borderline) para o outro lado do limiar de
confirmação. É um resultado honesto de se reportar: com apenas 10 clipes e uma única repetição por
rodada, o intervalo de confiança da acurácia é largo, e o próprio ROC-AUC de 0.947 obtido no
treinamento (veja [Treinamento do Modelo](./treino-modelo.md)) já antecipava que casos limítrofes
existem. Uma limitação documentada para trabalhos futuros é repetir este teste automatizado
múltiplas vezes por clipe e reportar médias com intervalo de variação, em vez de uma única
passada.

### Padrão nos falsos positivos

Os 2 falsos positivos, em ambas as rodadas, seguem o mesmo padrão: ocorrem nos clipes de
**ruído urbano contínuo com energia espectral sustentada** (ruído de motor/trânsito,
`street-ambience`/`car-street-noise`). Esse tipo de áudio produz um espectro relativamente estável
ao longo do tempo, com componentes tonais que podem se assemelhar, em janelas curtas de ~64ms, ao
padrão espectral de uma sirene — especialmente sem um mecanismo de contexto temporal mais longo (o
classificador decide janela a janela, e o debounce de 3 janelas consecutivas positivas filtra picos
pontuais, mas não um som que é consistentemente ambíguo ao longo de dezenas de segundos).

### Trade-off recall × precisão

Este é o trade-off central do sistema: a escolha de `DETECTION_THRESHOLD = 0.5` e
`CONFIRM_WINDOWS_COUNT = 3` prioriza sensibilidade (nunca perder uma sirene real) ao custo de uma
taxa de falso positivo maior em ruído urbano sustentado. Ajustes possíveis para trabalhos futuros:

- Aumentar `DETECTION_THRESHOLD` (ex.: 0.6–0.7) para exigir maior confiança antes de confirmar.
- Aumentar `CONFIRM_WINDOWS_COUNT` para exigir um padrão mais sustentado no tempo.
- Adicionar uma feature de variação temporal (ex.: variância do centroide espectral ao longo de
  múltiplas janelas), já que sirenes reais tipicamente têm um padrão de frequência que varia de
  forma característica (sweep), diferente de ruído de motor ou música de fundo mais estacionários.

Qualquer um desses ajustes desloca o ponto de operação na curva ROC — o `ROC-AUC = 0.947` obtido no
treinamento (veja [Treinamento do Modelo](./treino-modelo.md)) indica que existe margem para
recalibrar o limiar sem comprometer significativamente o recall.

## Latência medida

Com o firmware instrumentado, o script capturou **10.562 janelas de áudio processadas** ao longo
dos 10 clipes, cada uma com sua latência fim-a-fim medida via `esp_timer_get_time()`:

| Métrica | Valor |
|---|---|
| Latência média fim-a-fim | **4.93 ms** |
| Variação entre clipes | 4.930 ms – 4.932 ms (praticamente nula) |
| Janelas medidas (total) | 10.562 |

Para interpretar esse número: uma nova janela de áudio fica pronta a cada **32 ms**
(`HOP_SIZE / SAMPLE_RATE` = 512 / 16000 s), já que o hop define a cadência de produção do pipeline.
Uma latência fim-a-fim de ~4.93 ms significa que o sistema processa cada janela (extração de
features + inferência TFLite Micro + decisão) usando **cerca de 15% do orçamento de tempo
disponível entre duas janelas**, com folga confortável para não acumular atraso — a fila
(`featureQueue`) não deveria nunca chegar a encher em operação normal, o que é consistente com o
critério de "detecção em tempo real" da rubrica.

A variação entre clipes é essencialmente nula (4.930–4.932 ms), o que indica um tempo de
processamento **determinístico** — esperado, já que o volume de trabalho por janela (tamanho da
FFT, número de filtros mel, tamanho do tensor) é fixo e não depende do conteúdo do áudio.

O script atual agrega apenas a latência fim-a-fim por clipe; o firmware também expõe a quebra por
estágio (`lat_captura_features`, `lat_features_deteccao`, `lat_inferencia`) diretamente no log
serial a cada janela e um resumo min/média/máx a cada 50 janelas — útil para uma inspeção manual
mais fina, caso o relatório precise detalhar quanto tempo cabe a cada etapa individualmente.
