// Controle Musical - Filtro Passa Banda
// Hardware: ESP32 + Microfone INMP441 (Digital I2S) + Relé 3x

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <math.h>
#include <driver/i2s.h>
#include "pagina_html.h" // Interface web (index_html)

// CONFIGURAÇÃO DE REDE
const bool MODO_AUTONOMO = false; // true = cria a própria rede (uso sem a Central) | false = conecta à Central

// Rede própria (MODO_AUTONOMO = true): acesso em http://192.168.4.1
const String ssidAP       = "Controle Musical";
const char*  senhaAP      = "123456789"; // Mínimo de 8 caracteres
const int    MAX_CLIENTES = 6;           // Celulares conectados ao mesmo tempo (o ESP32 aceita até 10)
const int    CANAL_WIFI   = 1;

// Rede da Central (MODO_AUTONOMO = false)
const char* ssid     = "Rede_Comunicacao";
const char* password = "123456789";

// PINOS DE HARDWARE
const int        PINO_I2S_WS     = 19;
const int        PINO_I2S_SD     = 32;
const int        PINO_I2S_SCK    = 18;
const i2s_port_t I2S_PORT        = I2S_NUM_0;
const int        PINO_RELE_GRAVE = 25;
const int        PINO_RELE_MEDIO = 26;
const int        PINO_RELE_AGUDO = 27;

// PARÂMETROS DE AMOSTRAGEM E TEMPORIZAÇÃO
const float         FREQUENCIA_AMOSTRAGEM = 9000.0f; // Hz — define o teto de Nyquist em 4500 Hz
const uint16_t      AMOSTRAS              = 64;      // Amostras por pacote DMA
const unsigned long BLANKING_MS           = 60;      // Janela cega após disparo de relé (evita microfonia)
const unsigned long INTERVALO_GRAFICO_MS  = 40;      // Intervalo entre envios do gráfico por SSE

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

  void configurarPassaBanda(float Fs, float f0, float Q) {
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
  void configurarBordas(float Fs, float freqMin, float freqMax) {
    float f0 = sqrtf(freqMin * freqMax);
    float Q  = f0 / (freqMax - freqMin);
    configurarPassaBanda(Fs, f0, Q);
  }

  float processar(float in) {
    float out = in * b0 + z1;
    z1 = in * b1 - out * a1 + z2;
    z2 = in * b2 - out * a2;
    return out;
  }
};

FiltroBiquad filtroGrave;
FiltroBiquad filtroMedio;
FiltroBiquad filtroAgudo;

void configurarI2S() { // CONFIGURAÇÃO DO DRIVER I2S (DMA)
  const i2s_config_t configI2S = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = (int)FREQUENCIA_AMOSTRAGEM,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT, // L/R do INMP441 em GND = esquerdo, mas o driver legado inverte os canais (esp32 v2.0.17)
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 4,
    .dma_buf_len          = AMOSTRAS,
    .use_apll             = false
  };

  const i2s_pin_config_t configPinos = {
    .bck_io_num   = PINO_I2S_SCK,
    .ws_io_num    = PINO_I2S_WS,
    .data_out_num = -1,
    .data_in_num  = PINO_I2S_SD
  };

  i2s_driver_install(I2S_PORT, &configI2S, 0, NULL);
  i2s_set_pin(I2S_PORT, &configPinos);
}

// Controle de relé. Lógica invertida: LOW = relé ligado, HIGH = relé desligado.
// O blanking global (tempoUltimaAcao) impede microfonia pelo clique do relé.
void controlarCanal(int pino, float volume, int limiar, unsigned long &tempoLigado) {
  unsigned long agora = millis();
  if (agora - tempoUltimaAcao < BLANKING_MS) return;

  bool ligado = (digitalRead(pino) == LOW);
  if (ligado) {
    if (volume < limiar && (agora - tempoLigado > (unsigned long)holdTime)) {
      digitalWrite(pino, HIGH);
      tempoUltimaAcao = agora;
    }
  } else {
    if (volume > limiar) {
      digitalWrite(pino, LOW);
      tempoLigado     = agora;
      tempoUltimaAcao = agora;
    }
  }
}

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
      http.begin("http://192.168.4.1/ping?nome=Musical");
      http.GET();
      http.end();
    }
    vTaskDelay(pdMS_TO_TICKS(INTERVALO_PING_MS));
  }
}

void conectarWiFi() { // CONEXÃO WI-FI
  if (MODO_AUTONOMO) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssidAP.c_str(), senhaAP, CANAL_WIFI, 0, MAX_CLIENTES);
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    int tentativas = 0;
    while (WiFi.status() != WL_CONNECTED && tentativas < 20) { delay(500); tentativas++; }
  }
  if (MDNS.begin("Musical")) MDNS.addService("http", "tcp", 80);
}

