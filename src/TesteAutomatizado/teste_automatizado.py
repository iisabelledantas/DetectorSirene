#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Script de teste automatizado - Detector de Sirene (ESP32 + FreeRTOS)
=====================================================================

O que ele faz:
  1. Toca, um a um, arquivos de áudio de teste (sirene e não-sirene) pelos
     alto-falantes do computador.
  2. Ao mesmo tempo, lê a porta serial do ESP32 e captura as linhas que o
     firmware imprime (prob=..., consecutive=..., confirmed=... e as linhas
     de latência lat_fim_a_fim=...).
  3. Para cada clipe, decide se o sistema DETECTOU (confirmed=1 dentro da
     janela de tempo do clipe) e se isso está certo (clipe é sirene) ou
     errado (clipe não é sirene -> falso positivo).
  4. No final, calcula acurácia, precisão, recall, falsos positivos/negativos
     e estatísticas de latência de detecção, e salva tudo em CSV + um resumo
     no terminal (fácil de colar no relatório).

Por que isso importa para o relatório:
  A acurácia de 88% medida no Colab é a acurácia do MODELO em cima do
  dataset. Este script mede a acurácia do SISTEMA COMPLETO (microfone real +
  pipeline de features embarcado + modelo quantizado rodando no ESP32),
  que é o que a banca normalmente quer ver como "validação end-to-end".

------------------------------------------------------------------------
COMO USAR
------------------------------------------------------------------------

1) Instale as dependências (uma vez só):

    pip install pyserial sounddevice soundfile numpy

2) Organize seus áudios de teste em duas pastas:

    test_audio/
      sirene/        <- clipes .wav que SÃO sirene (rótulo positivo)
        clipe1.wav
        clipe2.wav
        ...
      nao_sirene/    <- clipes .wav que NÃO são sirene (rótulo negativo)
        clipe1.wav
        ...

   Dica: use um conjunto de áudios que o modelo NÃO viu no treino (ex.: os
   clipes do "holdout" separados no Colab, ou clipes novos baixados à parte).
   Evite reciclar os mesmos áudios usados para treinar/validar o modelo -
   isso infla artificialmente os resultados.

3) Conecte o ESP32 via USB, feche o Monitor Serial do Arduino IDE (a porta
   não pode estar em uso por dois programas ao mesmo tempo) e rode:

    python teste_automatizado.py --port /dev/ttyUSB0 --audio-dir test_audio

   (No Windows a porta é algo como COM5; no Mac, /dev/cu.usbserial-XXXX)

4) Posicione o alto-falante do computador a uma distância razoável do
   microfone INMP441 (a mesma distância/volume que você pretende usar no
   cenário real do projeto) e deixe o script rodar sem mexer no ambiente.

5) Ao final, o script imprime um resumo e salva:
     - resultados_detalhados.csv  (uma linha por clipe testado)
     - resumo_latencia.csv        (estatística agregada de latência)

------------------------------------------------------------------------
PARÂMETROS ÚTEIS
------------------------------------------------------------------------
  --gap N            Segundos de silêncio entre um clipe e o próximo,
                      para o debounce (consecutivePositives) resetar
                      antes do próximo teste. Padrão: 3s.
  --timeout N         Tempo máximo (s) após o clipe começar para considerar
                      uma detecção como "causada por este clipe".
                      Padrão: duração do clipe + 2s.
  --baud N            Baud rate da serial. Padrão: 115200 (bate com o
                      Serial.begin(115200) do firmware).
