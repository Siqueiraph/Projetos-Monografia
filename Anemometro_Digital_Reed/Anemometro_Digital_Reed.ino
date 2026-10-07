// Anemometro Digital - Monitor de Velocidade do Vento (m/s e rad/s)
// Hardware: ESP32 + Sensor Reed Switch KY-025

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <math.h>

// CONFIGURAÇÃO DE REDE
// const char* ssid     = "Rede_Comunicacao";
// const char* password = "123456789";
// const char* ssid     = "CLARO_6E22E7-IoT";
// const char* password = "Enzomorfo6927";
const char* ssid     = "ESP IoT";
const char* password = "123456789";

// PINOS DE HARDWARE
const int PINO_REED_D0 = 14; // Saída Digital (DO) do sensor — usada na interrupção de período/velocidade

// PARÂMETROS DE MEDIÇÃO
const unsigned long DEBOUNCE_US       = 15000; // Tempo mínimo entre dois pulsos válidos (15 ms)
const unsigned long LARGURA_MIN_US    = 1000;  // Nível baixo mais curto que isso é repique do contato, não passagem do ímã
const unsigned long JANELA_MEDIA_MS   = 1000;  // Janela sobre a qual a velocidade é calculada (média dos pulsos)
const unsigned long TIMEOUT_PARADO_MS = 3000;  // Sem pulsos por esse tempo, considera o rotor parado

// VARIÁVEIS VOLÁTEIS PARA INTERRUPÇÃO (ISR)
volatile unsigned long tempoUltimoPulso   = 0; // micros() do último pulso válido
volatile unsigned long tempoUltimaDescida = 0; // micros() da última borda de descida
volatile unsigned long contadorPulsos     = 0; // Total de pulsos válidos desde o boot

// Função de Interrupção Acionada pelo Sensor (bordas de subida e descida)
void IRAM_ATTR ISR_DetectaIma() {
  unsigned long agora = micros();
  if (digitalRead(PINO_REED_D0) == LOW) {
    tempoUltimaDescida = agora;
    return;
  }
  // Borda de subida: só conta se o nível baixo anterior durou o suficiente (descarta repique do reed)
  if (agora - tempoUltimaDescida < LARGURA_MIN_US) return;
  // Debounce de 15ms para evitar ruído mecânico/magnético
  if (agora - tempoUltimoPulso < DEBOUNCE_US) return;
  tempoUltimoPulso = agora;
  contadorPulsos++;
}

// OBJETOS GLOBAIS
AsyncWebServer server(80);
Preferences preferences;

// Parâmetros ajustáveis via interface web
int   raioRotor  = 10;   // Raio em centímetros (cm)
int   numImas    = 1;    // Quantidade de ímãs no rotor
float fatorCopo  = 2.5;  // Fator aerodinâmico K (Adimensional)

// Variáveis de cálculo
float velocidadeVentoMs = 0.0;
float velocidadeAngular = 0.0;

