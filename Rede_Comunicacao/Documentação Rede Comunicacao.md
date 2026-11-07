# Central de Comunicação — Ponto de Acesso e Portal de Projetos

Módulo centralizador do ecossistema didático de automação e aquisição de dados em Física baseado no microcontrolador **ESP32**. Atua como Ponto de Acesso Wi-Fi (SoftAP) independente, servidor mDNS local (`http://rede.local`) e portal de roteamento dinâmico. O sistema monitora a atividade dos subprojetos acoplados à rede por meio de pings periódicos e exibe o status de conectividade em tempo real tanto em um display OLED físico quanto em um portal *web* unificado.

---

## 📋 Sumário
- [Visão Geral e Arquitetura](#-visão-geral-e-arquitetura)
- [Recursos e Funcionalidades](#-recursos-e-funcionalidades)
- [Esquema de Hardware e Pinagem](#-esquema-de-hardware-e-pinagem)
- [Arquitetura de Endpoints e API](#-arquitetura-de-endpoints-e-api)
- [Instalação e Configuração](#-instalação-e-configuração)
- [Protocolo de Integração dos Subprojetos](#-protocolo-de-integração-dos-subprojetos)

---

## 🔬 Visão Geral e Arquitetura

O módulo opera como a "espinha dorsal" de comunicação do laboratório, dispensando a necessidade de roteadores externos ou infraestruturas de rede locais. Ao ser energizado, o ESP32 gera sua própria rede Wi-Fi e atribui IP fixo (`192.168.4.1`) para si e IPs dinâmicos via DHCP para os demais módulos experimentais (Anemômetro, Irrigação, Balança e Controle Musical) que se conectam a ele.

O monitoramento da presença dos módulos ocorre através de uma tabela de status em memória:

$$\text{Status do Módulo} = \begin{cases} \text{Ativo (ON)}, & \text{se } (t_{\text{atual}} - t_{\text{último\_ping}}) \le 15\text{ s} \\ \text{Inativo (---)}, & \text{se } (t_{\text{atual}} - t_{\text{último\_ping}}) > 15\text{ s} \end{cases}$$

---

## ⚡ Recursos e Funcionalidades

* **Ponto de Acesso Dedicado (SoftAP):** Criação da rede local `"Rede_Comunicacao"` com suporte a até $10$ clientes concorrentes (somando os módulos e os navegadores conectados).
* **Resolução mDNS Local:** Permite acesso simplificado ao portal via navegador através do endereço humanamente legível `http://rede.local`.
* **Interface Física Local (OLED I2C):** Atualização contínua a cada $2\text{ s}$ exibindo os dados da rede (URL e IP) e a indicação visual de status (`ON` ou `---`) para os 4 subprojetos.
* **Portal Web Unificado:** Interface HTML/CSS responsiva com requisições dinâmicas via `fetch` (polling a cada $3\text{ s}$) que gera links diretos e clicáveis para os IPs dos módulos que estiverem ativos.
* **Gestão Dinâmica de IP:** Mapeamento automático do endereço IP de origem dos módulos no momento da recepção do *heartbeat*.

---

## 🛠️ Esquema de Hardware e Pinagem

### Componentes Necessários
* 1x Microcontrolador ESP32 DevKit v1
* 1x Display OLED SSD1306 ($128 \times 64$ pixels, Comunicação I2C)
* Protoboard e Jumper Wires

### Tabela de Conexões (I2C)

| Componente | Pino do OLED | Pino do ESP32 | Função |
| :--- | :--- | :--- | :--- |
| **Display OLED** | VCC | **3.3V** | Alimentação do Display |
| **Display OLED** | GND | **GND** | Referência Comum |
| **Display OLED** | SDA | **GPIO 21** | Linha de Dados I2C |
| **Display OLED** | SCL | **GPIO 22** | Linha de Clock I2C |

---

## 🌐 Arquitetura de Endpoints e API

O servidor *web* assíncrono embarcado gerencia os seguintes caminhos HTTP na porta `80`:

| Rota HTTP | Método | Descrição |
| :--- | :--- | :--- |
| `/` | `GET` | Entrega o portal principal de acesso (HTML5/CSS/JS) com os botões dos projetos. |
| `/ping` | `GET` | Recebe requisições de *heartbeat* dos subprojetos (`?nome=NOME_PROJETO`), mapeando o IP remetente e atualizando a estampa de tempo. |
| `/api/projetos` | `GET` | Retorna um array JSON com o status de todos os projetos cadastrados: `[{"nome":"...", "ip":"...", "ativo":true/false}]`. |

O portal fica em `pagina_html.h` (constante `portal_html`), separado do *sketch*; o `.ino` apenas o inclui e o serve na rota `/`.

Para ver a página sem gravar o ESP32, use `python scripts/visualizar_interface.py rede` (raiz do repositório) e abra `http://localhost:8000`; as rotas respondem com valores fixos de exemplo.

---

## 💻 Instalação e Configuração

### 1. Requisitos de Software
* [Arduino IDE](https://www.arduino.cc/en/software) ou [PlatformIO](https://platformio.org/)
* Suporte à placa ESP32 instalado na IDE
* Dependências de bibliotecas:
  * `ESPAsyncWebServer`
  * `AsyncTCP`
  * `Adafruit_SSD1306`
  * `Adafruit_GFX`
  * `Wire` (incluso no core do ESP32)
  * `ESPmDNS` (incluso no core do ESP32)

### 2. Configurações da Rede Central
As credenciais do Ponto de Acesso criado pelo ESP32 vêm configuradas como padrão:
```cpp
const char* ssidAP       = "Rede_Comunicacao";
const char* senhaAP      = "123456789"; // Mínimo de 8 caracteres
const int   MAX_CLIENTES = 10;          // Módulos + navegadores conectados ao mesmo tempo
const int   CANAL_WIFI   = 1;
```

O tempo sem *ping* até um módulo ser marcado como inativo é `TIMEOUT_INATIVO_MS` ($15\text{ s}$), e o OLED é redesenhado a cada `INTERVALO_DISPLAY_MS` ($2\text{ s}$).

---

## 📡 Protocolo de Integração dos Subprojetos

Qualquer novo módulo que deseje integrar-se ao portal da Central de Comunicação deve seguir um contrato simples de três etapas:

1. **Conexão à Rede:** o subprojeto conecta-se em modo estação (`WIFI_STA`) à rede Wi-Fi gerada pela Central (`ssid`/`password` configurados), recebendo um IP dinâmico via DHCP.
2. **Heartbeat Periódico:** o firmware do subprojeto realiza requisições `GET` periódicas (a cada $5\text{ s}$, em uma tarefa própria do FreeRTOS para não bloquear o laço principal) para `http://192.168.4.1/ping?nome=<NOME_DO_PROJETO>`. O parâmetro `nome` deve corresponder **exatamente** a uma das entradas cadastradas no vetor `listaProjetos[]` do firmware central — é esse nome que determina em qual posição do portal e do display OLED o status do módulo será exibido.
3. **Janela de Atividade:** a cada *ping* recebido, a Central grava o IP de origem e a marca de tempo (`ultimoPing`). Se nenhum *ping* for recebido por mais de $15\text{ s}$ (`TIMEOUT_INATIVO_MS`), o módulo é automaticamente marcado como inativo tanto no display OLED quanto no portal *web*.

> **Nota:** nos quatro subprojetos, o *ping* é enviado pela função `tarefaPing`, criada com `xTaskCreate` ao final do `setup()` apenas quando `MODO_AUTONOMO = false` (no modo autônomo o módulo cria a própria rede e não há Central), e o intervalo entre envios é a constante `INTERVALO_PING_MS` ($5\text{ s}$). Como a tarefa roda em paralelo ao `loop()`, a espera pela resposta da Central — de até $1\text{ s}$ quando ela está fora de alcance — não interrompe a aquisição de dados nem a atualização dos gráficos. Ao alterar o intervalo, mantenha-o abaixo de $15\text{ s}$, para que o módulo não seja marcado como inativo entre dois *pings*.

Dessa forma, basta que o firmware de cada subprojeto envie o `nome` correspondente (`Irrigacao`, `Musical`, `Anemometro <ID>` ou `Balanca`) para que seu botão apareça automaticamente, com link direto, no portal unificado.