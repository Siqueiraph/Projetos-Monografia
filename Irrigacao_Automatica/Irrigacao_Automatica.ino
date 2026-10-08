// Irrigação Automática - Monitor de Umidade
// Hardware: ESP32 + Divisor de Tensão + Relé

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>

// CONFIGURAÇÃO DE REDE
const char* ssid     = "Rede_Comunicacao";
const char* password = "123456789";

// PINOS DE HARDWARE
const int PINO_SENSOR = 34; // ADC — leitura do divisor de tensão (só entrada)
const int PINO_RELE   = 26; // Saída digital — aciona a bomba via relé

// PARÂMETROS DE TEMPORIZAÇÃO
const unsigned long COOLDOWN_REGA_MS = 60000UL; // 60 segundos

// OBJETOS GLOBAIS
AsyncWebServer server(80);
Preferences preferences;

// Parâmetros ajustáveis via interface web (persistidos em flash pelo /save)
int valSeco      = 500;
int valUmido     = 3500;
int limiteLigar  = 30;
int tempoMaxRega = 10; // segundos

// Estado da bomba
bool          bombaLigada   = false;
unsigned long inicioRega    = 0;
unsigned long fimUltimaRega = 0;

// Leitura atual — atualizada no loop(), lida pelas rotas HTTP
int umidade = 0;