// FRONT-END HTML
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="UTF-8">
  <title>Anemômetro Digital</title>
  <style>
    :root {--destaque: #ff9900; --preto1: #0a0a0a; --preto2: #1a1a1a; --cinza1: #333; --cinza2: #ccc; --branco: #fff;}
    * { box-sizing: border-box; }
    html { font-size: clamp(14px, 1.2vw, 18px); }
    body { font-family: 'Segoe UI', Arial, sans-serif; background-color: var(--preto1); color: var(--cinza2); margin: 0; padding: 20px; display: flex; justify-content: center; min-height: 100vh;}

    .main-container {
      display: grid;
      grid-template-columns: 1.5fr 1fr;
      grid-template-rows: 1fr 1fr 1fr;
      gap: 20px;
      width: 100%; height: 100%;
      max-width: 1800px;
      align-items: stretch;
    }

    .graph-wrapper {
      grid-column: 1;
      grid-row: 1 / 3;
      position: relative;
      background: var(--preto1); border: 1px solid var(--cinza1); border-radius: 12px; overflow: hidden; display: flex; flex-direction: column; min-height: 250px;
    }
    .c-fisica  { grid-column: 2; grid-row: 1 / 2; }
    .c-calibra { grid-column: 2; grid-row: 2 / 3; }

    canvas { display: block; width: 100%; flex-grow: 1; touch-action: pan-y; cursor: crosshair; }
    #dataOverlay { position: absolute; top: 14px; right: 78px; font-size: clamp(30px, 4vh, 40px); font-weight: bold; text-shadow: 2px 2px 6px var(--preto1); z-index: 10; font-family: monospace; text-align: right; line-height: 1.1; pointer-events: none;}
    .data-v { color: var(--destaque); }
    .data-w { color: var(--cinza2); }

    .top-btns { position: absolute; top: 21px; left: 72px; z-index: 10; display: flex; gap: 8px; }
    .top-btn { padding: 10px 16px; font-size: 14px; font-weight: bold; background-color: var(--preto2); color: var(--branco); border: 1px solid var(--cinza1); border-radius: 6px; cursor: pointer; transition: 0.2s; }
    .top-btn:hover { filter: brightness(1.4); }
    .btn-saved { background-color: #4CAF50 !important; border-color: #4CAF50 !important; color: var(--preto1) !important; }
    .btn-hold  { background-color: var(--destaque) !important; border-color: var(--destaque) !important; color: var(--preto1) !important; }

    .card { background-color: var(--preto2); padding: 18px 20px; border-radius: 10px; border-left: 5px solid var(--destaque); box-shadow: 0 4px 10px rgba(0,0,0,0.4); display: flex; flex-direction: column; justify-content: center;}
    h3 { margin: 0 0 16px 0; font-size: clamp(14px, 3vh, 22px); color: var(--cinza2); border-bottom: 1px solid var(--cinza1); padding-bottom: 8px; letter-spacing: 1px;}

    .control-row { display: flex; align-items: center; gap: 10px; margin-top: 8px; }
    .control-row label { width: 70px; font-size: clamp(12px, 2.5vh, 16px); color: var(--cinza2); font-weight: bold; flex-shrink: 0; }
    .step-btn { background-color: var(--preto2); color: var(--branco); border: 1px solid var(--cinza1); border-radius: 6px; width: 36px; height: 36px; font-size: 18px; font-weight: bold; cursor: pointer; flex-shrink: 0; transition: 0.2s; }
    .step-btn:hover { filter: brightness(1.4); }

    .control-row input { flex: 1; background: var(--preto2); color: var(--cinza2); border: 1px solid var(--cinza1); border-radius: 6px; padding: 8px; font-family: monospace; font-size: clamp(14px, 3vh, 18px); font-weight: bold; text-align: center; min-width: 0; }
    .control-row input:focus { outline: none; filter: brightness(1.3); }
    input[type=number]::-webkit-inner-spin-button, input[type=number]::-webkit-outer-spin-button { -webkit-appearance: none; }

    @media (max-width: 768px) {
      body { padding: 10px; }
      .main-container { grid-template-columns: 1fr; grid-template-rows: auto; gap: 12px; }
      .graph-wrapper, .c-fisica, .c-calibra { grid-column: 1; grid-row: auto; }
      .graph-wrapper { min-height: 320px; }
    }
  </style>
</head>
<body>
  <div class="main-container">
    <div class="graph-wrapper">
      <div class="top-btns">
        <button class="top-btn" id="btnSave" onclick="saveData()">Salvar</button>
        <button class="top-btn" id="btnHold" onclick="toggleHold()">Hold</button>
      </div>
      <div id="dataOverlay">
        <span class="data-v">v: <span id="velHtml">0.0</span> m/s</span><br>
        <span class="data-w">w: <span id="omegaHtml">0.0</span> rad/s</span>
      </div>
      <canvas id="plotCanvas"></canvas>
    </div>
    <div class="card c-fisica">
      <h3>GEOMETRIA</h3>
      <div class="control-row">
        <label>Raio (cm)</label>
        <button class="step-btn" onclick="stepValue('raio', -1)">-</button>
        <input type="text" inputmode="decimal" min="1" max="100" id="raio" onchange="sendData()">
        <button class="step-btn" onclick="stepValue('raio', 1)">+</button>
      </div>
      <div class="control-row">
        <label>Qtd Ímãs</label>
        <button class="step-btn" onclick="stepValue('imas', -1)">-</button>
        <input type="number" min="1" max="12" id="imas" onchange="sendData()">
        <button class="step-btn" onclick="stepValue('imas', 1)">+</button>
      </div>
    </div>
    <div class="card c-calibra">
      <h3>AERODINÂMICA</h3>
      <div class="control-row">
        <label>Fator K</label>
        <button class="step-btn" onclick="stepFloat('fator', -0.1)">-</button>
        <input type="text" inputmode="decimal" min="1.0" max="5.0" id="fator" onchange="sendData()">
        <button class="step-btn" onclick="stepFloat('fator', 0.1)">+</button>
      </div>
    </div>
  </div>

  <script>
    const JANELA_S = 50;                              // Janela de tempo visível no gráfico (s)
    const DIVISOES_Y = 5;                             // Divisões horizontais da grade (iguais nos dois eixos verticais)
    const ESCALA_V = { piso: 5,  teto: 50 };          // Limites do topo do eixo de velocidade tangencial (m/s)
    const ESCALA_W = { piso: 25, teto: 250 };         // Limites do topo do eixo de velocidade angular (rad/s)
    const SEP_DECIMAL = '.';                          // Separador decimal usado em toda a interface
    let amostras = [];                                // Histórico: { t (s), v (m/s), w (rad/s) }
    let emHold = false;                               // Gráfico congelado para leitura
    let cursorX = null;                               // Posição do cursor de leitura (px do canvas)
    const canvas = document.getElementById('plotCanvas');
    const ctx = canvas.getContext('2d');

    // Formata um número com o separador decimal da interface
    function fmt(val, casas) { return val.toFixed(casas).replace('.', SEP_DECIMAL); }
    // Interpreta um número digitado com vírgula ou com ponto
    function lerNumero(texto) { return parseFloat(String(texto).replace(',', '.')); }

    function resizeCanvas() {
      setTimeout(() => {
        canvas.width = canvas.parentElement.clientWidth;
        canvas.height = canvas.parentElement.clientHeight;
        drawCanvas();
      }, 50);
    }

    // Escreve um valor no campo e o registra como último valor válido
    function setCampo(id, val, casas) {
      let el = document.getElementById(id);
      el.dataset.ultimo = val;
      el.value = casas > 0 ? fmt(val, casas) : String(val);
    }

    // Valida o campo (valor inválido volta ao último aceito; fora da faixa é limitado) e devolve o texto a enviar ao ESP32
    function validarCampo(id, casas) {
      let el = document.getElementById(id);
      let min = parseFloat(el.getAttribute('min')), max = parseFloat(el.getAttribute('max'));
      let val = lerNumero(el.value);
      if (isNaN(val)) val = parseFloat(el.dataset.ultimo);
      if (isNaN(val)) val = min;
      if (casas === 0) val = Math.round(val);
      val = Math.max(min, Math.min(max, val));
      val = parseFloat(val.toFixed(casas));
      setCampo(id, val, casas);
      return val.toFixed(casas);
    }

    window.addEventListener('resize', resizeCanvas);
    window.onload = function() {
      resizeCanvas();
      fetch('/status').then(res => res.json()).then(data => {
        setCampo('raio', data.raioRotor, 0);
        setCampo('imas', data.numImas, 0);
        setCampo('fator', data.fatorCopo, 1);
      });
    };

    function stepValue(id, delta) {
      let el = document.getElementById(id);
      let val = lerNumero(el.value);
      if (isNaN(val)) val = parseFloat(el.dataset.ultimo) || 0;
      el.value = String(Math.round(val) + delta);
      sendData();
    }

    function stepFloat(id, delta) {
      let el = document.getElementById(id);
      let val = lerNumero(el.value);
      if (isNaN(val)) val = parseFloat(el.dataset.ultimo) || 0.0;
      el.value = (val + delta).toFixed(1);
      sendData();
    }

    function sendData() {
      let r = validarCampo('raio', 0);
      let i = validarCampo('imas', 0);
      let f = validarCampo('fator', 1);
      fetch(`/set?raio=${r}&imas=${i}&fator=${f}`);
    }

    function saveData() {
      let btn = document.getElementById('btnSave');
      fetch('/save').then(() => {
        btn.innerText = "Salvo!";
        btn.classList.add('btn-saved');
        setTimeout(() => { btn.innerText = "Salvar"; btn.classList.remove('btn-saved'); }, 2000);
      });
    }

    // Congela/retoma o gráfico para que os valores possam ser lidos com calma
    function toggleHold() {
      emHold = !emHold;
      let btn = document.getElementById('btnHold');
      btn.innerText = emHold ? "Retomar" : "Hold";
      btn.classList.toggle('btn-hold', emHold);
    }

    // Cursor de leitura: acompanha o mouse/toque e mostra os valores do ponto mais próximo
    function moveCursor(e) {
      let rect = canvas.getBoundingClientRect();
      cursorX = (e.clientX - rect.left) * (canvas.width / rect.width);
      drawCanvas();
    }
    canvas.addEventListener('pointerdown', moveCursor);
    canvas.addEventListener('pointermove', moveCursor);
    canvas.addEventListener('pointerleave', (e) => {
      if (e.pointerType === 'mouse') { cursorX = null; drawCanvas(); }
    });

    setInterval(function() {
      if (emHold) return;
      fetch('/dados').then(res => res.json()).then(data => {
        if (emHold) return;
        document.getElementById('velHtml').innerText = fmt(data.v, 1);
        document.getElementById('omegaHtml').innerText = fmt(data.w, 2);

        let t = performance.now() / 1000;
        amostras.push({ t: t, v: data.v, w: data.w });
        while (amostras.length && amostras[0].t < t - JANELA_S) amostras.shift();
        drawCanvas();
      });
    }, 500);

    // Topo do eixo: menor valor "redondo" (1, 2 ou 5 x 10^n por divisão) que comporta o máximo,
    // limitado entre o piso e o teto para que a escala não amplie nem comprima demais as curvas
    function topoEscala(maximo, limites) {
      if (!(maximo > 0)) return limites.piso;
      let bruto = (maximo * 1.1) / DIVISOES_Y;
      let pot = Math.pow(10, Math.floor(Math.log10(bruto)));
      let f = bruto / pot;
      let topo = (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * pot * DIVISOES_Y;
      return Math.max(limites.piso, Math.min(limites.teto, topo));
    }

    function drawCanvas() {
      const W = canvas.width, H = canvas.height;
      const fs = W < 500 ? 12 : 15;                       // Tamanho da fonte dos eixos (px)
      const margemLateral = fs * 4 + 6;                   // Espaço para os valores e o título de cada eixo vertical
      const x0 = margemLateral, x1 = W - margemLateral, y0 = 16, y1 = H - (fs * 3 + 8);
      const COR_V = '#ff9900', COR_W = '#ccc', COR_T = '#aaa';
      const fonteValores = fs + 'px monospace';
      const fonteTitulos = 'bold ' + fs + "px 'Segoe UI', Arial, sans-serif";
      ctx.clearRect(0, 0, W, H);
      if (x1 <= x0 || y1 <= y0) return;

      // Cada curva tem o próprio eixo vertical: v à esquerda (m/s) e w à direita (rad/s)
      let maxV = 0, maxW = 0;
      amostras.forEach(a => { maxV = Math.max(maxV, a.v); maxW = Math.max(maxW, a.w); });
      const topoV = topoEscala(maxV, ESCALA_V);
      const topoW = topoEscala(maxW, ESCALA_W);
      const tFim = amostras.length ? amostras[amostras.length - 1].t : 0;
      const px  = t => x1 - ((tFim - t) / JANELA_S) * (x1 - x0);
      const pyV = val => y1 - (val / topoV) * (y1 - y0);
      const pyW = val => y1 - (val / topoW) * (y1 - y0);
      const casasV = (topoV / DIVISOES_Y) < 1 ? 1 : 0;
      const casasW = (topoW / DIVISOES_Y) < 1 ? 1 : 0;

      // Grade horizontal com os valores dos dois eixos verticais
      ctx.setLineDash([]); ctx.lineWidth = 1; ctx.strokeStyle = '#333';
      ctx.font = fonteValores; ctx.textBaseline = 'middle';
      for (let i = 0; i <= DIVISOES_Y; i++) {
        let y = Math.round(y1 - (i / DIVISOES_Y) * (y1 - y0)) + 0.5;
        ctx.beginPath(); ctx.moveTo(x0, y); ctx.lineTo(x1, y); ctx.stroke();
        ctx.fillStyle = COR_V; ctx.textAlign = 'right';
        ctx.fillText(fmt(topoV * i / DIVISOES_Y, casasV), x0 - 6, y);
        ctx.fillStyle = COR_W; ctx.textAlign = 'left';
        ctx.fillText(fmt(topoW * i / DIVISOES_Y, casasW), x1 + 6, y);
      }

      // Grade vertical: tempo passado (0 = leitura mais recente, à direita)
      const passoT = (x1 - x0) < 600 ? 10 : 5;
      ctx.fillStyle = COR_T; ctx.textAlign = 'center'; ctx.textBaseline = 'top';
      for (let s = 0; s <= JANELA_S; s += passoT) {
        let x = Math.round(x1 - (s / JANELA_S) * (x1 - x0)) + 0.5;
        ctx.beginPath(); ctx.moveTo(x, y0); ctx.lineTo(x, y1); ctx.stroke();
        ctx.fillText(String(s), x, y1 + 6);
      }

      // Títulos dos três eixos
      ctx.font = fonteTitulos;
      ctx.fillStyle = COR_T; ctx.textAlign = 'center'; ctx.textBaseline = 'bottom';
      ctx.fillText('Tempo passado [s]', (x0 + x1) / 2, H - 7);
      ctx.textBaseline = 'top';
      ctx.save(); ctx.translate(10, (y0 + y1) / 2); ctx.rotate(-Math.PI / 2);
      ctx.fillStyle = COR_V; ctx.fillText('Velocidade Tangencial [m/s]', 0, 0);
      ctx.restore();
      ctx.save(); ctx.translate(W - 10, (y0 + y1) / 2); ctx.rotate(Math.PI / 2);
      ctx.fillStyle = COR_W; ctx.fillText('Velocidade Angular [rad/s]', 0, 0);
      ctx.restore();

      if (amostras.length === 0) return;

      // Curvas recortadas na área do gráfico (valores acima do teto da escala não invadem as margens)
      ctx.save();
      ctx.beginPath(); ctx.rect(x0, y0, x1 - x0, y1 - y0); ctx.clip();

      // Desenha a curva da Velocidade Angular (rad/s) - Cinza
      ctx.strokeStyle = COR_W; ctx.lineWidth = 2; ctx.beginPath();
      amostras.forEach((a, i) => {
        if (i === 0) ctx.moveTo(px(a.t), pyW(a.w)); else ctx.lineTo(px(a.t), pyW(a.w));
      });
      ctx.stroke();

      // Desenha a curva da Velocidade Tangencial (m/s) - Laranja
      ctx.fillStyle = 'rgba(255, 153, 0, 0.15)'; ctx.beginPath();
      ctx.moveTo(px(amostras[0].t), y1);
      amostras.forEach(a => ctx.lineTo(px(a.t), pyV(a.v)));
      ctx.lineTo(px(tFim), y1);
      ctx.closePath();
      ctx.fill();
      ctx.strokeStyle = COR_V; ctx.lineWidth = 3; ctx.beginPath();
      amostras.forEach((a, i) => {
        if (i === 0) ctx.moveTo(px(a.t), pyV(a.v)); else ctx.lineTo(px(a.t), pyV(a.v));
      });
      ctx.stroke();
      ctx.restore();

      // Cursor de leitura: linha vertical e valores do ponto mais próximo
      if (cursorX !== null) {
        let sel = amostras[0];
        amostras.forEach(a => { if (Math.abs(px(a.t) - cursorX) < Math.abs(px(sel.t) - cursorX)) sel = a; });
        let x = px(sel.t);
        ctx.strokeStyle = '#fff'; ctx.lineWidth = 1; ctx.setLineDash([4, 4]);
        ctx.beginPath(); ctx.moveTo(x, y0); ctx.lineTo(x, y1); ctx.stroke();
        ctx.setLineDash([]);
        ctx.fillStyle = COR_W; ctx.beginPath(); ctx.arc(x, Math.max(y0, pyW(sel.w)), 4, 0, 2 * Math.PI); ctx.fill();
        ctx.fillStyle = COR_V; ctx.beginPath(); ctx.arc(x, Math.max(y0, pyV(sel.v)), 4, 0, 2 * Math.PI); ctx.fill();

        let texto = 't = ' + fmt(tFim - sel.t, 1) + ' s   v = ' + fmt(sel.v, 2) + ' m/s   w = ' + fmt(sel.w, 2) + ' rad/s';
        ctx.font = 'bold ' + (fs - 1) + 'px monospace';
        let larg = ctx.measureText(texto).width + 16;
        let bx = Math.max(x0, Math.min(x1 - larg, x - larg / 2));
        ctx.fillStyle = 'rgba(26, 26, 26, 0.92)'; ctx.fillRect(bx, y1 - 32, larg, 26);
        ctx.strokeStyle = '#333'; ctx.strokeRect(bx + 0.5, y1 - 31.5, larg, 26);
        ctx.fillStyle = '#fff'; ctx.textAlign = 'left'; ctx.textBaseline = 'middle';
        ctx.fillText(texto, bx + 8, y1 - 19);
      }
    }
  </script>
</body>
</html>
)rawliteral";

void conectarWiFi() { // CONEXÃO WI-FI
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) { delay(500); tentativas++; }
  if (MDNS.begin("Anemometro")) MDNS.addService("http", "tcp", 80);
}

void setup() { // SETUP
  pinMode(PINO_REED_D0, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(PINO_REED_D0), ISR_DetectaIma, CHANGE);

  preferences.begin("anemo_cfg", false);
  raioRotor = preferences.getInt("raio", 10);
  numImas   = preferences.getInt("imas", 1);
  fatorCopo = preferences.getFloat("fator", 2.5);

  conectarWiFi();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"raioRotor\":" + String(raioRotor) +
                  ",\"numImas\":"   + String(numImas) +
                  ",\"fatorCopo\":" + String(fatorCopo) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/dados", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"v\":" + String(velocidadeVentoMs) +
                  ",\"w\":" + String(velocidadeAngular)  + "}";
    request->send(200, "application/json", json);
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("raio"))  raioRotor = request->getParam("raio")->value().toInt();
    if (request->hasParam("imas"))  numImas   = request->getParam("imas")->value().toInt();
    if (request->hasParam("fator")) fatorCopo = request->getParam("fator")->value().toFloat();
    request->send(200, "text/plain", "OK");
  });

  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    preferences.putInt("raio",   raioRotor);
    preferences.putInt("imas",   numImas);
    preferences.putFloat("fator", fatorCopo);
    request->send(200, "text/plain", "SALVO");
  });

  server.begin();
}

