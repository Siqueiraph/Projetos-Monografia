#!/usr/bin/env python3
"""
Visualizador das interfaces web dos firmwares, sem gravar o ESP32.

Serve o pagina_html.h do projeto escolhido (relido a cada requisição: salve e
dê F5) e responde às rotas com valores fixos de exemplo, só para a página
renderizar. Não simula sensores nem guarda o que é enviado por /set.

Uso (só biblioteca padrão, Python 3.8+):
    python scripts/visualizar_interface.py <projeto> [--porta 8000] [--rede]

    projeto   anemometro | balanca | irrigacao | musical | rede
    --rede    aceita conexões de outros aparelhos (celular na mesma rede)
"""

import argparse
import json
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

RAIZ = Path(__file__).resolve().parent.parent

# Pasta de cada projeto e respostas fixas das rotas JSON que a página consulta
PROJETOS = {
    "anemometro": ("Anemometro_Digital", {
        "/status": {"id": "A", "raioRotor": 10, "numImas": 1, "fatorCopo": 2.5},
        "/dados": {"v": 7.5, "w": 30.0},
    }),
    "balanca": ("Balanca_Inercial", {
        "/status": {"constanteK": 15.0, "numCiclos": 3, "intervaloLeitura": 40},
        "/dados": {"h": 12.0, "T": 0.8, "m": 243.2},
    }),
    "irrigacao": ("Irrigacao_Automatica", {
        "/status": {"vSeco": 500, "vUmido": 3500, "limite": 30, "tempo": 10},
        "/dados": {"umidade": 45, "statusBomba": 0, "bloqueio": 0},
    }),
    "musical": ("Controle_Musical", {  # /events (SSE) não é atendido: o gráfico fica vazio
        "/status": {"limiarGlobal": 400, "holdTime": 120, "offsetGrave": 0, "ganhoGrave": 1,
                    "offsetMedio": 0, "ganhoMedio": 1, "offsetAgudo": 0, "ganhoAgudo": 1},
    }),
    "rede": ("Rede_Comunicacao", {
        "/api/projetos": [
            {"nome": "Anemometro A", "ip": "192.168.4.2", "ativo": True},
            {"nome": "Anemometro B", "ip": "0.0.0.0", "ativo": False},
            {"nome": "Anemometro C", "ip": "0.0.0.0", "ativo": False},
            {"nome": "Anemometro D", "ip": "0.0.0.0", "ativo": False},
        ],
    }),
}
TEXTO = {"/set": "OK", "/save": "SALVO"}


def ler_html(arquivo):
    m = re.search(r'R"rawliteral\((.*)\)rawliteral"', arquivo.read_text(encoding="utf-8"), re.S)
    if not m:
        raise ValueError('não achei R"rawliteral(...)rawliteral" em ' + str(arquivo))
    return m.group(1)


def criar_handler(arquivo, rotas):
    class Handler(BaseHTTPRequestHandler):
        def enviar(self, corpo, tipo="text/plain", status=200):
            dados = corpo.encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", tipo + "; charset=utf-8")
            self.send_header("Content-Length", str(len(dados)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(dados)

        def do_GET(self):
            rota = urlparse(self.path).path
            if rota == "/":
                try:
                    return self.enviar(ler_html(arquivo), "text/html")
                except (OSError, ValueError) as e:
                    return self.enviar(f"Erro ao ler a página: {e}", status=500)
            if rota in rotas:
                return self.enviar(json.dumps(rotas[rota], ensure_ascii=False), "application/json")
            if rota in TEXTO:
                return self.enviar(TEXTO[rota])
            self.send_response(204)  # /events, favicon etc.: 204 também faz o EventSource parar de reconectar
            self.end_headers()

        def log_message(self, fmt, *args):
            pass  # as consultas periódicas da página poluiriam o terminal

    return Handler


def main():
    ap = argparse.ArgumentParser(description="Visualizador das interfaces web dos firmwares.")
    ap.add_argument("projeto", choices=PROJETOS)
    ap.add_argument("--porta", type=int, default=8000)
    ap.add_argument("--rede", action="store_true", help="aceitar conexões de outros aparelhos da rede")
    args = ap.parse_args()

    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except AttributeError:
        pass

    pasta, rotas = PROJETOS[args.projeto]
    arquivo = RAIZ / pasta / "pagina_html.h"
    ler_html(arquivo)  # Falha já na partida se o arquivo não existir ou não tiver o rawliteral

    host = "0.0.0.0" if args.rede else "127.0.0.1"
    servidor = ThreadingHTTPServer((host, args.porta), criar_handler(arquivo, rotas))
    servidor.daemon_threads = True
    print(f"{pasta} em http://localhost:{args.porta}  (Ctrl+C para sair)")
    try:
        servidor.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
