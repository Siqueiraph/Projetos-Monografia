// Balança Inercial - Medidor de Massa em Microgravidade
// Hardware: ESP32 + Sensor Ultrassônico HC-SR04

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <math.h>

// CONFIGURAÇÃO DE REDE
const char* ssid     = "Rede_Comunicacao";
const char* password = "123456789";

// PINOS DE HARDWARE
const int PINO_TRIG = 5;
const int PINO_ECHO = 18;

// OBJETOS GLOBAIS
AsyncWebServer server(80);
Preferences preferences;

// Parâmetros ajustáveis via interface web
float constanteK       = 15.0; // Constante elástica (N/m)
int   numCiclos        = 3;    // Quantidade de ciclos para média do período
int   intervaloLeitura = 40;   // Intervalo de amostragem em ms

// Variáveis de Estado do Sensor e Algoritmo
float distFiltrada   = 0;
float picoTemp       = 0;
float fundoTemp      = 1000;
bool  subindo        = true;
int   contagemCiclos = 0;
unsigned long tempoUltimoPico = 0;
unsigned long somaPeriodos    = 0;

// Resultados para o Front-End
float distancia = 0.0;
float periodo   = 0.0;
float massa = 0.0; // Em gramas

// FRONT-END HTML
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="UTF-8">
  <title>Balança Inercial</title>
  <style>
    :root {--destaque: #9c27b0; --preto1: #0a0a0a; --preto2: #1a1a1a; --cinza1: #333; --cinza2: #ccc; --branco: #fff;}
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
    .c-sensor  { grid-column: 2; grid-row: 2 / 3; }

    canvas { display: block; width: 100%; flex-grow: 1; }
    #dataOverlay { position: absolute; top: 14px; right: 20px; font-size: clamp(30px, 4vh, 40px); font-weight: bold; text-shadow: 2px 2px 6px var(--preto1); z-index: 10; font-family: monospace; text-align: right; line-height: 1.1;}
    .data-T { color: var(--destaque); }
    .data-m { color: var(--cinza2); }
    
    #btnSave { position: absolute; top: 14px; left: 14px; z-index: 10; padding: 10px 16px; font-size: 14px; font-weight: bold; background-color: var(--preto2); color: var(--branco); border: 1px solid var(--cinza1); border-radius: 6px; cursor: pointer; transition: 0.2s; }
    #btnSave:hover { filter: brightness(1.4); }
    .btn-saved { background-color: #4CAF50 !important; border-color: #4CAF50 !important; color: var(--preto1) !important; }

    .card { background-color: var(--preto2); padding: 18px 20px; border-radius: 10px; border-left: 5px solid var(--destaque); box-shadow: 0 4px 10px rgba(0,0,0,0.4); display: flex; flex-direction: column; justify-content: center;}
    h3 { margin: 0 0 16px 0; font-size: clamp(14px, 3vh, 22px); color: var(--cinza2); border-bottom: 1px solid var(--cinza1); padding-bottom: 8px; letter-spacing: 1px;}
  
    .control-row { display: flex; align-items: center; gap: 10px; margin-top: 8px; }
    .control-row label { width: 80px; font-size: clamp(12px, 2.5vh, 16px); color: var(--cinza2); font-weight: bold; flex-shrink: 0; }
    .step-btn { background-color: var(--preto2); color: var(--branco); border: 1px solid var(--cinza1); border-radius: 6px; width: 36px; height: 36px; font-size: 18px; font-weight: bold; cursor: pointer; flex-shrink: 0; transition: 0.2s; }
    .step-btn:hover { filter: brightness(1.4); }
    
    input[type=number] { flex: 1; background: var(--preto2); color: var(--cinza2); border: 1px solid var(--cinza1); border-radius: 6px; padding: 8px; font-family: monospace; font-size: clamp(14px, 3vh, 18px); font-weight: bold; text-align: center; min-width: 0; }
    input[type=number]:focus { outline: none; filter: brightness(1.3); }
    input[type=number]::-webkit-inner-spin-button, input[type=number]::-webkit-outer-spin-button { -webkit-appearance: none; }

    @media (max-width: 768px) {
      body { padding: 10px; }
      .main-container { grid-template-columns: 1fr; grid-template-rows: auto; gap: 12px; }
      .graph-wrapper, .c-fisica, .c-sensor { grid-column: 1; grid-row: auto; }
      .graph-wrapper { min-height: 250px; }
    }
  </style>
</head>
<body>
  <div class="main-container">
    <div class="graph-wrapper">
      <button id="btnSave" onclick="saveData()">Salvar</button>
      <div id="dataOverlay">
        <span class="data-T">T: <span id="perHtml">0.0</span> s</span><br>
        <span class="data-m">m: <span id="masHtml">0.0</span> g</span>
      </div>
      <canvas id="plotCanvas"></canvas>
    </div>
    <div class="card c-fisica">
      <h3>FÍSICA O.H.S.</h3>
      <div class="control-row">
        <label>k (N/m)</label>
        <button class="step-btn" onclick="stepFloat('k', -0.1)">-</button>
        <input type="number" step="0.1" min="0.5" max="100.0" id="k" onchange="sendData()">
        <button class="step-btn" onclick="stepFloat('k', 0.1)">+</button>
      </div>
    </div>
    <div class="card c-sensor">
      <h3>AMOSTRAGEM</h3>
      <div class="control-row">
        <label>Ciclos</label>
        <button class="step-btn" onclick="stepValue('ciclos', -1)">-</button>
        <input type="number" min="1" max="10" id="ciclos" onchange="sendData()">
        <button class="step-btn" onclick="stepValue('ciclos', 1)">+</button>
      </div>
      <div class="control-row">
        <label>Delay (ms)</label>
        <button class="step-btn" onclick="stepValue('delay', -5)">-</button>
        <input type="number" min="20" max="200" id="delay" onchange="sendData()">
        <button class="step-btn" onclick="stepValue('delay', 5)">+</button>
      </div>
    </div>
  </div>
  
  <script>
    let dataDistancia = [];
    const maxPts = 100;
    const canvas = document.getElementById('plotCanvas');
    const ctx = canvas.getContext('2d');
    for(let i = 0; i < maxPts; i++) { dataDistancia.push(0); }

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
        if(document.getElementById('k')) document.getElementById('k').value = data.constanteK.toFixed(1);
        if(document.getElementById('ciclos')) document.getElementById('ciclos').value = data.numCiclos;
        if(document.getElementById('delay')) document.getElementById('delay').value = data.intervaloLeitura;
      });
    };

    function stepValue(id, delta) {
      let el = document.getElementById(id);
      let val = (parseInt(el.value) || 0) + delta;
      val = Math.max(parseInt(el.min), Math.min(parseInt(el.max), val));
      el.value = val; sendData();
    }

    function stepFloat(id, delta) {
      let el = document.getElementById(id);
      let val = (parseFloat(el.value) || 0.0) + delta;
      val = Math.max(parseFloat(el.min), Math.min(parseFloat(el.max), val));
      el.value = val.toFixed(1); sendData();
    }

    function sendData() {
      let k = document.getElementById('k').value;
      let c = document.getElementById('ciclos').value;
      let d = document.getElementById('delay').value;
      fetch(`/set?k=${k}&ciclos=${c}&delay=${d}`);
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
        document.getElementById('perHtml').innerText = data.T.toFixed(2);
        document.getElementById('masHtml').innerText = data.m.toFixed(1);
        
        dataDistancia.push(data.h);
        if(dataDistancia.length > maxPts) { dataDistancia.shift(); }
        drawCanvas();
      });
    }, 100);

    function drawCanvas() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      let maxGlobal = Math.max(...dataDistancia, 20); // Escala base 20cm
      let scale = canvas.height / (maxGlobal * 1.2);
      let step = canvas.width / (maxPts - 1);
      
      ctx.strokeStyle = '#9c27b0';
      ctx.lineWidth = 3;
      ctx.beginPath();
      dataDistancia.forEach((h, i) => {
        let x = i * step; let y = canvas.height - (h * scale);
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
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
      http.begin("http://192.168.4.1/ping?nome=Balanca");
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
  if (MDNS.begin("Balanca")) MDNS.addService("http", "tcp", 80);
}

void setup() { // SETUP
  pinMode(PINO_TRIG, OUTPUT);
  pinMode(PINO_ECHO, INPUT);

  preferences.begin("balanca_cfg", false);
  constanteK       = preferences.getFloat("k", 15.0);
  numCiclos        = preferences.getInt("ciclos", 3);
  intervaloLeitura = preferences.getInt("delay", 40);

  conectarWiFi();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { 
    request->send_P(200, "text/html", index_html); 
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"constanteK\":" + String(constanteK) +
                  ",\"numCiclos\":"  + String(numCiclos) +
                  ",\"intervaloLeitura\":" + String(intervaloLeitura) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/dados", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"h\":" + String(distancia) +
                  ",\"T\":" + String(periodo) +
                  ",\"m\":" + String(massa) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("k"))      constanteK = request->getParam("k")->value().toFloat();
    if (request->hasParam("ciclos")) numCiclos  = request->getParam("ciclos")->value().toInt();
    if (request->hasParam("delay"))  intervaloLeitura = request->getParam("delay")->value().toInt();
    request->send(200, "text/plain", "OK");
  });

  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    preferences.putFloat("k", constanteK);
    preferences.putInt("ciclos", numCiclos);
    preferences.putInt("delay", intervaloLeitura);
    request->send(200, "text/plain", "SALVO");
  });

  server.begin();

  xTaskCreate(tarefaPing, "ping", 8192, NULL, 1, NULL);
}