void loop() { // LOOP PRINCIPAL
  unsigned long agora = millis();

  // Cálculo da Física (uma vez por janela, usando a média de todos os pulsos ocorridos nela)
  static unsigned long inicioJanela = 0;
  static unsigned long pulsosRef = 0;     // contadorPulsos no fim da última janela com pulsos
  static unsigned long tempoRef  = 0;     // tempoUltimoPulso no fim da última janela com pulsos
  static bool referenciaValida = false;   // Falso até o primeiro pulso após o rotor estar parado

  if (agora - inicioJanela >= JANELA_MEDIA_MS) {
    inicioJanela = agora;

    noInterrupts();
    unsigned long pulsos    = contadorPulsos;
    unsigned long tempoPuls = tempoUltimoPulso;
    interrupts();

    unsigned long n = pulsos - pulsosRef;
    if (n > 0) {
      if (referenciaValida) {
        // Tempo médio entre ímãs consecutivos = (tempo entre o último pulso da janela anterior e o desta) / n
        float intervaloMedio = ((tempoPuls - tempoRef) / 1000000.0) / n;

        // Período total (T) de uma volta = (tempo entre ímãs) * (numero de ímãs)
        float periodoSegundos = intervaloMedio * numImas;

        // w = 2*PI / T
        if (periodoSegundos > 0) {
          velocidadeAngular = (2.0 * PI) / periodoSegundos;
        }
      }
      pulsosRef = pulsos;
      tempoRef  = tempoPuls;
      referenciaValida = true;
    }

    // Timeout: Se o rotor parar, a velocidade deve zerar
    if (micros() - tempoPuls > TIMEOUT_PARADO_MS * 1000UL) {
      velocidadeAngular = 0.0;
      referenciaValida = false;
    }

    // v = w * r * K  (convertendo r de cm para metros. Removido o * 3.6 para manter em m/s)
    velocidadeVentoMs = velocidadeAngular * (raioRotor / 100.0) * fatorCopo;
  }

  // Ping para o Módulo Central de Comunicação
  static unsigned long lastPing = 0;
  if (agora - lastPing > 30000) { // era 5000
    lastPing = agora;
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.setConnectTimeout(1000);
      http.setTimeout(1000);
      http.begin("http://192.168.4.1/ping?nome=Anemometro");
      http.GET();
      http.end();
    }
  }
}
