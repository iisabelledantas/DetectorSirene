---
id: treino-modelo
slug: /
title: Treinamento do Modelo e Análise de Resultados
sidebar_label: Treino do Modelo
sidebar_position: 1
description: Pipeline de treinamento do classificador binário de sirenes (UrbanSound8K + TensorFlow Lite Micro), métricas obtidas e o case study da correção do cálculo de MFCC.
---

## Visão geral

O detector usa um classificador binário (**sirene** vs. **não-sirene**) treinado sobre o dataset
[UrbanSound8K](https://urbansounddataset.weebly.com/urbansound8k.html), quantizado para `int8` e
embarcado via **TensorFlow Lite Micro** em um ESP32. Este documento cobre o pipeline de treino, os
resultados obtidos e um estudo de caso sobre o bug mais relevante encontrado no projeto — um erro
sutil no cálculo dos coeficientes MFCC que impedia a detecção correta em áudio real.

## Pipeline de dados

1. **Download do dataset.** A tentativa inicial via `soundata` (que baixa do Zenodo) falhou
   repetidamente com erro `504 Gateway Timeout`. O pipeline foi adaptado para baixar o dataset via
   `kagglehub` (`chrisfilo/urbansound8k`), com uma classe `Clip` própria replicando a interface que
   o `soundata` oferecia, lendo `UrbanSound8K.csv` diretamente para mapear cada arquivo ao seu fold
   e classe.
2. **Rotulagem binária.** A classe `siren` (classID 8) do UrbanSound8K vira o rótulo positivo；
   todas as outras 9 classes (buzina, cachorro latindo, perfuratriz, música de rua etc.) formam o
   conjunto negativo.
3. **Split por fold.** O UrbanSound8K é organizado em 10 folds pré-definidos, desenhados
   especificamente para evitar vazamento entre clipes da mesma gravação de origem. O split
   treino/teste respeita essa estrutura de fold em vez de uma divisão aleatória simples.

## Extração de features

Cada janela de áudio gera um vetor de **12 features**, na mesma ordem usada no firmware:

| # | Feature | Descrição |
|---|---|---|
| 0 | RMS | energia da janela |
| 1 | Spectral centroid | "centro de massa" do espectro (Hz) |
| 2–11 | MFCC 0–9 | 10 coeficientes cepstrais (mel-frequency) |

As features são extraídas com `librosa` no Colab (para o treino) e recalculadas em C no firmware
(para a inferência em tempo real) — **a fidelidade entre as duas implementações é o ponto mais
crítico do projeto** e é o assunto do estudo de caso abaixo.

## Modelo e treinamento

- **Arquitetura:** rede densa `12 → 16 → 8 → 1`, ativações ReLU, `Dropout` para regularização,
  saída `sigmoid`.
- **Treinamento:** `EarlyStopping` monitorando a loss de validação.
- **Validação cruzada:** 10-fold cross-validation (respeitando os folds nativos do UrbanSound8K),
  para obter uma estimativa robusta de acurácia e não depender de um único split, que no início do
  projeto mostrou grande variância (89% em validação vs. 65% em teste em uma tentativa inicial de
  split único — resolvido metodologicamente ao migrar para 10-fold CV).
- **Modelo final:** treinado sobre os folds combinados, com um holdout separado para avaliação
  final e para calibração da quantização.

### Resultados

| Métrica | Valor |
|---|---|
| Acurácia (10-fold CV) | 82.2% ± 7.2% |
| Acurácia (modelo final, holdout) | 88% |
| Recall (modelo final, holdout) | 88% |
| ROC-AUC (modelo final, holdout) | 0.947 |

O desvio padrão de ±7.2% entre folds é esperado e documentado: cada fold do UrbanSound8K vem de
gravações de origem diferentes (ruído de fundo, distância da fonte, equipamento de gravação), então
alguma variância entre folds é sinal de rigor metodológico, não de instabilidade do modelo.

## Quantização (INT8)

O modelo foi exportado para ONNX (contornando uma incompatibilidade do Keras 3 com `tf2onnx` via um
wrapper `tf.function` com `input_signature` explícito) e então convertido para TFLite com
quantização full-integer pós-treino, usando um dataset representativo para calibração.

| Métrica | Float32 | INT8 quantizado |
|---|---|---|
| Acurácia (holdout) | 87.17% | 87.09% |
| Tamanho do modelo | — | 3.432 KB |

A perda de acurácia da quantização é desprezível (0.08 p.p.), o que valida a escolha de INT8 para
rodar dentro do orçamento de memória do ESP32.

Também foram exportados como arrays C:
- `modelo_sirene_tflite[]` — o modelo quantizado
- `mel_filterbank.h` — matriz do banco de filtros mel, 26×513
- `dct_matrix.h` — matriz da DCT-II, 10×26
- `normalizacao_params.h` — `feature_mean`, `feature_std` (z-score) e os parâmetros de
  quantização (`INPUT_SCALE`, `INPUT_ZERO_POINT`, `OUTPUT_SCALE`, `OUTPUT_ZERO_POINT`)

## Estudo de caso: o bug do cálculo de MFCC

### Sintoma

Com o pipeline completo rodando no ESP32 (captura → features → inferência → alerta), a
probabilidade de saída do modelo (`prob`) ficava presa perto de zero (`0.000`–`0.012`)
**mesmo com uma sirene real tocando perto do microfone**, apesar de o modelo relatar 88% de
acurácia em Python.

### Descartando hipóteses

1. **Hipótese: problema de microfone/fiação.** Descartada — um estalo de mão durante o teste
   produziu RMS de até 0.23, provando que o INMP441 capturava som transiente normalmente.
2. **Hipótese: limitação de volume do alto-falante do celular.** Parcialmente confirmada, mas
   insuficiente como explicação completa — trocar para caixas de som de PC produziu bem mais
   variação de RMS (0.003–0.038), e ainda assim `prob` continuou perto de zero.
3. **Causa raiz confirmada:** um vetor de features real, extraído em Python a partir de um clipe de
   treino, foi calculado manualmente e comparado byte a byte com a saída do firmware C — isso
   isolou definitivamente o problema no cálculo de features, não na captura de áudio.

### A causa raiz

A função `computeMFCC()` do firmware usava:

```cpp
// ERRADO
energy += MEL_FILTERBANK[m][k] * magnitude[k];       // magnitude linear, não potência
melEnergies[m] = logf(energy + 1e-6f);                // log natural, não log10
```

enquanto `librosa.feature.mfcc()` (usado no treino em Python) internamente:
1. eleva o espectro de magnitude ao **quadrado** (espectro de potência) antes de aplicar o banco
   de filtros mel;
2. aplica `10 * log10(x)` (convenção `power_to_db`), não logaritmo natural.

A correção:

```cpp
// CORRIGIDO
energy += MEL_FILTERBANK[m][k] * (magnitude[k] * magnitude[k]);  // espectro de potência
melEnergies[m] = 10.0f * log10f(energy + 1e-6f);                  // power_to_db
```

### Por que o efeito era tão sutil

A combinação dos dois erros produzia um fator de compressão sistemático de aproximadamente
**8.69× (= 2 × 10/ln(10))** em todas as features derivadas de MFCC. Isso por si só já bastaria para
descalibrar a entrada do modelo (que espera valores normalizados por z-score calculado sobre a
escala correta) — mas o sintoma tinha uma peculiaridade que ajudou a confundir o diagnóstico:
**MFCC0 "reagia" um pouco ao som, enquanto MFCC1 parecia completamente travado.**

A explicação está na estrutura da matriz DCT: a primeira linha da DCT-II (usada para calcular
MFCC0) é constante e soma um valor não-nulo — ela captura principalmente o **nível médio** de
energia, então um erro multiplicativo de escala ainda deixa alguma variação visível. Já a segunda
linha (MFCC1) é uma função oscilante que **soma zero** — ela só reflete *diferenças relativas* entre
bandas de frequência, o que é justamente o tipo de informação que um erro multiplicativo uniforme
apaga quase por completo. Isso foi confirmado recalculando manualmente, em Python, o produto entre
`melEnergies` e a linha 1 da matriz DCT, reproduzindo exatamente os valores que o firmware imprimia
— o que provou que a matemática da DCT em si estava correta, e que o erro estava a montante, no
cálculo da energia mel.

### Lição para o relatório

Esse bug ilustra um risco específico de pipelines de TinyML: **a fidelidade entre a implementação
de features em Python (treino) e em C/C++ (inferência embarcada) não é garantida por bibliotecas
diferentes lerem "a mesma fórmula"** — pequenas escolhas de convenção (magnitude vs. potência,
log natural vs. log10) produzem modelos que compilam, rodam e não travam, mas nunca vão classificar
corretamente, porque a distribuição da entrada real nunca bate com a distribuição vista no treino.
A técnica de depuração mais eficaz foi comparar, numericamente e passo a passo, a saída de cada
estágio do pipeline em C contra o mesmo cálculo feito manualmente em Python a partir de dados de
treino conhecidos — isolando se o problema estava na captura de áudio ou no cálculo de features
antes de sequer olhar para a inferência do modelo em si.
