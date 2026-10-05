// Anemometro Digital - Monitor de Velocidade do Vento (m/s e rad/s)
// Hardware: ESP32 + Sensor Hall KY-003 ou Reed Switch KY-025

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <math.h>

// CONFIGURAÇÃO DE REDE
// const char* ssid     = "Rede_Comunicacao";
// const char* password = "123456789";
const char* ssid     = "CLARO_6E22E7-IoT";
const char* password = "Enzomorfo6927";

// PINOS DE HARDWARE
const int PINO_SENSOR = 14; // Entrada Digital do Sensor

// VARIÁVEIS VOLÁTEIS PARA INTERRUPÇÃO (ISR)
volatile unsigned long tempoUltimoPulso = 0;
volatile unsigned long deltaTempoRaw = 0;
volatile bool novoPulso = false;

// Função de Interrupção Acionada pelo Sensor
void IRAM_ATTR ISR_DetectaIma() {
  unsigned long agora = millis();
  // Debounce de 15ms para evitar ruído mecânico/magnético
  if (agora - tempoUltimoPulso > 15) {
    deltaTempoRaw = agora - tempoUltimoPulso;
    tempoUltimoPulso = agora;
    novoPulso = true;
  }
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

    canvas { display: block; width: 100%; flex-grow: 1; }
    #dataOverlay { position: absolute; top: 14px; right: 20px; font-size: clamp(30px, 4vh, 40px); font-weight: bold; text-shadow: 2px 2px 6px var(--preto1); z-index: 10; font-family: monospace; text-align: right; line-height: 1.1;}
    .data-v { color: var(--destaque); }
    .data-w { color: var(--cinza2); }
    
    #btnSave { position: absolute; top: 14px; left: 14px; z-index: 10; padding: 10px 16px; font-size: 14px; font-weight: bold; background-color: var(--preto2); color: var(--branco); border: 1px solid var(--cinza1); border-radius: 6px; cursor: pointer; transition: 0.2s; }
    #btnSave:hover { filter: brightness(1.4); }
    .btn-saved { background-color: #4CAF50 !important; border-color: #4CAF50 !important; color: var(--preto1) !important; }

    .card { background-color: var(--preto2); padding: 18px 20px; border-radius: 10px; border-left: 5px solid var(--destaque); box-shadow: 0 4px 10px rgba(0,0,0,0.4); display: flex; flex-direction: column; justify-content: center;}
    h3 { margin: 0 0 16px 0; font-size: clamp(14px, 3vh, 22px); color: var(--cinza2); border-bottom: 1px solid var(--cinza1); padding-bottom: 8px; letter-spacing: 1px;}
  
    .control-row { display: flex; align-items: center; gap: 10px; margin-top: 8px; }
    .control-row label { width: 70px; font-size: clamp(12px, 2.5vh, 16px); color: var(--cinza2); font-weight: bold; flex-shrink: 0; }
    .step-btn { background-color: var(--preto2); color: var(--branco); border: 1px solid var(--cinza1); border-radius: 6px; width: 36px; height: 36px; font-size: 18px; font-weight: bold; cursor: pointer; flex-shrink: 0; transition: 0.2s; }
    .step-btn:hover { filter: brightness(1.4); }
    
    input[type=number] { flex: 1; background: var(--preto2); color: var(--cinza2); border: 1px solid var(--cinza1); border-radius: 6px; padding: 8px; font-family: monospace; font-size: clamp(14px, 3vh, 18px); font-weight: bold; text-align: center; min-width: 0; }
    input[type=number]:focus { outline: none; filter: brightness(1.3); }
    input[type=number]::-webkit-inner-spin-button, input[type=number]::-webkit-outer-spin-button { -webkit-appearance: none; }

    @media (max-width: 768px) {
      body { padding: 10px; }
      .main-container { grid-template-columns: 1fr; grid-template-rows: auto; gap: 12px; }
      .graph-wrapper, .c-fisica, .c-calibra { grid-column: 1; grid-row: auto; }
      .graph-wrapper { min-height: 250px; }
    }
  </style>
</head>
<body>
  <div class="main-container">
    <div class="graph-wrapper">
      <button id="btnSave" onclick="saveData()">Salvar</button>
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
        <input type="number" min="1" max="100" id="raio" onchange="sendData()">
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
        <input type="number" step="0.1" min="1.0" max="5.0" id="fator" onchange="sendData()">
        <button class="step-btn" onclick="stepFloat('fator', 0.1)">+</button>
      </div>
    </div>
  </div>
  
  <script>
    let dataVelocidade = [];
    let dataOmega = [];
    const maxPts = 100;
    const canvas = document.getElementById('plotCanvas');
    const ctx = canvas.getContext('2d');
    
    for(let i = 0; i < maxPts; i++) {
      dataVelocidade.push(0);
      dataOmega.push(0);
    }

    function resizeCanvas() {
      setTimeout(() => {
        canvas.width = canvas.parentElement.clientWidth;
        canvas.height = canvas.parentElement.clientHeight;
        drawCanvas();
      }, 50);
    }
    
    window.addEventListener('resize', resizeCanvas);
    window.onload = function() {
      resizeCanvas();
      fetch('/status').then(res => res.json()).then(data => {
        if(document.getElementById('raio')) document.getElementById('raio').value = data.raioRotor;
        if(document.getElementById('imas')) document.getElementById('imas').value = data.numImas;
        if(document.getElementById('fator')) document.getElementById('fator').value = data.fatorCopo.toFixed(1);
      });
    };

    function stepValue(id, delta) {
      let el = document.getElementById(id);
      let val = (parseInt(el.value) || 0) + delta;
      val = Math.max(parseInt(el.min), Math.min(parseInt(el.max), val));
      el.value = val;
      sendData();
    }

    function stepFloat(id, delta) {
      let el = document.getElementById(id);
      let val = (parseFloat(el.value) || 0.0) + delta;
      val = Math.max(parseFloat(el.min), Math.min(parseFloat(el.max), val));
      el.value = val.toFixed(1);
      sendData();
    }

    function sendData() {
      let r = document.getElementById('raio').value;
      let i = document.getElementById('imas').value;
      let f = document.getElementById('fator').value;
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

    setInterval(function() {
      fetch('/dados').then(res => res.json()).then(data => {
        document.getElementById('velHtml').innerText = data.v.toFixed(1);
        document.getElementById('omegaHtml').innerText = data.w.toFixed(2);
        
        dataVelocidade.push(data.v);
        dataOmega.push(data.w);
        
        if(dataVelocidade.length > maxPts) {
          dataVelocidade.shift();
          dataOmega.shift();
        }
        drawCanvas();
      });
    }, 500);

    function drawCanvas() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      
      // Escala baseada no maior valor entre as duas curvas para mantê-las proporcionais
      let maxGlobal = Math.max(...dataVelocidade, ...dataOmega, 5);
      let scale = canvas.height / (maxGlobal * 1.2);
      let step = canvas.width / (maxPts - 1);
      
      // Desenha a curva da Velocidade Angular (rad/s) - Ciano
      ctx.setLineDash([]); ctx.strokeStyle = '#ccc'; ctx.lineWidth = 2; ctx.beginPath();
      dataOmega.forEach((w, i) => {
        let x = i * step; let y = canvas.height - (w * scale);
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      });
      ctx.stroke();

      // Desenha a curva da Velocidade do Vento (m/s) - Laranja
      ctx.strokeStyle = '#ff9900'; ctx.lineWidth = 3; ctx.beginPath();
      ctx.moveTo(0, canvas.height);
      dataVelocidade.forEach((v, i) => {
        let x = i * step; let y = canvas.height - (v * scale);
        ctx.lineTo(x, y);
      });
      ctx.lineTo(canvas.width, canvas.height);
      ctx.closePath();
      ctx.fill();
      ctx.stroke();
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
  // pinMode(PINO_SENSOR, INPUT_PULLUP); // Sensor Hall 
  // attachInterrupt(digitalPinToInterrupt(PINO_SENSOR), ISR_DetectaIma, FALLING);

  pinMode(PINO_SENSOR, INPUT_PULLDOWN); // Sensor Reed
  attachInterrupt(digitalPinToInterrupt(PINO_SENSOR), ISR_DetectaIma, RISING);

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
  
  // Cálculo da Física
  if (novoPulso) {
    noInterrupts();
    unsigned long dt = deltaTempoRaw;
    novoPulso = false;
    interrupts();

    // Período total (T) de uma volta = (tempo entre ímãs) * (numero de ímãs)
    float periodoSegundos = (dt / 1000.0) * numImas;
    
    // w = 2*PI / T
    if (periodoSegundos > 0) {
      velocidadeAngular = (2.0 * PI) / periodoSegundos;
    }

    // v = w * r * K  (convertendo r de cm para metros. Removido o * 3.6 para manter em m/s)
    velocidadeVentoMs = velocidadeAngular * (raioRotor / 100.0) * fatorCopo;
  }

  // Timeout: Se o rotor parar, a velocidade deve zerar
  if (agora - tempoUltimoPulso > 3000) { 
    velocidadeAngular = 0.0;
    velocidadeVentoMs = 0.0;
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