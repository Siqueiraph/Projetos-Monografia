#!/usr/bin/env python3
"""
Pré-visualização da página do Anemômetro Digital sem gravar o ESP32.

Serve Anemometro_Digital/pagina_html.h (relido a cada requisição: salve e dê F5)
e simula os endpoints do firmware com um rotor virtual. Testa só a interface:
sensor, interrupção, debounce e persistência na flash só se validam no hardware.

Uso (só biblioteca padrão, Python 3.8+):
    python ferramentas/preview_anemometro.py [--porta 8000] [--vento 30] [--rede]

    --vento W   velocidade angular média do rotor virtual, em rad/s (padrão 30)
    --rede      aceita conexões de outros aparelhos (celular na mesma rede)

Rota só do simulador: /_vento?w=W muda a velocidade angular média
(w=0 para o rotor e testa o zeramento após TIMEOUT_PARADO_MS).

Ao mudar endpoints, chaves de JSON ou o cálculo no firmware, atualize este
arquivo junto. Na inicialização e a cada recarga da página ele avisa no
terminal se alguma rota, id ou chave de JSON usada pela página não existir.
"""

import argparse
import json
import math
import random
import re
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

RAIZ = Path(__file__).resolve().parent.parent
ARQ_HTML = RAIZ / "Anemometro_Digital" / "pagina_html.h"
ARQ_INO = RAIZ / "Anemometro_Digital" / "Anemometro_Digital.ino"

ROTAS = {"/", "/status", "/dados", "/set", "/save"}


# ---------------------------------------------------------------------------
# Leitura do firmware
# ---------------------------------------------------------------------------

def ler_html():
    texto = ARQ_HTML.read_text(encoding="utf-8")
    m = re.search(r'R"rawliteral\((.*)\)rawliteral"', texto, re.S)
    if not m:
        raise ValueError('não achei R"rawliteral(...)rawliteral" em ' + ARQ_HTML.name)
    return m.group(1)


def ler_firmware():
    """Constantes e valores padrão lidos do .ino, para não divergirem do firmware."""
    c = {
        "ID_ANEMOMETRO": "A", "JANELA_MEDIA_MS": 1000, "TIMEOUT_PARADO_MS": 3000, "DEBOUNCE_US": 15000,
        "raio": 10, "imas": 1, "fator": 2.5,
    }
    try:
        texto = ARQ_INO.read_text(encoding="utf-8")
    except OSError:
        print("Aviso: não consegui ler o .ino; usando valores padrão.")
        return c
    m = re.search(r"ID_ANEMOMETRO\s*=\s*'(.)'", texto)
    if m:
        c["ID_ANEMOMETRO"] = m.group(1)
    for nome, valor in re.findall(r"const\s+unsigned\s+long\s+(\w+)\s*=\s*(\d+)", texto):
        c[nome] = int(valor)
    # Padrões da flash: preferences.getInt("raio", 10), getFloat("fator", 2.5)
    for chave, valor in re.findall(r'preferences\.get\w+\("(\w+)",\s*([0-9.]+)\)', texto):
        c[chave] = float(valor) if "." in valor else int(valor)
    return c


C = ler_firmware()


# ---------------------------------------------------------------------------
# Calibração (espelha as variáveis globais, /set e /status)
# ---------------------------------------------------------------------------

P = {"raio": C["raio"], "imas": C["imas"], "fator": C["fator"]}


def restringir(v, lo, hi):
    return max(lo, min(hi, v))


def para_int(s):
    """Como String::toInt(): lê o início numérico; texto inválido vira 0."""
    m = re.match(r"\s*[-+]?\d+", s)
    return int(m.group()) if m else 0


def para_float(s):
    """Como String::toFloat(): texto inválido vira 0."""
    m = re.match(r"\s*[-+]?(\d+\.?\d*|\.\d+)", s)
    return float(m.group()) if m else 0.0


def aplicar_set(q):  # espelha o handler de /set: parâmetro ausente mantém o valor
    if "raio" in q:
        P["raio"] = restringir(para_int(q["raio"][0]), 1, 100)
    if "imas" in q:
        P["imas"] = restringir(para_int(q["imas"][0]), 1, 3)
    if "fator" in q:
        P["fator"] = restringir(para_float(q["fator"][0]), 1.0, 5.0)


def status_json():  # espelha o handler de /status (String(float) tem 2 casas)
    return {"id": C["ID_ANEMOMETRO"], "raioRotor": P["raio"], "numImas": P["imas"],
            "fatorCopo": round(P["fator"], 2)}


# ---------------------------------------------------------------------------
# Rotor virtual
# ---------------------------------------------------------------------------

