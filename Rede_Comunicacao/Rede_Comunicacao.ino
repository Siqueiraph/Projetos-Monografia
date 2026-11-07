// Rede de Comunicação - Gerador de rede + Monitor de Projetos
// Hardware: ESP32 + Display OLED SSD1306 (I2C)

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "pagina_html.h" // Interface web (portal_html)

// CONFIGURAÇÃO DE REDE
const char* ssidAP       = "Rede_Comunicacao";
const char* senhaAP      = "123456789"; // Mínimo de 8 caracteres
const int   MAX_CLIENTES = 10;          // Módulos + navegadores conectados ao mesmo tempo (máximo do ESP32; o padrão é 4)
const int   CANAL_WIFI   = 1;

// PINOS DE HARDWARE
const int PINO_SDA = 21; // I2C do OLED
const int PINO_SCL = 22;

// PARÂMETROS
const int           LARGURA_OLED_PX      = 128;
const int           ALTURA_OLED_PX       = 64;
const unsigned long TIMEOUT_INATIVO_MS   = 15000; // Sem ping por esse tempo, o módulo é marcado como inativo
const unsigned long INTERVALO_DISPLAY_MS = 2000;  // Intervalo de atualização do OLED

// OBJETOS GLOBAIS
AsyncWebServer server(80);
Adafruit_SSD1306 display(LARGURA_OLED_PX, ALTURA_OLED_PX, &Wire, -1);

struct Projeto {
  String        nome;
  String        ip;
  unsigned long ultimoPing;
  bool          ativo;
};

Projeto listaProjetos[4] = { // Projetos monitorados: o OLED tem espaço para exatamente quatro linhas
  {"Irrigacao",    "0.0.0.0", 0, false},
  {"Musical",      "0.0.0.0", 0, false},
  {"Anemometro A", "0.0.0.0", 0, false}, // O Anemômetro envia "Anemometro <ID>"
  {"Balanca",      "0.0.0.0", 0, false}
};

void setup() { // SETUP
  Wire.begin(PINO_SDA, PINO_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Rede de Comunicacao");
  display.println("Iniciando AP...");
  display.display();

  WiFi.softAP(ssidAP, senhaAP, CANAL_WIFI, 0, MAX_CLIENTES);
  if (MDNS.begin("rede")) MDNS.addService("http", "tcp", 80); // http://rede.local

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", portal_html);
  });

  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("nome")) {
      String nome = request->getParam("nome")->value();
      for (Projeto &p : listaProjetos) {
        if (p.nome == nome) {
          p.ip         = request->client()->remoteIP().toString();
          p.ultimoPing = millis();
          p.ativo      = true;
        }
      }
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/api/projetos", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "[";
    for (Projeto &p : listaProjetos) {
      if (json.length() > 1) json += ",";
      json += "{\"nome\":\"" + p.nome + "\",\"ip\":\"" + p.ip + "\",\"ativo\":" + (p.ativo ? "true" : "false") + "}";
    }
    json += "]";
    request->send(200, "application/json", json);
  });

  server.begin();
}

void loop() { // LOOP PRINCIPAL
  unsigned long agora = millis();
  static unsigned long ultimaAtualizacao = 0;

  if (agora - ultimaAtualizacao >= INTERVALO_DISPLAY_MS) {
    ultimaAtualizacao = agora;
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Rede de Comunicacao");
    display.println("URL: rede.local");
    display.println("IP:  192.168.4.1");
    display.println("---------------------");

    for (int i = 0; i < 4; i++) {
      // millis() é lido de novo aqui: um ping que chegue depois de "agora" daria uma diferença negativa
      if (millis() - listaProjetos[i].ultimoPing > TIMEOUT_INATIVO_MS) listaProjetos[i].ativo = false;
      display.print(listaProjetos[i].nome + ":");
      display.setCursor(70, 32 + (i * 8)); // Coluna do status
      display.println(listaProjetos[i].ativo ? "ON" : "---");
    }
    display.display();
  }
}