// FRONT-END HTML
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="UTF-8">
  <title>Irrigação Automática</title>
  <style>
    :root {--destaque: #00ff00; --preto1: #0a0a0a; --preto2: #1a1a1a; --cinza1: #333; --cinza2: #ccc; --branco: #fff;}
    * { box-sizing: border-box; }
    html { font-size: clamp(14px, 1.2vw, 18px); }
    body { font-family: 'Segoe UI', Arial, sans-serif; background-color: var(--preto1); color: var(--cinza2); margin: 0; padding: 20px; display: flex; justify-content: center; min-height: 100vh;}

    /* ── Malha CSS Grid ── */
    .main-container { 
      display: grid; 
      grid-template-columns: 1.5fr 1fr; 
      grid-template-rows: 1fr 1fr 1fr;  /* 3 linhas de altura rigorosamente iguais */
      gap: 20px; 
      width: 100%; height: 100%;
      max-width: 1800px; 
      align-items: stretch;
    }

    /* ── Posicionamento na Grade ── */
    .graph-wrapper {
      grid-column: 1;
      grid-row: 1 / 3; /* O gráfico ocupa da linha 1 até o final da linha 2 */
      position: relative; background: var(--preto1); border: 1px solid var(--cinza1); border-radius: 12px; overflow: hidden; display: flex; flex-direction: column; min-height: 250px;
    }
    .c-calibra { grid-column: 2; grid-row: 1 / 2; }
    .c-rega    { grid-column: 2; grid-row: 2 / 3; }

    /* ── Elementos do Gráfico ── */
    canvas { display: block; width: 100%; flex-grow: 1; }
    #humOverlay { position: absolute; top: 14px; right: 20px; font-size: clamp(36px, 5vh, 50px); font-weight: bold; color: var(--destaque); text-shadow: 2px 2px 6px var(--preto1); z-index: 10; font-family: monospace; }
    #btnSave { position: absolute; top: 14px; left: 14px; z-index: 10; padding: 10px 16px; font-size: 14px; font-weight: bold; background-color: var(--preto2); color: var(--branco); border: 1px solid var(--cinza1); border-radius: 6px; cursor: pointer; transition: 0.2s; }
    #btnSave:hover { filter: brightness(1.4); }
    .btn-saved { background-color: #4CAF50 !important; border-color: #4CAF50 !important; color: var(--preto1) !important; }

    /* ── Cards e Controles ── */
    .card { background-color: var(--preto2); padding: 18px 20px; border-radius: 10px; border-left: 5px solid var(--destaque); box-shadow: 0 4px 10px rgba(0,0,0,0.4); display: flex; flex-direction: column; justify-content: center;}
    h3 { margin: 0 0 16px 0; font-size: clamp(14px, 3vh, 22px); color: var(--cinza2); border-bottom: 1px solid var(--cinza1); padding-bottom: 8px; letter-spacing: 1px;}
  
    .control-row { display: flex; align-items: center; gap: 10px; margin-top: 8px; }
    .control-row label { width: 60px; font-size: clamp(12px, 2.5vh, 16px); color: var(--cinza2); font-weight: bold; flex-shrink: 0; }
    .step-btn { background-color: var(--preto2); color: var(--branco); border: 1px solid var(--cinza1); border-radius: 6px; width: 36px; height: 36px; font-size: 18px; font-weight: bold; cursor: pointer; flex-shrink: 0; transition: 0.2s; }
    .step-btn:hover { filter: brightness(1.4); }
    
    input[type=number] { flex: 1; background: var(--preto2); color: var(--cinza2); border: 1px solid var(--cinza1); border-radius: 6px; padding: 8px; font-family: monospace; font-size: clamp(14px, 3vh, 18px); font-weight: bold; text-align: center; min-width: 0; }
    input[type=number]:focus { outline: none; filter: brightness(1.3); }
    input[type=number]::-webkit-inner-spin-button, input[type=number]::-webkit-outer-spin-button { -webkit-appearance: none; }
    input[type=number] { -moz-appearance: textfield; }

    /* ── Mobile ── */
    @media (max-width: 768px) {
      body { padding: 10px; }
      .main-container { 
        grid-template-columns: 1fr; /* Coluna única no mobile */
        grid-template-rows: auto;   /* Alturas baseadas no conteúdo */
        gap: 12px; 
      }
      /* Elementos assumem empilhamento natural */
      .graph-wrapper, .c-calibra, .c-rega {
        grid-column: 1;
        grid-row: auto;
      }
      .graph-wrapper { min-height: 250px; }
    }
  </style>
</head>
<body>
  <div class="main-container">
    <div class="graph-wrapper">
      <button id="btnSave" onclick="saveData()">Salvar</button>
      <div id="humOverlay">0.0%</div>
      <canvas id="plotCanvas"></canvas>
    </div>
    <div class="card c-calibra">
      <h3>CALIBRAÇÃO</h3>
      <div class="control-row">
        <label>Seco</label>
        <button class="step-btn" onclick="stepValue('vSeco', -50)">-</button>
        <input type="number" min="0" max="4095" id="vSeco" onchange="sendData()">
        <button class="step-btn" onclick="stepValue('vSeco', 50)">+</button>
      </div>
      <div class="control-row">
        <label>Úmido</label>
        <button class="step-btn" onclick="stepValue('vUmido', -50)">-</button>
        <input type="number" min="0" max="4095" id="vUmido" onchange="sendData()">
        <button class="step-btn" onclick="stepValue('vUmido', 50)">+</button>
      </div>
    </div>
    <div class="card c-rega">
      <h3>ACIONAMENTO</h3>
      <div class="control-row">
        <label>Limiar</label>
        <button class="step-btn" onclick="stepValue('limite', -1)">-</button>
        <input type="number" min="0" max="100" id="limite" onchange="sendData()">
        <button class="step-btn" onclick="stepValue('limite', 1)">+</button>
      </div>
      <div class="control-row">
        <label>Tempo</label>
        <button class="step-btn" onclick="stepValue('tempo', -1)">-</button>
        <input type="number" min="1" max="120" id="tempo" onchange="sendData()">
        <button class="step-btn" onclick="stepValue('tempo', 1)">+</button>
      </div>
    </div>
  </div>
  
  <script>
    let dataUmidade = [];
    const maxPts = 100;
    const canvas = document.getElementById('plotCanvas');
    const ctx = canvas.getContext('2d');
    for(let i = 0; i < maxPts; i++) dataUmidade.push(0);

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
        Object.keys(data).forEach(key => {
          let el = document.getElementById(key);
          if(el) { el.value = data[key]; }
        });
      });
    };

    function stepValue(id, delta) {
      let el = document.getElementById(id);
      let val = (parseInt(el.value) || 0) + delta;
      val = Math.max(parseInt(el.min), Math.min(parseInt(el.max), val));
      el.value = val;
      sendData();
    }

    function sendData() {
      let s = document.getElementById('vSeco').value;
      let u = document.getElementById('vUmido').value;
      let l = document.getElementById('limite').value;
      let t = document.getElementById('tempo').value;
      fetch(`/set?seco=${s}&umido=${u}&limite=${l}&tempo=${t}`);
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
        document.getElementById('humOverlay').innerText = data.umidade + "%";
        dataUmidade.push(data.umidade);
        if(dataUmidade.length > maxPts) dataUmidade.shift();
        drawCanvas();
      });
    }, 1000);

    function drawCanvas() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      let limEl = document.getElementById('limite');
      let lim = limEl ? parseInt(limEl.value) : 30;
      let scale = canvas.height / 100;
      let yLim = canvas.height - (lim * scale);
      ctx.setLineDash([5, 5]); ctx.strokeStyle = '#555'; ctx.beginPath(); ctx.moveTo(0, yLim); ctx.lineTo(canvas.width, yLim); ctx.stroke();
      ctx.setLineDash([]); ctx.strokeStyle = '#00ff00'; ctx.lineWidth = 3; ctx.beginPath();
      let step = canvas.width / (maxPts - 1);
      dataUmidade.forEach((v, i) => {
        let x = i * step; let y = canvas.height - (v * scale);
        if(i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      });
      ctx.stroke();
    }
  </script>
