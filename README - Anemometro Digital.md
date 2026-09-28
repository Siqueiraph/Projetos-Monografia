# Anemômetro Digital - Monitor de Velocidade do Vento

Módulo experimental de automação e aquisição de dados do ecossistema didático de Física baseado no microcontrolador **ESP32** e no **Sensor de Efeito Hall KY-003**. O sistema captura pulsos magnéticos gerados pela rotação de um rotor, calcula grandezas cinemáticas em tempo real e disponibiliza uma interface *web* interativa para calibração de parâmetros geométricos/aerodinâmicos e visualização gráfica.

---

## 📋 Sumário
- [Fundamentação Física](#-fundamentação-física)
- [Recursos e Funcionalidades](#-recursos-e-funcionalidades)
- [Esquema de Hardware e Pinagem](#-esquema-de-hardware-e-pinagem)
- [Arquitetura do Código e Endpoints](#-arquitetura-do-código-e-endpoints)
- [Instalação e Configuração](#-instalação-e-configuração)
- [Como Utilizar](#-como-utilizar)

---

## 🔬 Fundamentação Física

O princípio de funcionamento baseia-se na contagem de pulsos causados pela passagem de ímãs fixados ao rotor diante de um sensor de efeito Hall. O firmware utiliza uma rotina de interrupção (ISR) para mensurar o intervalo de tempo $\Delta t$ (em milissegundos) entre detecções consecutivas.

1. **Período de Rotação ($T$):**
   O período completo para uma volta do rotor considera o tempo entre pulsos e a quantidade total de ímãs ($N$) instalados:
   $$T = \left(\frac{\Delta t}{1000}\right) \cdot N \quad [\text{s}]$$

2. **Velocidade Angular ($\omega$):**
   A partir do período $T$, determina-se a velocidade angular do rotor:
   $$\omega = \frac{2\pi}{T} \quad [\text{rad/s}]$$

3. **Velocidade Linear do Vento ($v$):**
   Relaciona-se a velocidade angular $\omega$, o raio do rotor $R$ (convertido de centímetros para metros) e o fator de correção aerodinâmico das conchas/pás ($K$):
   $$v = \omega \cdot \left(\frac{R}{100}\right) \cdot K \quad [\text{m/s}]$$

> **Nota:** Caso o sistema não detecte novos pulsos durante um intervalo superior a $3000\text{ ms}$, assume-se que o rotor parou, zerando automaticamente as variáveis $\omega$ e $v$.

---

## ⚡ Recursos e Funcionalidades

* **Amostragem Baseada em Interrupção:** Leitura precisa de tempo de pulso via ISR com *debouncing* por software ($15\text{ ms}$) para atenuar ruídos mecânicos/magnéticos.
* **Interface Web Responsiva Integrada:** Servidor *web* assíncrono embarcado em HTML/CSS/JS.
* **Gráfico Dinâmico em Tempo Real:** Renderização por `HTML5 Canvas` que exibe a velocidade do vento $v$ ($\text{m/s}$) e a velocidade angular $\omega$ ($\text{rad/s}$) em um histórico contínuo.
* **Calibração Dinâmica:** Ajuste dos parâmetros geométricos (Raio e Ímãs) e aerodinâmicos (Fator $K$) diretamente pelo navegador, sem necessidade de recompilar o firmware.
* **Persistência de Dados (NVS):** Salva as configurações de calibração na memória flash via biblioteca `Preferences`, preservando-as após reinicializações.
* **Conectividade Integrada:** Resolução mDNS (`http://Anemometro.local`) e comunicação de *heartbeat*/ping contínuo com a central de comunicação do ecossistema didático.

---

## 🛠️ Esquema de Hardware e Pinagem

### Componentes Necessários
* 1x Microcontrolador ESP32 DevKit v1
* 1x Sensor de Efeito Hall KY-003
* Ímãs de Neodímio instalados no rotor
* Rotor impresso em 3D ou artesanal com copos/pás
* Protoboard e Jumper Wires

### Tabela de Conexões

| Componente | Pino do Módulo KY-003 | Pino do ESP32 | Função |
| :--- | :--- | :--- | :--- |
| **Sensor Hall** | Signal (S) | **GPIO 14** | Entrada com suporte a Interrupção (`FALLING`) |
| **Sensor Hall** | VCC (+) | **3.3V** ou **5V** | Alimentação |
| **Sensor Hall** | GND (-) | **GND** | Ponto de Referência Comum |

---

## 🌐 Arquitetura do Código e Endpoints

O firmware roda um servidor HTTP assíncrono na porta `80` e disponibiliza as seguintes rotas:

| Rota HTTP | Método | Descrição |
| :--- | :--- | :--- |
| `/` | `GET` | Entrega a interface *web* gráfica (HTML5/CSS/JavaScript). |
| `/dados` | `GET` | Retorna um JSON com as leituras atuais: `{"v": float, "w": float}`. |
| `/status` | `GET` | Retorna o estado atual das variáveis de calibração salvas. |
| `/set` | `GET` | Recebe parâmetros via *query params* (`?raio=X&imas=Y&fator=Z`) para atualização instantânea. |
| `/save` | `GET` | Grava os valores atuais de calibração na memória NVS (`Preferences`). |

---

## 💻 Instalação e Configuração

### 1. Requisitos de Software
* [Arduino IDE](https://www.arduino.cc/en/software) ou [PlatformIO](https://platformio.org/)
* Suporte à placa ESP32 instalado na IDE
* Dependências de bibliotecas:
  * `ESPAsyncWebServer`
  * `AsyncTCP` (para arquitetura ESP32)
  * `Preferences` (incluso no core do ESP32)
  * `ESPmDNS` (incluso no core do ESP32)

### 2. Configuração da Rede
No código-fonte, ajuste as credenciais do seu Ponto de Acesso Central, se necessário:
```cpp
const char* ssid     = "Rede_Comunicacao";
const char* password = "123456789";