// Balança Inercial - Medidor de Massa em Microgravidade
// Hardware: ESP32 + Sensor Ultrassônico HC-SR04

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <math.h>
#include "pagina_html.h" // Interface web (index_html)

// CONFIGURAÇÃO DE REDE
const bool MODO_AUTONOMO = false; // true = cria a própria rede (uso sem a Central) | false = conecta à Central

// Rede própria (MODO_AUTONOMO = true): acesso em http://192.168.4.1
const char* ssidAP       = "Balanca Inercial";
const char* senhaAP      = "123456789"; // Mínimo de 8 caracteres
const int   MAX_CLIENTES = 6;           // Celulares conectados ao mesmo tempo (o ESP32 aceita até 10)

// Rede da Central (MODO_AUTONOMO = false)
const char* ssid     = "Rede_Comunicacao";
const char* password = "123456789";

// PINOS DE HARDWARE
const int PINO_TRIG = 5;
const int PINO_ECHO = 18;

// PARÂMETROS DE MEDIÇÃO
const unsigned long TIMEOUT_PARADO_MS = 3000; // Sem um novo pico por esse tempo, considera a oscilação encerrada

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
  if (MODO_AUTONOMO) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssidAP, senhaAP, 1, 0, MAX_CLIENTES); // Canal 1, rede visível
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    int tentativas = 0;
    while (WiFi.status() != WL_CONNECTED && tentativas < 20) { delay(500); tentativas++; }
  }
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
    // Mesmas faixas dos campos da página: o que chega fora delas é limitado
    if (request->hasParam("k"))      constanteK = constrain(request->getParam("k")->value().toFloat(), 0.5f, 100.0f);
    if (request->hasParam("ciclos")) numCiclos  = constrain(request->getParam("ciclos")->value().toInt(), 1, 10);
    if (request->hasParam("delay"))  intervaloLeitura = constrain(request->getParam("delay")->value().toInt(), 20, 200);
    request->send(200, "text/plain", "OK");
  });

  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    preferences.putFloat("k", constanteK);
    preferences.putInt("ciclos", numCiclos);
    preferences.putInt("delay", intervaloLeitura);
    request->send(200, "text/plain", "SALVO");
  });

  server.begin();

  // Sem a Central não há a quem enviar o ping
  if (!MODO_AUTONOMO) xTaskCreate(tarefaPing, "ping", 8192, NULL, 1, NULL);
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
              periodo = (somaPeriodos / (float)contagemCiclos) / 1000.0; // T em segundos (média dos ciclos de fato somados)
              
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

  // Timeout geral de oscilação: sem um novo pico por muito tempo, zera os resultados e
  // descarta a medida em andamento, para que a pausa não entre no período da próxima
  if (tempoUltimoPico > 0 && agora - tempoUltimoPico > TIMEOUT_PARADO_MS) {
    periodo = 0.0;
    massa   = 0.0;
    tempoUltimoPico = 0;
    somaPeriodos    = 0;
    contagemCiclos  = 0;
  }
}