</body>
</html>
)rawliteral";

// PING PARA A CENTRAL DE COMUNICAÇÃO
const unsigned long INTERVALO_PING_MS = 5000; // A Central considera o módulo inativo após 15 s sem ping

// Ping para o Módulo Central de Comunicação
// Roda em uma tarefa própria do FreeRTOS para que a espera pela resposta não bloqueie o loop principal
void tarefaPing(void *parametro) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.setConnectTimeout(1000);
      http.setTimeout(1000);
      http.begin("http://192.168.4.1/ping?nome=Irrigacao");
      http.GET();
      http.end();
    }
    vTaskDelay(pdMS_TO_TICKS(INTERVALO_PING_MS));
  }
}

void conectarWiFi() { // CONEXÃO WI-FI
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) { delay(500); tentativas++; }
  if (MDNS.begin("Irrigacao")) MDNS.addService("http", "tcp", 80);
}

void setup() { // SETUP
  pinMode(PINO_RELE, OUTPUT);
  digitalWrite(PINO_RELE, LOW);

  preferences.begin("irrig_config", false);
  valSeco      = preferences.getInt("seco",   500);
  valUmido     = preferences.getInt("umido",  3500);
  limiteLigar  = preferences.getInt("limite", 30);
  tempoMaxRega = preferences.getInt("timer",  10);

  conectarWiFi();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { request->send_P(200, "text/html", index_html); });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"vSeco\":"  + String(valSeco)      +
                  ",\"vUmido\":" + String(valUmido)     +
                  ",\"limite\":" + String(limiteLigar)  +
                  ",\"tempo\":"  + String(tempoMaxRega) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/dados", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"umidade\":"     + String(umidade) +
                  ",\"statusBomba\":" + String(bombaLigada)  + "}";
    request->send(200, "application/json", json);
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
    int novoSeco  = request->hasParam("seco")  ? request->getParam("seco")->value().toInt()  : valSeco;
    int novoUmido = request->hasParam("umido") ? request->getParam("umido")->value().toInt() : valUmido;

    // Validação cruzada: garante que seco < úmido
    if (novoSeco < novoUmido) { valSeco = novoSeco; valUmido = novoUmido; }

    if (request->hasParam("limite")) limiteLigar  = request->getParam("limite")->value().toInt();
    if (request->hasParam("tempo"))  tempoMaxRega = request->getParam("tempo")->value().toInt();
    request->send(200, "text/plain", "OK");
  });

  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    preferences.putInt("seco",   valSeco);
    preferences.putInt("umido",  valUmido);
    preferences.putInt("limite", limiteLigar);
    preferences.putInt("timer",  tempoMaxRega);
    request->send(200, "text/plain", "SALVO");
  });

  server.begin();

  xTaskCreate(tarefaPing, "ping", 8192, NULL, 1, NULL);
}

void loop() { // LOOP PRINCIPAL
  unsigned long agora = millis();

  // --- 1. LEITURA E CONVERSÃO DO SENSOR ---
  int leitura = analogRead(PINO_SENSOR);
  umidade = constrain(map(leitura, valSeco, valUmido, 0, 100), 0, 100);

  // --- 2. CONTROLE DA BOMBA ---
  if (!bombaLigada) {
    bool cooldownEncerrado = (fimUltimaRega == 0) || (agora - fimUltimaRega >= COOLDOWN_REGA_MS);
    if (umidade < limiteLigar && cooldownEncerrado) {
      bombaLigada = true;
      inicioRega  = agora;
      digitalWrite(PINO_RELE, HIGH);
    }
  } else {
    if (agora - inicioRega >= (unsigned long)(tempoMaxRega * 1000)) {
      bombaLigada   = false;
      fimUltimaRega = agora;
      digitalWrite(PINO_RELE, LOW);
    }
  }
}
