// Anemometro Digital - Monitor de Velocidade do Vento (m/s e rad/s)
// Hardware: ESP32 + Sensor Reed Switch KY-025 ou Sensor Hall KY-003

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <math.h>
#include "pagina_html.h" // Interface web (index_html)

// CONFIGURAÇÃO DE REDE
const bool MODO_AUTONOMO = true; // true = cria a própria rede (uso sem a Central) | false = conecta à Central

// Rede própria (MODO_AUTONOMO = true): nome "Anemometro <ID>", acesso em http://192.168.4.1
const char ID_ANEMOMETRO = 'A';              // Letra da placa: troque antes de gravar cada anemômetro
const char* senhaAP        = "123456789";    // Mínimo de 8 caracteres
const int   MAX_CLIENTES   = 6;              // Celulares conectados ao mesmo tempo (o ESP32 aceita até 10)
const int   CANAIS_WIFI[4] = {1, 6, 11, 1};  // Canal de A, B, C e D: 1, 6 e 11 não se sobrepõem

// Rede da Central (MODO_AUTONOMO = false)
// const char* ssid     = "Rede_Comunicacao";
// const char* password = "123456789";
const char* ssid     = "ESP IoT";
const char* password = "123456789";

// PINOS DE HARDWARE
const bool SENSOR_HALL = false; // true = Hall KY-003 (pino S) | false = Reed KY-025 (pino DO)
const int  PINO_SENSOR = 14;    // Saída digital do sensor; Ambos usam GPIO14 no ESP32

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
  if (digitalRead(PINO_SENSOR) == LOW) {
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
      http.begin("http://192.168.4.1/ping?nome=Anemometro");
      http.GET();
      http.end();
    }
    vTaskDelay(pdMS_TO_TICKS(INTERVALO_PING_MS));
  }
}

void conectarWiFi() { // CONEXÃO WI-FI
  if (MODO_AUTONOMO) {
    // Ponto de acesso próprio: cada placa tem rede e canal distintos, todas em 192.168.4.1
    String nomeRede = String("Anemometro ") + ID_ANEMOMETRO;
    int canal = CANAIS_WIFI[constrain(ID_ANEMOMETRO - 'A', 0, 3)];
    WiFi.mode(WIFI_AP);
    WiFi.softAP(nomeRede.c_str(), senhaAP, canal, 0, MAX_CLIENTES);
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    int tentativas = 0;
    while (WiFi.status() != WL_CONNECTED && tentativas < 20) { delay(500); tentativas++; }
  }
  if (MDNS.begin("Anemometro")) MDNS.addService("http", "tcp", 80);
}

void setup() { // SETUP
  // O KY-003 só puxa a saída para baixo e precisa de pull-up; o KY-025 mantém a configuração original
  pinMode(PINO_SENSOR, SENSOR_HALL ? INPUT_PULLUP : INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(PINO_SENSOR), ISR_DetectaIma, CHANGE);

  preferences.begin("anemo_cfg", false);
  raioRotor = preferences.getInt("raio", 10);
  numImas   = preferences.getInt("imas", 1);
  fatorCopo = preferences.getFloat("fator", 2.5);

  conectarWiFi();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", index_html);
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"id\":\""      + String(ID_ANEMOMETRO) + "\"" +
                  ",\"raioRotor\":" + String(raioRotor) +
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
    // Mesmas faixas dos campos da página: o que chega fora delas é limitado
    if (request->hasParam("raio"))  raioRotor = constrain(request->getParam("raio")->value().toInt(), 1, 100);
    if (request->hasParam("imas"))  numImas   = constrain(request->getParam("imas")->value().toInt(), 1, 3);
    if (request->hasParam("fator")) fatorCopo = constrain(request->getParam("fator")->value().toFloat(), 1.0f, 5.0f);
    request->send(200, "text/plain", "OK");
  });

  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    preferences.putInt("raio",   raioRotor);
    preferences.putInt("imas",   numImas);
    preferences.putFloat("fator", fatorCopo);
    request->send(200, "text/plain", "SALVO");
  });

  server.begin();

  // Sem a Central não há a quem enviar o ping
  if (!MODO_AUTONOMO) xTaskCreate(tarefaPing, "ping", 8192, NULL, 1, NULL);
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
}
