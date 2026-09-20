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

Execução com 10 clipes (5 de sirene, 5 de não-sirene):

| Métrica | Valor |
|---|---|
| Acurácia end-to-end | **80.0%** |
| Precisão | 71.4% |
| Recall | **100%** |
| Verdadeiros positivos (TP) | 5 |
| Falsos negativos (FN) | 0 |
| Falsos positivos (FP) | 2 |
| Verdadeiros negativos (TN) | 3 |
| Tempo médio até confirmar detecção | 2.15s (mín. 0.27s, máx. 6.98s) |

### Detalhamento por clipe

| Arquivo | Rótulo real | Detectou? | Classificação | Prob. máxima |
|---|---|---|---|---|
| air-raid-siren-sound-effect | sirene | Sim | TP | 0.977 |
| police-siren-sound-effect | sirene | Sim | TP | 0.996 |
| medical-ambulance-siren | sirene | Sim | TP | 0.930 |
| police-siren (cinematic) | sirene | Sim | TP | 0.996 |
| police-siren-cinematic-hd | sirene | Sim | TP | 0.996 |
| car-street-noise | não-sirene | Sim | **FP** | 0.961 |
| sunflower-street-drumloop | não-sirene | Não | TN | 0.855 |
| street-ambience | não-sirene | Não | TN | 0.926 |
| street-music-cafe-atmo | não-sirene | Sim | **FP** | 0.949 |
| street-ambience-traffic | não-sirene | Não | TN | 0.836 |

## Análise

**Recall de 100% é o resultado mais importante para um sistema de alerta de segurança**: nenhuma
sirene real deixou de ser detectada nos testes realizados. Isso é o comportamento desejado quando o
custo de um falso negativo (não alertar sobre uma sirene real) é maior que o custo de um falso
positivo (alertar à toa).

Os 2 falsos positivos seguem um padrão reconhecível: ambos ocorreram nos clipes **mais longos e com
energia espectral sustentada** (ruído de motor/trânsito contínuo, 51.8s; música ambiente de fundo,
45.5s). Esse tipo de áudio produz um espectro relativamente estável ao longo do tempo, com
componentes tonais que podem se assemelhar, em janelas curtas de ~64ms, ao padrão espectral de uma
sirene — especialmente sem um mecanismo de contexto temporal mais longo (o classificador decide
janela a janela, e o debounce de 3 janelas consecutivas positivas não é suficiente para filtrar um
som que é consistentemente ambíguo, apenas para filtrar picos pontuais).

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

## Latência (pendente)

O firmware usado nesta rodada de testes ainda não incluía a
[instrumentação de latência](./arquitetura-rtos.md#instrumentação-de-latência) — as colunas de
latência do CSV de saída ficaram vazias. Após reflashar o firmware atualizado, repetir este mesmo
teste preenche a lacuna, sem necessidade de novos áudios.

:::info Números pendentes
| Métrica | Valor |
|---|---|
| Latência média fim-a-fim (ms) | — |
| Latência de inferência pura (ms) | — |
:::

## Reprodutibilidade

O script, o `README.md` com instruções de uso e os CSVs de saída
(`resultados_detalhados.csv`, `resumo_latencia.csv`) desta execução ficam junto ao código-fonte do
projeto, permitindo repetir o teste a qualquer momento com o mesmo conjunto de áudios ou com um
conjunto expandido.
