// Controle Musical - Filtro Passa Banda
// Hardware: ESP32 + Microfone INMP441 (Digital I2S) + Relé 3x

#include <WiFi.h>
#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <math.h>
#include <driver/i2s.h>

// CONFIGURAÇÃO DE REDE
// const char* ssid     = "Rede_Comunicacao";
// const char* password = "123456789";
const char* ssid     = "ESP IoT";
const char* password = "123456789";
// const char* ssid     = "CLARO_6E22E7-IoT";
// const char* password = "Enzomorfo6927";

// PINOS DE HARDWARE - INMP441 e Relés
const int I2S_WS  = 19;
const int I2S_SD  = 32;
const int I2S_SCK = 18;
const i2s_port_t I2S_PORT = I2S_NUM_0;
const int PINO_RELE_GRAVE = 25;
const int PINO_RELE_MEDIO = 26;
const int PINO_RELE_AGUDO = 27;

// PARÂMETROS DE AMOSTRAGEM E TIMING
const float    FREQUENCIA_AMOSTRAGEM = 9000.0f;  // Hz — define o teto de Nyquist em 4500 Hz
const uint16_t AMOSTRAS              = 64;       // Amostras por pacote DMA
const int      BLANKING_MS           = 60;       // Janela cega após disparo de relé (evita microfonia)

// OBJETOS GLOBAIS
AsyncWebServer server(80);
AsyncEventSource events("/events");
Preferences preferences;

// Parâmetros ajustáveis via interface web (persistidos em flash)
int limiarGlobal;
int holdTime;
int offsetGrave, ganhoGrave;
int offsetMedio, ganhoMedio;
int offsetAgudo, ganhoAgudo;

// Estado de temporização
unsigned long tempoUltimaAcao = 0;
unsigned long tempoGrave = 0, tempoMedio = 0, tempoAgudo = 0;
unsigned long tempoUltimoGrafico  = 0;

// Picos inter-ciclos: acumulam o maior valor entre transmissões do gráfico
float picoGrave = 0, picoMedio = 0, picoAgudo = 0;

struct FiltroBiquad { // MOTOR DSP: FILTRO IIR BIQUAD
    float b0, b1, b2; // Coeficientes do numerador
    float a1, a2;     // Coeficientes do denominador (a0 normalizado para 1)
    float z1, z2;     // Linhas de atraso (memória do filtro entre amostras)

    bool calcularBase(float Fs, float f0, float Q,
                      float &w0, float &cosW0, float &alpha, float &a0) {
        if (f0 <= 0.0f || f0 >= Fs / 2.0f || Q <= 0.0f) return false;
        w0    = 2.0f * PI * (f0 / Fs);
        cosW0 = cosf(w0);
        alpha = sinf(w0) / (2.0f * Q);
        a0    = 1.0f + alpha;
        return true;
    }

    void setBandPass(float Fs, float f0, float Q) {
        float w0, cosW0, alpha, a0;
        if (!calcularBase(Fs, f0, Q, w0, cosW0, alpha, a0)) return;
        b0 =  alpha / a0;
        b1 =  0.0f;
        b2 = -alpha / a0;
        a1 = (-2.0f * cosW0) / a0;
        a2 =  (1.0f - alpha) / a0;
        z1 = z2 = 0.0f;
    }

    // Configura o passa-banda a partir das frequências de borda (Hz).
    // A frequência central é a média geométrica das bordas (escala logarítmica), e Q é derivado da largura de banda.
    void setBandPassEdges(float Fs, float f_min, float f_max) {
        float f0 = sqrtf(f_min * f_max);
        float Q  = f0 / (f_max - f_min);
        setBandPass(Fs, f0, Q);
    }

    float process(float in) {
        float out = in * b0 + z1;
        z1 = in * b1 - out * a1 + z2;
        z2 = in * b2 - out * a2;
        return out;
    }
};

FiltroBiquad filtroGrave;
FiltroBiquad filtroMedio;
FiltroBiquad filtroAgudo;