void setup() { // SETUP
  pinMode(PINO_RELE_GRAVE, OUTPUT); digitalWrite(PINO_RELE_GRAVE, HIGH);
  pinMode(PINO_RELE_MEDIO, OUTPUT); digitalWrite(PINO_RELE_MEDIO, HIGH);
  pinMode(PINO_RELE_AGUDO, OUTPUT); digitalWrite(PINO_RELE_AGUDO, HIGH);

  configurarI2S();

  preferences.begin("musical_cfg", false);
  limiarGlobal = preferences.getInt("limiarGlobal", 400);
  holdTime     = preferences.getInt("holdTime",     120);
  offsetGrave  = preferences.getInt("offsetGrave",  0); ganhoGrave = preferences.getInt("ganhoGrave", 1);
  offsetMedio  = preferences.getInt("offsetMedio",  0); ganhoMedio = preferences.getInt("ganhoMedio", 1);
  offsetAgudo  = preferences.getInt("offsetAgudo",  0); ganhoAgudo = preferences.getInt("ganhoAgudo", 1);

  filtroGrave.configurarBordas(FREQUENCIA_AMOSTRAGEM,  180.0f,  600.0f);  // Grave: 180 Hz a 600 Hz  (ref. original: 80–500 Hz)
  filtroMedio.configurarBordas(FREQUENCIA_AMOSTRAGEM,  600.0f, 1500.0f);  // Médio: 600 Hz a 1500 Hz
  filtroAgudo.configurarBordas(FREQUENCIA_AMOSTRAGEM, 1500.0f, 4400.0f);  // Agudo: 1500 Hz a 4400 Hz

  conectarWiFi();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", index_html);
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"limiarGlobal\":" + String(limiarGlobal) + ",\"holdTime\":"    + String(holdTime)   +
                  ",\"offsetGrave\":"  + String(offsetGrave)  + ",\"ganhoGrave\":"  + String(ganhoGrave) +
                  ",\"offsetMedio\":"  + String(offsetMedio)  + ",\"ganhoMedio\":"  + String(ganhoMedio) +
                  ",\"offsetAgudo\":"  + String(offsetAgudo)  + ",\"ganhoAgudo\":"  + String(ganhoAgudo) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Mesmas faixas dos campos da página: o que chega fora delas é limitado
    if (request->hasParam("limiarGlobal")) limiarGlobal = constrain(request->getParam("limiarGlobal")->value().toInt(),   10, 2000);
    if (request->hasParam("holdTime"))     holdTime     = constrain(request->getParam("holdTime")->value().toInt(),      100, 1000);
    if (request->hasParam("offsetGrave"))  offsetGrave  = constrain(request->getParam("offsetGrave")->value().toInt(), -500,  100);
    if (request->hasParam("ganhoGrave"))   ganhoGrave   = constrain(request->getParam("ganhoGrave")->value().toInt(),     1,   50);
    if (request->hasParam("offsetMedio"))  offsetMedio  = constrain(request->getParam("offsetMedio")->value().toInt(), -500,  100);
    if (request->hasParam("ganhoMedio"))   ganhoMedio   = constrain(request->getParam("ganhoMedio")->value().toInt(),     1,   50);
    if (request->hasParam("offsetAgudo"))  offsetAgudo  = constrain(request->getParam("offsetAgudo")->value().toInt(), -500,  100);
    if (request->hasParam("ganhoAgudo"))   ganhoAgudo   = constrain(request->getParam("ganhoAgudo")->value().toInt(),     1,   50);
    request->send(200, "text/plain", "OK");
  });

  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    preferences.putInt("limiarGlobal", limiarGlobal); preferences.putInt("holdTime",   holdTime);
    preferences.putInt("offsetGrave",  offsetGrave);  preferences.putInt("ganhoGrave", ganhoGrave);
    preferences.putInt("offsetMedio",  offsetMedio);  preferences.putInt("ganhoMedio", ganhoMedio);
    preferences.putInt("offsetAgudo",  offsetAgudo);  preferences.putInt("ganhoAgudo", ganhoAgudo);
    request->send(200, "text/plain", "SALVO");
  });

  server.addHandler(&events);
  server.begin();

  // Sem a Central não há a quem enviar o ping
  if (!MODO_AUTONOMO) xTaskCreate(tarefaPing, "ping", 8192, NULL, 1, NULL);
}

void loop() { // LOOP PRINCIPAL
  unsigned long agora = millis();
  static unsigned long tempoUltimoGrafico = 0;

  float picoBlocoG = 0, picoBlocoM = 0, picoBlocoA = 0;

  // --- 1. CAPTURA DIGITAL VIA DMA ---
  static int32_t amostrasBrutas[AMOSTRAS];
  size_t bytesLidos;
  i2s_read(I2S_PORT, amostrasBrutas, sizeof(amostrasBrutas), &bytesLidos, pdMS_TO_TICKS(20));
  if (bytesLidos == 0) return;

  int numAmostras = bytesLidos / sizeof(int32_t);

  // --- 2. FILTRAGEM ---
  for (int i = 0; i < numAmostras; i++) {
    // O INMP441 entrega dados de 24 bits left-justified num word de 32 bits.
    // >> 14 produz valores de 18 bits (±131.072). Os ganhos e offsets da
    // interface compensam essa escala durante a calibração.
    float amostra = (float)(amostrasBrutas[i] >> 14);

    // Todos os três filtros são passa-banda, que rejeitam DC por construção
    // matemática (b0 = -b2, b1 = 0 → resposta zero em 0 Hz).
    float sG = fabsf(filtroGrave.processar(amostra));
    float sM = fabsf(filtroMedio.processar(amostra));
    float sA = fabsf(filtroAgudo.processar(amostra));

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
  if (events.count() > 0 && (agora - tempoUltimoGrafico > INTERVALO_GRAFICO_MS)) {
    static char jsonBuf[48];
    snprintf(jsonBuf, sizeof(jsonBuf), "{\"g\":%d,\"m\":%d,\"a\":%d}",
             (int)picoGrave, (int)picoMedio, (int)picoAgudo);
    events.send(jsonBuf, "plot", agora);
    tempoUltimoGrafico = agora;
    picoGrave = picoMedio = picoAgudo = 0.0f;
  }
}