"""

import argparse
import csv
import glob
import os
import re
import sys
import threading
import time
from dataclasses import dataclass, field

try:
    import serial
except ImportError:
    sys.exit("Faltando dependência: pip install pyserial")

try:
    import sounddevice as sd
    import soundfile as sf
except ImportError:
    sys.exit("Faltando dependência: pip install sounddevice soundfile numpy")


# ------------------------------------------------------------------
# Parsing das linhas que o firmware imprime na serial
# ------------------------------------------------------------------
# Ex.: "prob=0.823 consecutive=3 confirmed=1"
RE_DETECTION = re.compile(
    r"prob=([\-0-9.]+)\s+consecutive=(\d+)\s+confirmed=(\d+)"
)
# Ex.: "lat_captura_features=1.234ms lat_features_deteccao=0.456ms lat_inferencia=2.100ms lat_fim_a_fim=3.790ms"
RE_LATENCY = re.compile(
    r"lat_captura_features=([\-0-9.]+)ms\s+"
    r"lat_features_deteccao=([\-0-9.]+)ms\s+"
    r"lat_inferencia=([\-0-9.]+)ms\s+"
    r"lat_fim_a_fim=([\-0-9.]+)ms"
)


@dataclass
class LogEvent:
    t: float           # timestamp local (time.time()) de quando a linha chegou
    raw: str
    prob: float = None
    consecutive: int = None
    confirmed: bool = None
    lat_captura_features_ms: float = None
    lat_features_deteccao_ms: float = None
    lat_inferencia_ms: float = None
    lat_fim_a_fim_ms: float = None


class SerialReader:
    """Lê a serial continuamente em uma thread separada e guarda tudo
    com timestamp, para que a thread principal possa depois filtrar
    o que aconteceu durante a janela de tempo de cada clipe."""

    def __init__(self, port, baud):
        self.ser = serial.Serial(port, baud, timeout=0.2)
        self.events = []
        self.lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._thread.join(timeout=2)
        self.ser.close()

    def _run(self):
        while not self._stop.is_set():
            try:
                line = self.ser.readline().decode(errors="ignore").strip()
            except Exception:
                continue
            if not line:
                continue
            now = time.time()
            ev = LogEvent(t=now, raw=line)

            m = RE_DETECTION.search(line)
            if m:
                ev.prob = float(m.group(1))
                ev.consecutive = int(m.group(2))
                ev.confirmed = bool(int(m.group(3)))

            m2 = RE_LATENCY.search(line)
            if m2:
                ev.lat_captura_features_ms = float(m2.group(1))
                ev.lat_features_deteccao_ms = float(m2.group(2))
                ev.lat_inferencia_ms = float(m2.group(3))
                ev.lat_fim_a_fim_ms = float(m2.group(4))

            with self.lock:
                self.events.append(ev)

    def events_between(self, t0, t1):
        with self.lock:
            return [e for e in self.events if t0 <= e.t <= t1]


def play_wav_blocking(path):
    data, samplerate = sf.read(path, dtype="float32")
    sd.play(data, samplerate)
    sd.wait()
    return len(data) / samplerate  # duração em segundos


def coletar_clipes(audio_dir):
    clipes = []
    for label, subpasta in (("sirene", "sirene"), ("nao_sirene", "nao_sirene")):
        pasta = os.path.join(audio_dir, subpasta)
        arquivos = sorted(glob.glob(os.path.join(pasta, "*.wav")))
        if not arquivos:
            print(f"[AVISO] Nenhum .wav encontrado em {pasta}")
        for f in arquivos:
            clipes.append((f, label))
    return clipes


def testar_clipe(reader, path, label, gap_s, timeout_extra_s):
    nome = os.path.basename(path)
    print(f"\n--- Tocando: {nome}  (rótulo real: {label}) ---")

    t_inicio = time.time()
    duracao = play_wav_blocking(path)
    t_fim_audio = time.time()

    # Continua monitorando por mais um tempo depois do áudio acabar,
    # pois a detecção (debounce de 3 janelas) leva um tempo para confirmar.
    janela_final = t_fim_audio + timeout_extra_s
    while time.time() < janela_final:
        time.sleep(0.05)

    eventos = reader.events_between(t_inicio, janela_final)
    detec_events = [e for e in eventos if e.confirmed is not None]
    lat_events = [e for e in eventos if e.lat_fim_a_fim_ms is not None]

    detectou = any(e.confirmed for e in detec_events)
    tempo_ate_confirmar = None
    if detectou:
        primeiro_confirmado = next(e for e in detec_events if e.confirmed)
        tempo_ate_confirmar = primeiro_confirmado.t - t_inicio

    prob_max = max((e.prob for e in detec_events if e.prob is not None), default=None)
    lat_media_ms = (
        sum(e.lat_fim_a_fim_ms for e in lat_events) / len(lat_events)
        if lat_events else None
    )

    correto = (detectou and label == "sirene") or (not detectou and label == "nao_sirene")
    tipo = None
    if label == "sirene" and detectou:
        tipo = "TP"  # verdadeiro positivo
    elif label == "sirene" and not detectou:
        tipo = "FN"  # falso negativo
    elif label == "nao_sirene" and detectou:
        tipo = "FP"  # falso positivo
    else:
        tipo = "TN"  # verdadeiro negativo

    print(f"  duração={duracao:.2f}s  detectou={detectou}  prob_max={prob_max}  "
          f"tempo_ate_confirmar={tempo_ate_confirmar}  classificação={tipo}")

    # Pausa de silêncio para o debounce (consecutivePositives) zerar antes do próximo clipe
    time.sleep(gap_s)

    return {
        "arquivo": nome,
        "rotulo_real": label,
        "detectou": detectou,
        "classificacao": tipo,
        "correto": correto,
        "duracao_s": round(duracao, 3),
        "prob_max": prob_max,
        "tempo_ate_confirmar_s": round(tempo_ate_confirmar, 3) if tempo_ate_confirmar else None,
        "latencia_media_fim_a_fim_ms": round(lat_media_ms, 3) if lat_media_ms else None,
        "n_amostras_latencia": len(lat_events),
    }


def imprimir_resumo(resultados):
    n = len(resultados)
    tp = sum(1 for r in resultados if r["classificacao"] == "TP")
    fn = sum(1 for r in resultados if r["classificacao"] == "FN")
    fp = sum(1 for r in resultados if r["classificacao"] == "FP")
    tn = sum(1 for r in resultados if r["classificacao"] == "TN")

    acuracia = (tp + tn) / n if n else 0
    precisao = tp / (tp + fp) if (tp + fp) else float("nan")
    recall = tp / (tp + fn) if (tp + fn) else float("nan")

    latencias = [r["latencia_media_fim_a_fim_ms"] for r in resultados
                 if r["latencia_media_fim_a_fim_ms"] is not None]
    tempos_deteccao = [r["tempo_ate_confirmar_s"] for r in resultados
                        if r["tempo_ate_confirmar_s"] is not None]

    print("\n" + "=" * 60)
    print("RESUMO DO TESTE AUTOMATIZADO")
    print("=" * 60)
    print(f"Total de clipes testados: {n}")
    print(f"  Verdadeiros positivos (TP): {tp}")
    print(f"  Falsos negativos      (FN): {fn}")
    print(f"  Falsos positivos      (FP): {fp}")
    print(f"  Verdadeiros negativos (TN): {tn}")
    print(f"Acurácia end-to-end: {acuracia*100:.1f}%")
    print(f"Precisão: {precisao*100:.1f}%" if precisao == precisao else "Precisão: N/A")
    print(f"Recall:   {recall*100:.1f}%" if recall == recall else "Recall: N/A")
    if tempos_deteccao:
        print(f"Tempo médio até confirmar detecção: "
              f"{sum(tempos_deteccao)/len(tempos_deteccao):.2f}s "
              f"(min={min(tempos_deteccao):.2f}s, max={max(tempos_deteccao):.2f}s)")
    if latencias:
        print(f"Latência média fim-a-fim do pipeline (por janela de 64ms): "
              f"{sum(latencias)/len(latencias):.2f}ms "
              f"(min={min(latencias):.2f}ms, max={max(latencias):.2f}ms)")
    print("=" * 60)

    return {
        "n": n, "tp": tp, "fn": fn, "fp": fp, "tn": tn,
        "acuracia": acuracia, "precisao": precisao, "recall": recall,
    }


def salvar_csv(resultados, resumo, out_detalhado, out_resumo):
    with open(out_detalhado, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(resultados[0].keys()))
        writer.writeheader()
        writer.writerows(resultados)
    print(f"\nResultados detalhados salvos em: {out_detalhado}")

    with open(out_resumo, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(resumo.keys()))
        writer.writeheader()
        writer.writerow(resumo)
    print(f"Resumo agregado salvo em: {out_resumo}")


def main():
    parser = argparse.ArgumentParser(description="Teste automatizado do Detector de Sirene")
    parser.add_argument("--port", required=True, help="Porta serial do ESP32 (ex: /dev/ttyUSB0, COM5)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--audio-dir", default="test_audio",
                         help="Pasta com subpastas sirene/ e nao_sirene/")
    parser.add_argument("--gap", type=float, default=3.0,
                         help="Segundos de silêncio entre clipes (para resetar debounce)")
    parser.add_argument("--timeout-extra", type=float, default=2.0,
                         help="Segundos extras de monitoramento após o fim do áudio")
    parser.add_argument("--out-detalhado", default="resultados_detalhados.csv")
    parser.add_argument("--out-resumo", default="resumo_latencia.csv")
    args = parser.parse_args()

    clipes = coletar_clipes(args.audio_dir)
    if not clipes:
        sys.exit(f"Nenhum arquivo .wav encontrado em {args.audio_dir}/sirene ou {args.audio_dir}/nao_sirene")

    print(f"{len(clipes)} clipes encontrados. Abrindo serial em {args.port} @ {args.baud}...")
    reader = SerialReader(args.port, args.baud)
    reader.start()

    # Dá um tempo para o ESP32 estabilizar / imprimir os logs de boot
    time.sleep(2)

    resultados = []
    try:
        for path, label in clipes:
            r = testar_clipe(reader, path, label, args.gap, args.timeout_extra)
            resultados.append(r)
    finally:
        reader.stop()

    resumo = imprimir_resumo(resultados)
    salvar_csv(resultados, resumo, args.out_detalhado, args.out_resumo)


if __name__ == "__main__":
    main()