// FRONT-END HTML
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="UTF-8">
  <title>Controle Musical</title>
  <style>
    :root {--preto1: #0a0a0a; --preto2: #1a1a1a; --cinza1: #333; --cinza2: #ccc; --branco: #fff; }
    * { box-sizing: border-box; }
    html { font-size: clamp(14px, 1.2vw, 18px); }
    body { font-family: 'Segoe UI', Arial, sans-serif; background-color: var(--preto1); color: var(--cinza2); margin: 0; padding: 20px; display: flex; justify-content: center; min-height: 100vh;}

    /* ── Malha CSS Grid ── */
    .main-container { 
      display: grid; 
      grid-template-columns: 1.5fr 1fr; /* Coluna esquerda mais larga */
      grid-template-rows: 1fr 1fr 1fr;  /* 3 linhas de altura rigorosamente iguais */
      gap: 20px; 
      width: 100%; height: 100%;
      max-width: 1800px; 
      align-items: stretch;
    }

    /* ── Posicionamento na Grade ── */
    .graph-wrapper {
      grid-column: 1;
      grid-row: 1 / 3;
      position: relative; background: var(--preto1); border: 1px solid var(--cinza1); border-radius: 12px; overflow: hidden; display: flex; flex-direction: column; min-height: 250px;
    }
    .c-master { grid-column: 1; grid-row: 3; }
    .c-grave  { grid-column: 2; grid-row: 1; }
    .c-medio  { grid-column: 2; grid-row: 2; }
    .c-agudo  { grid-column: 2; grid-row: 3; }

    /* ── Gráfico ── */
    canvas { display: block; width: 100%; flex-grow: 1; }
    #btnSave { position: absolute; top: 14px; left: 14px; z-index: 10; padding: 10px 16px; font-size: 14px; font-weight: bold; background-color: var(--preto2); color: var(--branco); border: 1px solid var(--cinza1); border-radius: 6px; cursor: pointer; transition: 0.2s; }
    #btnSave:hover { filter: brightness(1.4); }
    .btn-saved { background-color: #4CAF50 !important; border-color: #4CAF50 !important; color: var(--preto1) !important; }

    /* ── Cards e Controles ── */
    .card { background-color: var(--preto2); padding: 18px 20px; border-radius: 10px; border-left: 5px solid; box-shadow: 0 4px 10px rgba(0,0,0,0.4); display: flex; flex-direction: column; justify-content: center;}
    .c-master { border-color: #ddd; }
    .c-grave  { border-color: #ff4500; }
    .c-medio  { border-color: #00ff00; }
    .c-agudo  { border-color: #1e90ff; }
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
      /* Todos os itens assumem a posição natural de empilhamento */
      .graph-wrapper, .c-master, .c-grave, .c-medio, .c-agudo {
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
      <canvas id="plotCanvas"></canvas>
    </div>
    <div class="card c-master">
      <h3>GERAL</h3>
      <div class="control-row"><label>Limiar</label><button class="step-btn" onclick="stepValue('limiarGlobal', -50)">-</button><input type="number" min="10" max="2000" id="limiarGlobal" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('limiarGlobal', 50)">+</button></div>
      <div class="control-row"><label>Hold</label><button class="step-btn" onclick="stepValue('holdTime', -50)">-</button><input type="number" min="100" max="1000" id="holdTime" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('holdTime', 50)">+</button></div>
    </div>
    <div class="card c-grave">
      <h3>GRAVE</h3>
      <div class="control-row"><label>Offset</label><button class="step-btn" onclick="stepValue('offsetGrave', -20)">-</button><input type="number" min="-500" max="100" id="offsetGrave" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('offsetGrave', 20)">+</button></div>
      <div class="control-row"><label>Ganho</label><button class="step-btn" onclick="stepValue('ganhoGrave', -1)">-</button><input type="number" min="1" max="50" id="ganhoGrave" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('ganhoGrave', 1)">+</button></div>
    </div>
    <div class="card c-medio">
      <h3>MÉDIO</h3>
      <div class="control-row"><label>Offset</label><button class="step-btn" onclick="stepValue('offsetMedio', -20)">-</button><input type="number" min="-500" max="100" id="offsetMedio" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('offsetMedio', 20)">+</button></div>
      <div class="control-row"><label>Ganho</label><button class="step-btn" onclick="stepValue('ganhoMedio', -1)">-</button><input type="number" min="1" max="50" id="ganhoMedio" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('ganhoMedio', 1)">+</button></div>
    </div>
    <div class="card c-agudo">
      <h3>AGUDO</h3>
      <div class="control-row"><label>Offset</label><button class="step-btn" onclick="stepValue('offsetAgudo', -20)">-</button><input type="number" min="-500" max="100" id="offsetAgudo" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('offsetAgudo', 20)">+</button></div>
      <div class="control-row"><label>Ganho</label><button class="step-btn" onclick="stepValue('ganhoAgudo', -1)">-</button><input type="number" min="1" max="50" id="ganhoAgudo" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('ganhoAgudo', 1)">+</button></div>
    </div>
  </div>

  <script>
    let source; let dataG = [], dataM = [], dataA = [];
    const maxPts = 80; const canvas = document.getElementById('plotCanvas');
    const ctx = canvas.getContext('2d');
    for (let i = 0; i < maxPts; i++) { dataG.push(0); dataM.push(0); dataA.push(0); }
    
    function resize() { setTimeout(() => { canvas.width = canvas.parentElement.clientWidth; canvas.height = canvas.parentElement.clientHeight; draw(); }, 50); }
    window.addEventListener('resize', resize);
    window.onload = () => {
      resize();
      source = new EventSource('/events');
      source.addEventListener('plot', e => {
        let v = JSON.parse(e.data);
        dataG.push(v.g); dataM.push(v.m); dataA.push(v.a);
        if (dataG.length > maxPts) { dataG.shift(); dataM.shift(); dataA.shift(); }
        draw();
      });
      fetch('/status').then(res => res.json()).then(data => { for (let key in data) { let el = document.getElementById(key); if (el) el.value = data[key]; } });
    };
    
    function sendData(el) { let val = parseInt(el.value) || 0; val = Math.max(parseInt(el.min), Math.min(parseInt(el.max), val)); el.value = val; fetch(`/update?id=${el.id}&value=${val}`); }
    function stepValue(id, delta) { let el = document.getElementById(id); let val = (parseInt(el.value) || 0) + delta; val = Math.max(parseInt(el.min), Math.min(parseInt(el.max), val)); el.value = val; fetch(`/update?id=${el.id}&value=${val}`); }
    
    function saveData() { 
      let btn = document.getElementById('btnSave');
      fetch('/save').then(() => { btn.innerText = "Salvo!"; btn.classList.add('btn-saved'); setTimeout(() => { btn.innerText = "Salvar"; btn.classList.remove('btn-saved'); }, 2000); });
    }
    
    function draw() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      let limEl = document.getElementById('limiarGlobal');
      let lim = limEl ? parseInt(limEl.value) : 400;
      let scale = canvas.height / (lim * 1.8);
      let yLim = canvas.height - (lim * scale);
      ctx.setLineDash([5, 5]); ctx.strokeStyle = '#555'; ctx.beginPath(); ctx.moveTo(0, yLim); ctx.lineTo(canvas.width, yLim); ctx.stroke(); ctx.setLineDash([]);
      renderLine(dataG, '#ff4500', scale); renderLine(dataM, '#00ff00', scale); renderLine(dataA, '#1e90ff', scale);
    }
    
    function renderLine(arr, col, sc) {
      if (arr.length < 2) return;
      ctx.strokeStyle = col; ctx.lineWidth = 2; ctx.beginPath();
      let step = canvas.width / (maxPts - 1);
      arr.forEach((v, i) => { let x = i * step; let y = canvas.height - (v * sc); if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y); });
      ctx.stroke();
    }
  </script>
</body>
</html>
)rawliteral";

void setupI2S() { // CONFIGURAÇÃO DO DRIVER I2S (DMA)
  const i2s_config_t i2s_config = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = (int)FREQUENCIA_AMOSTRAGEM,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 4,
    .dma_buf_len          = AMOSTRAS,
    .use_apll             = false
  };

  const i2s_pin_config_t pin_config = {
    .bck_io_num   = I2S_SCK,
    .ws_io_num    = I2S_WS,
    .data_out_num = -1,
    .data_in_num  = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
}

void conectarWiFi() { // CONEXÃO WI-FI
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) { delay(500); tentativas++; }
  if (MDNS.begin("Musical")) { MDNS.addService("http", "tcp", 80); }
}

void setup() { // SETUP
  pinMode(PINO_RELE_GRAVE, OUTPUT); digitalWrite(PINO_RELE_GRAVE, HIGH);
  pinMode(PINO_RELE_MEDIO, OUTPUT); digitalWrite(PINO_RELE_MEDIO, HIGH);
  pinMode(PINO_RELE_AGUDO, OUTPUT); digitalWrite(PINO_RELE_AGUDO, HIGH);

  setupI2S();

  preferences.begin("dj_config", false);
  limiarGlobal = preferences.getInt("lim",   400);
  holdTime     = preferences.getInt("hold",  120);
  offsetGrave  = preferences.getInt("offG",    0); ganhoGrave = preferences.getInt("ganG", 1);
  offsetMedio  = preferences.getInt("offM",    0); ganhoMedio = preferences.getInt("ganM", 1);
  offsetAgudo  = preferences.getInt("offA",    0); ganhoAgudo = preferences.getInt("ganA", 1);

  filtroGrave.setBandPassEdges(FREQUENCIA_AMOSTRAGEM,  180.0f,  600.0f);  // Grave: 180 Hz a 600 Hz  (ref. original: 80–500 Hz)
  filtroMedio.setBandPassEdges(FREQUENCIA_AMOSTRAGEM,  600.0f, 1500.0f);  // Médio: 600 Hz a 1500 Hz
  filtroAgudo.setBandPassEdges(FREQUENCIA_AMOSTRAGEM, 1500.0f, 4400.0f);  // Agudo: 1500 Hz a 4400 Hz

  conectarWiFi();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { request->send_P(200, "text/html", index_html); });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"limiarGlobal\":" + String(limiarGlobal) + ",\"holdTime\":"    + String(holdTime)   +
                  ",\"offsetGrave\":"  + String(offsetGrave)  + ",\"ganhoGrave\":"  + String(ganhoGrave) +
                  ",\"offsetMedio\":"  + String(offsetMedio)  + ",\"ganhoMedio\":"  + String(ganhoMedio) +
                  ",\"offsetAgudo\":"  + String(offsetAgudo)  + ",\"ganhoAgudo\":"  + String(ganhoAgudo) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("id") && request->hasParam("value")) {
      String id  = request->getParam("id")->value();
      int    val = request->getParam("value")->value().toInt();
      if      (id == "limiarGlobal") { limiarGlobal = val; }
      else if (id == "holdTime")     { holdTime     = val; }
      else if (id == "offsetGrave")  { offsetGrave  = val; }
      else if (id == "ganhoGrave")   { ganhoGrave   = val; }
      else if (id == "offsetMedio")  { offsetMedio  = val; }
      else if (id == "ganhoMedio")   { ganhoMedio   = val; }
      else if (id == "offsetAgudo")  { offsetAgudo  = val; }
      else if (id == "ganhoAgudo")   { ganhoAgudo   = val; }
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    preferences.putInt("lim",  limiarGlobal); preferences.putInt("hold", holdTime);
    preferences.putInt("offG", offsetGrave);  preferences.putInt("ganG", ganhoGrave);
    preferences.putInt("offM", offsetMedio);  preferences.putInt("ganM", ganhoMedio);
    preferences.putInt("offA", offsetAgudo);  preferences.putInt("ganA", ganhoAgudo);
    request->send(200, "text/plain", "SALVO");
  });

  server.addHandler(&events);
  server.begin();
}

