// Rede de Comunicação - Gerador de rede + Monitor de Projetos
// Hardware: ESP32 + Display OLED

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "pagina_html.h" // Interface web (portal_html)

// --- CONFIGURAÇÕES DO OLED ---
#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const char* ssid = "Rede_Comunicacao";
const char* password = "123456789";

AsyncWebServer server(80);

struct Projeto {
  String nome;
  String ip;
  unsigned long lastSeen;
  bool ativo;
};

// Projeto listaProjetos[4] = { // Projetos monitorados
//   {"Irrigacao", "0.0.0.0", 0, false},
//   {"Musical", "0.0.0.0", 0, false},
//   {"Anemometro", "0.0.0.0", 0, false},
//   {"Balanca", "0.0.0.0", 0, false}
// };

Projeto listaProjetos[4] = { // Projetos monitorados
  {"Anemometro A", "0.0.0.0", 0, false},
  {"Anemometro B", "0.0.0.0", 0, false},
  {"Anemometro C", "0.0.0.0", 0, false},
  {"Anemometro D", "0.0.0.0", 0, false}
};

void setup() {
  Wire.begin(21, 22); // 1. Inicializa I2C e OLED (Pins 21 e 22)
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
      Serial.println("OLED não encontrado");
    }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Rede de Comunicacao");
  display.println("Iniciando AP...");
  display.display();

  WiFi.softAP(ssid, password, 1, 0, 10); // Canal 1, rede visível, até 10 clientes (o padrão é 4)
  
  if (MDNS.begin("rede")) {  // mDNS: http://rede.local
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", portal_html);
  });

  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("nome")) {
      String nome = request->getParam("nome")->value();
      String ipOrigem = request->client()->remoteIP().toString();
      
      for(int i=0; i<4; i++) {
        if(listaProjetos[i].nome == nome) {
          listaProjetos[i].ip = ipOrigem;
          listaProjetos[i].lastSeen = millis();
          listaProjetos[i].ativo = true;
        }
      }
    }
    request->send(200, "text/plain", "OK");
  });

  server.on("/api/projetos", HTTP_GET, [](AsyncWebServerRequest *request){ // API para o Portal JSON
    String json = "[";
    for(int i=0; i<4; i++) {
      json += "{\"nome\":\"" + listaProjetos[i].nome + "\",\"ip\":\"" + listaProjetos[i].ip + "\",\"ativo\":" + (listaProjetos[i].ativo ? "true" : "false") + "}";
      if(i < 3) json += ",";
    }
    json += "]";
    request->send(200, "application/json", json);
  });

  server.begin();
}

void loop() {
  static unsigned long lastUpdate = 0;
  unsigned long agora = millis();

  if (agora - lastUpdate > 2000) { // Atualiza o Display a cada 2s
    lastUpdate = agora;
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Rede de Comunicacao");
    display.println("URL: rede.local");
    display.println("IP:  192.168.4.1");
    display.println("---------------------");

    for(int i=0; i<4; i++) {
      // millis() é lido de novo aqui: um ping que chegue depois de "agora" daria uma diferença negativa
      if(millis() - listaProjetos[i].lastSeen > 15000) {
        listaProjetos[i].ativo = false;
      }
      display.print(listaProjetos[i].nome + ":"); // Escreve o nome na esquerda
      display.setCursor(70, 32 + (i * 8)); // Pula para a coluna (x)
      display.println(listaProjetos[i].ativo ? "ON" : "---");
    }
    display.display();
  }
}