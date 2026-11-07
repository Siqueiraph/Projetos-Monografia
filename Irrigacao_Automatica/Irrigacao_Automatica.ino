// Irrigação Automática - Monitor de Umidade
// Hardware: ESP32 + Divisor de Tensão + Relé

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "pagina_html.h" // Interface web (index_html)

// CONFIGURAÇÃO DE REDE
const bool MODO_AUTONOMO = false; // true = cria a própria rede (uso sem a Central) | false = conecta à Central

// Rede própria (MODO_AUTONOMO = true): acesso em http://192.168.4.1
const String ssidAP       = "Irrigacao Automatica";
const char*  senhaAP      = "123456789"; // Mínimo de 8 caracteres
const int    MAX_CLIENTES = 6;           // Celulares conectados ao mesmo tempo (o ESP32 aceita até 10)
const int    CANAL_WIFI   = 1;

// Rede da Central (MODO_AUTONOMO = false)
const char* ssid     = "Rede_Comunicacao";
const char* password = "123456789";

// PINOS DE HARDWARE
const int PINO_SENSOR = 34; // ADC — leitura do divisor de tensão (só entrada)
const int PINO_RELE   = 26; // Saída digital — aciona a bomba via relé

// PARÂMETROS DE TEMPORIZAÇÃO
const unsigned long COOLDOWN_REGA_MS = 60000UL; // 60 segundos

// PARÂMETROS DE MEDIÇÃO E PROTEÇÃO
const int AMOSTRAS_ADC         = 16; // Leituras do ADC por medida (a média atenua o ruído)
const int MAX_REGAS_SEM_EFEITO = 3;  // Regas seguidas sem a umidade subir até bloquear a bomba (sensor solto ou fora do solo)

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
int           umidadeAntesRega = 0; // Umidade no início da última rega
int           regasSemEfeito   = 0; // Regas seguidas sem a umidade subir; ao atingir o máximo, só um reinício libera a bomba

// Leitura atual — atualizada no loop(), lida pelas rotas HTTP
int umidade = 0;

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
  if (MODO_AUTONOMO) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssidAP.c_str(), senhaAP, CANAL_WIFI, 0, MAX_CLIENTES);
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    int tentativas = 0;
    while (WiFi.status() != WL_CONNECTED && tentativas < 20) { delay(500); tentativas++; }
  }
  if (MDNS.begin("Irrigacao")) MDNS.addService("http", "tcp", 80);
}

void setup() { // SETUP
  pinMode(PINO_RELE, OUTPUT);
  digitalWrite(PINO_RELE, LOW);

  preferences.begin("irrigacao_cfg", false);
  valSeco      = preferences.getInt("seco",   500);
  valUmido     = preferences.getInt("umido",  3500);
  limiteLigar  = preferences.getInt("limite", 30);
  tempoMaxRega = preferences.getInt("tempo",  10);

  conectarWiFi();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", index_html);
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"vSeco\":"  + String(valSeco)      +
                  ",\"vUmido\":" + String(valUmido)     +
                  ",\"limite\":" + String(limiteLigar)  +
                  ",\"tempo\":"  + String(tempoMaxRega) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/dados", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"umidade\":"     + String(umidade) +
                  ",\"statusBomba\":" + String(bombaLigada) +
                  ",\"bloqueio\":"    + String(regasSemEfeito >= MAX_REGAS_SEM_EFEITO) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Mesmas faixas dos campos da página: o que chega fora delas é limitado
    int novoSeco  = request->hasParam("seco")  ? constrain(request->getParam("seco")->value().toInt(),  0, 4095) : valSeco;
    int novoUmido = request->hasParam("umido") ? constrain(request->getParam("umido")->value().toInt(), 0, 4095) : valUmido;

    // Validação cruzada: garante que seco < úmido
    if (novoSeco < novoUmido) { valSeco = novoSeco; valUmido = novoUmido; }

    if (request->hasParam("limite")) limiteLigar  = constrain(request->getParam("limite")->value().toInt(), 0, 100);
    if (request->hasParam("tempo"))  tempoMaxRega = constrain(request->getParam("tempo")->value().toInt(),  1, 120);
    request->send(200, "text/plain", "OK");
  });

  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    preferences.putInt("seco",   valSeco);
    preferences.putInt("umido",  valUmido);
    preferences.putInt("limite", limiteLigar);
    preferences.putInt("tempo",  tempoMaxRega);
    request->send(200, "text/plain", "SALVO");
  });

  server.begin();

  // Sem a Central não há a quem enviar o ping
  if (!MODO_AUTONOMO) xTaskCreate(tarefaPing, "ping", 8192, NULL, 1, NULL);
}

void loop() { // LOOP PRINCIPAL
  unsigned long agora = millis();

  // --- 1. LEITURA E CONVERSÃO DO SENSOR ---
  long soma = 0;
  for (int i = 0; i < AMOSTRAS_ADC; i++) soma += analogRead(PINO_SENSOR);
  int leitura = soma / AMOSTRAS_ADC;
  umidade = constrain(map(leitura, valSeco, valUmido, 0, 100), 0, 100);

  // --- 2. CONTROLE DA BOMBA ---
  if (!bombaLigada) {
    bool cooldownEncerrado = (fimUltimaRega == 0) || (agora - fimUltimaRega >= COOLDOWN_REGA_MS);
    if (umidade < limiteLigar && cooldownEncerrado && regasSemEfeito < MAX_REGAS_SEM_EFEITO) {
      // Se a rega anterior não elevou a umidade, o sensor pode estar solto ou fora do solo
      if (fimUltimaRega != 0 && umidade <= umidadeAntesRega) regasSemEfeito++;
      else                                                   regasSemEfeito = 0;

      if (regasSemEfeito < MAX_REGAS_SEM_EFEITO) {
        bombaLigada      = true;
        inicioRega       = agora;
        umidadeAntesRega = umidade;
        digitalWrite(PINO_RELE, HIGH);
      }
    }
  } else {
    if (agora - inicioRega >= (unsigned long)(tempoMaxRega * 1000)) {
      bombaLigada   = false;
      fimUltimaRega = agora;
      digitalWrite(PINO_RELE, LOW);
    }
  }
}