// CONTROLE DE RELÉ
// Lógica invertida: LOW = relé ligado, HIGH = relé desligado.
// O blanking global (tempoUltimaAcao) impede microfonia pelo clique do relé.
void controlarCanal(int pino, float volume, int limiar, unsigned long &lastOn) {
  unsigned long agora = millis();
  if (agora - tempoUltimaAcao < BLANKING_MS) return;

  bool ligado = (digitalRead(pino) == LOW);
  if (ligado) {
    if (volume < limiar && (agora - lastOn > (unsigned long)holdTime)) {
      digitalWrite(pino, HIGH);
      tempoUltimaAcao = agora;
    }
  } else {
    if (volume > limiar) {
      digitalWrite(pino, LOW);
      lastOn         = agora;
      tempoUltimaAcao = agora;
    }
  }
}

void loop() { // LOOP PRINCIPAL
  unsigned long agora = millis();

  float picoBlocoG = 0, picoBlocoM = 0, picoBlocoA = 0;

  // --- 1. CAPTURA DIGITAL VIA DMA ---
  static int32_t amostrasBrutas[AMOSTRAS];
  size_t bytesLidos;
  i2s_read(I2S_PORT, amostrasBrutas, sizeof(amostrasBrutas), &bytesLidos, pdMS_TO_TICKS(20));
  if (bytesLidos == 0) return;

  // Serial.begin(115200);
  // // Serial.println(bytesLidos);

  // // TESTE DE DIAGNÓSTICO RIGOROSO:
  // Serial.println(amostrasBrutas[0]);
  // return; // Interrompe o resto do loop. Esqueça relés e filtros por enquanto.

  int numAmostras = bytesLidos / sizeof(int32_t);

  // --- 2. FILTRAGEM ---
  for (int i = 0; i < numAmostras; i++) {
    // O INMP441 entrega dados de 24 bits left-justified num word de 32 bits.
    // >> 14 produz valores de 18 bits (±131.072). Os ganhos e offsets da
    // interface compensam essa escala durante a calibração.
    float raw = (float)(amostrasBrutas[i] >> 14);

    // Todos os três filtros são passa-banda, que rejeitam DC por construção
    // matemática (b0 = -b2, b1 = 0 → resposta zero em 0 Hz).
    float sG = fabsf(filtroGrave.process(raw));
    float sM = fabsf(filtroMedio.process(raw));
    float sA = fabsf(filtroAgudo.process(raw));

    if (sG > picoBlocoG) picoBlocoG = sG;
    if (sM > picoBlocoM) picoBlocoM = sM;
    if (sA > picoBlocoA) picoBlocoA = sA;
  }

  // --- 3. GANHO E OFFSET ---
  float volGrave = constrain((picoBlocoG * ganhoGrave) + offsetGrave, 0.0f, 2000.0f);
  float volMedio = constrain((picoBlocoM * ganhoMedio) + offsetMedio, 0.0f, 2000.0f);
  float volAgudo = constrain((picoBlocoA * ganhoAgudo) + offsetAgudo, 0.0f, 2000.0f);

  if (volGrave > picoGrave) picoGrave = volGrave;
  if (volMedio > picoMedio) picoMedio = volMedio;
  if (volAgudo > picoAgudo) picoAgudo = volAgudo;

  // --- 4. CONTROLE DOS RELÉS ---
  controlarCanal(PINO_RELE_GRAVE, volGrave, limiarGlobal, tempoGrave);
  controlarCanal(PINO_RELE_MEDIO, volMedio, limiarGlobal, tempoMedio);
  controlarCanal(PINO_RELE_AGUDO, volAgudo, limiarGlobal, tempoAgudo);

  // --- 5. TRANSMISSÃO DO GRÁFICO (SSE) ---
  if (events.count() > 0 && (agora - tempoUltimoGrafico > 40)) {
    static char jsonBuf[48];
    snprintf(jsonBuf, sizeof(jsonBuf), "{\"g\":%d,\"m\":%d,\"a\":%d}",
            (int)picoGrave, (int)picoMedio, (int)picoAgudo);
    events.send(jsonBuf, "plot", agora);
    tempoUltimoGrafico = agora;
    picoGrave = picoMedio = picoAgudo = 0.0f;
  }

  // --- 6. PING PARA A REDE DE COMUNICAÇÃO ---
  static unsigned long lastPing = 0;
  if (agora - lastPing > 30000) { // era 5000
    lastPing = agora;
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.setConnectTimeout(1000);
      http.setTimeout(1000);
      http.begin("http://192.168.4.1/ping?nome=Musical");
      http.GET();
      http.end();
    }
  }
}