class Rotor:
    """Vento turbulento em torno de uma média; a leitura imita o loop() do firmware:
    atualiza uma vez por janela, mantém o último valor sem pulsos e zera após o timeout."""

    def __init__(self, w_medio):
        self.trava = threading.Lock()
        self.w_medio = w_medio
        self.t = 0.0
        self.ruido = 0.0
        self.w_lido = 0.0
        self.ultimo_pulso = None   # instante (s) em que o rotor ainda girava
        self.inicio_janela = 0.0

    def w_real(self):
        if self.w_medio <= 0:
            return 0.0
        rajada = 0.15 * math.sin(2 * math.pi * self.t / 7) + 0.08 * math.sin(2 * math.pi * self.t / 2.3)
        return max(0.0, self.w_medio * (1 + rajada + self.ruido))

    def passo(self, dt):
        with self.trava:
            self.t += dt
            self.ruido += (random.gauss(0, 0.05) - self.ruido) * min(1.0, dt * 2)  # ruído suavizado
            w = self.w_real()
            if w > 0:
                self.ultimo_pulso = self.t
            if self.t - self.inicio_janela < C["JANELA_MEDIA_MS"] / 1000:
                return
            self.inicio_janela = self.t
            if w > 0:
                # Acima do limite do debounce os pulsos são descartados e a leitura fica abaixo do real
                w_max = 2 * math.pi / (P["imas"] * C["DEBOUNCE_US"] / 1e6)
                self.w_lido = min(w, w_max)
            if self.ultimo_pulso is None or self.t - self.ultimo_pulso > C["TIMEOUT_PARADO_MS"] / 1000:
                self.w_lido = 0.0

    def dados(self):  # espelha o handler de /dados: v = w * (R/100) * K
        with self.trava:
            v = self.w_lido * (P["raio"] / 100) * P["fator"]
            return {"v": round(v, 2), "w": round(self.w_lido, 2)}


# ---------------------------------------------------------------------------
# Conferência página × simulador
# ---------------------------------------------------------------------------

_ultimo_aviso = None


def conferir(html):
    global _ultimo_aviso
    avisos = []
    for rota in re.findall(r"fetch\(\s*['`\"](/[^'`\"?$]*)", html):
        if rota not in ROTAS:
            avisos.append(f"fetch('{rota}') não é atendido pelo simulador")
    # getElementById('x') para um id inexistente devolve null e o acesso seguinte lança erro
    ids = set(re.findall(r'\bid="([^"]+)"', html))
    padrao_ref = r"""(?:getElementById|setCampo|validarCampo|stepValue|stepFloat)\(\s*'([\w-]+)'"""
    for ref in sorted(set(re.findall(padrao_ref, html))):
        if ref not in ids:
            avisos.append(f"'{ref}' não corresponde a nenhum id da página")
    # data.x lido de /status ou /dados
    chaves = set(status_json()) | {"v", "w"}
    for chave in sorted(set(re.findall(r"\bdata\.(\w+)", html))):
        if chave not in chaves:
            avisos.append(f"data.{chave} não existe em /status nem em /dados")
    if avisos != _ultimo_aviso:
        for a in avisos:
            print("Aviso:", a)
        if _ultimo_aviso and not avisos:
            print("Página e simulador conferem.")
        _ultimo_aviso = avisos


# ---------------------------------------------------------------------------
# Servidor HTTP
# ---------------------------------------------------------------------------

def criar_handler(rotor):
    class Handler(BaseHTTPRequestHandler):
        def enviar(self, corpo, tipo="text/plain", status=200):
            dados = corpo.encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", tipo + "; charset=utf-8")
            self.send_header("Content-Length", str(len(dados)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(dados)

        def enviar_json(self, obj):
            self.enviar(json.dumps(obj, ensure_ascii=False), "application/json")

        def do_GET(self):
            url = urlparse(self.path)
            q = parse_qs(url.query, keep_blank_values=True)

            if url.path == "/":
                try:
                    html = ler_html()
                except (OSError, ValueError) as e:
                    return self.enviar(f"Erro ao ler a página: {e}", status=500)
                conferir(html)
                return self.enviar(html, "text/html")
            if url.path == "/status":
                with rotor.trava:
                    return self.enviar_json(status_json())
            if url.path == "/dados":
                return self.enviar_json(rotor.dados())
            if url.path == "/set":
                with rotor.trava:
                    aplicar_set(q)
                return self.enviar("OK")
            if url.path == "/save":
                return self.enviar("SALVO")  # A flash não é simulada: reiniciar o script volta aos padrões
            if url.path == "/_vento":
                with rotor.trava:
                    rotor.w_medio = max(0.0, para_float(q.get("w", ["0"])[0]))
                    return self.enviar(f"Velocidade angular média: {rotor.w_medio:g} rad/s")
            if url.path == "/favicon.ico":
                self.send_response(204)
                return self.end_headers()
            self.enviar("Não encontrado", status=404)

        def log_message(self, fmt, *args):
            pass  # /dados a cada 500 ms poluiria o terminal

    return Handler


def main():
    ap = argparse.ArgumentParser(description="Pré-visualização da página do Anemômetro Digital.")
    ap.add_argument("--porta", type=int, default=8000)
    ap.add_argument("--vento", type=float, default=30.0,
                    help="velocidade angular média do rotor virtual, em rad/s")
    ap.add_argument("--rede", action="store_true",
                    help="aceitar conexões de outros aparelhos da rede")
    args = ap.parse_args()

    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except AttributeError:
        pass

    rotor = Rotor(args.vento)

    def relogio():
        anterior = time.monotonic()
        while True:
            time.sleep(0.05)
            agora = time.monotonic()
            rotor.passo(agora - anterior)
            anterior = agora

    threading.Thread(target=relogio, daemon=True).start()

    try:
        conferir(ler_html())
    except (OSError, ValueError) as e:
        print("Aviso:", e)

    host = "0.0.0.0" if args.rede else "127.0.0.1"
    servidor = ThreadingHTTPServer((host, args.porta), criar_handler(rotor))
    servidor.daemon_threads = True
    print(f"Simulador do Anemômetro {C['ID_ANEMOMETRO']} em http://localhost:{args.porta}  (Ctrl+C para sair)")
    print(f"Vento: http://localhost:{args.porta}/_vento?w=60  ·  Parar o rotor: /_vento?w=0")
    try:
        servidor.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