void loop() { // LOOP PRINCIPAL
  unsigned long agora = millis();
  static unsigned long tempoUltimaLeitura = 0;

  // Lógica principal restrita pelo intervalo ajustável do sensor
  if (agora - tempoUltimaLeitura >= (unsigned long)intervaloLeitura) {
    tempoUltimaLeitura = agora;

    // Disparo do HC-SR04
    digitalWrite(PINO_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PINO_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PINO_TRIG, LOW);
    
    // Timeout de 25ms previne bloqueio excessivo do ESP32 caso o eco falhe
    long duracao = pulseIn(PINO_ECHO, HIGH, 25000); 
    
    if (duracao > 0) {
      float distRaw = (duracao * 0.0343) / 2.0;
      
      // Filtro EMA (Média Móvel Exponencial) para atenuar o ruído ultrassônico
      distFiltrada = (0.3 * distRaw) + (0.7 * distFiltrada);
      distancia = distFiltrada;

      // --- ALGORITMO DETECTOR DE PICOS COM HISTERESE ---
      float threshold = 0.5; // Margem em cm para descartar ruídos minúsculos

      // Atualiza o pico máximo temporário
      if (distFiltrada > picoTemp) picoTemp = distFiltrada;
      
      // Se estava subindo, mas caiu além do threshold, então passamos por um PICO REAL
      if (subindo && (picoTemp - distFiltrada > threshold)) {
        subindo = false;
        fundoTemp = distFiltrada; // Reseta a busca pelo fundo
        
        if (tempoUltimoPico > 0) {
          unsigned long deltaT = agora - tempoUltimoPico;
          if (deltaT > 150) { // Ignora ciclos absurdamente rápidos (ruído)
            somaPeriodos += deltaT;
            contagemCiclos++;
            
            if (contagemCiclos >= numCiclos) {
              periodo = (somaPeriodos / (float)numCiclos) / 1000.0; // T em segundos
              
              // T = 2*PI * sqrt(m/k) -> m = k * (T / 2*PI)^2
              // Multiplica por 1000 para exibir em gramas
              massa = constanteK * pow(periodo / (2.0 * PI), 2) * 1000.0;
              
              somaPeriodos = 0;
              contagemCiclos = 0;
            }
          }
        }
        tempoUltimoPico = agora;
      }

      // Atualiza o fundo mínimo temporário
      if (distFiltrada < fundoTemp) fundoTemp = distFiltrada;

      // Se estava descendo, mas subiu além do threshold, preparamos para o próximo pico
      if (!subindo && (distFiltrada - fundoTemp > threshold)) {
        subindo = true;
        picoTemp = distFiltrada; // Reseta a busca pelo pico
      }
    }
  }

  // Timeout geral de oscilação: se o copo parar por muito tempo, zera
  static unsigned long timeoutParado = 0;
  if (abs(distancia - picoTemp) < 0.2 && abs(distancia - fundoTemp) < 0.2) {
    if (agora - timeoutParado > 3000) {
      periodo = 0.0;
      massa = 0.0;
    }
  } else {
    timeoutParado = agora;
  }
}