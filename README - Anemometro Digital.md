# Anemômetro Digital - Monitor de Velocidade do Vento

Módulo experimental de automação e aquisição de dados do ecossistema didático de Física baseado no microcontrolador **ESP32** e em um **sensor magnético** que detecta a passagem de ímãs fixados a um rotor. O sistema captura os pulsos gerados pela rotação, calcula grandezas cinemáticas em tempo real e disponibiliza uma interface *web* interativa para calibração de parâmetros geométricos/aerodinâmicos e visualização gráfica.

O projeto é distribuído em **duas versões de firmware**, uma para cada sensor: **Efeito Hall (KY-003)** e **Reed Switch (KY-025)**. A física, a calibração e a interface de velocidade são idênticas nas duas; a versão Reed acrescenta a leitura da saída analógica do módulo e um segundo gráfico com o sinal bruto do sensor.

---

## 📋 Sumário
- [Versões do Firmware](#-versões-do-firmware)
- [Fundamentação Física](#-fundamentação-física)
- [Recursos e Funcionalidades](#-recursos-e-funcionalidades)
- [Esquema de Hardware e Pinagem](#-esquema-de-hardware-e-pinagem)
- [Arquitetura do Código e Endpoints](#-arquitetura-do-código-e-endpoints)
- [Instalação e Configuração](#-instalação-e-configuração)
- [Como Utilizar](#-como-utilizar)

---

## 🔀 Versões do Firmware

Cada versão fica em sua própria pasta de *sketch*. Grave no ESP32 **apenas uma delas**, de acordo com o sensor montado.

| | **Versão Hall** | **Versão Reed** |
| :--- | :--- | :--- |
| **Sketch** | `Anemometro_Digital_Hall/Anemometro_Digital_Hall.ino` | `Anemometro_Digital_Reed/Anemometro_Digital_Reed.ino` |
| **Sensor** | Efeito Hall KY-003 (3 pinos: S, VCC, GND) | Reed Switch KY-025 (4 pinos: AO, G, +, DO) |
| **Princípio de detecção** | Semicondutor: o campo magnético comuta um transistor interno, sem partes móveis | Eletromecânico: o campo magnético fecha um contato de lâminas metálicas |
| **Sinal digital** | GPIO 14, `INPUT_PULLUP`, interrupção em `FALLING` | GPIO 14, `INPUT_PULLDOWN`, interrupção em `RISING` |
| **Sinal analógico** | Não disponível no módulo | GPIO 34 (ADC1), amostrado a cada $5\text{ ms}$ |
| **Gráficos na interface** | Velocidade ($v$ e $\omega$) | Velocidade ($v$ e $\omega$) + sinal analógico do sensor |
| **Rota `/intensidade`** | Não | Sim |
| **Intervalo do *ping* à Central** | $5\text{ s}$ | $30\text{ s}$ |

**Qual escolher:**
* **Hall** — versão base, mais simples de montar e de explicar. Indicada quando o objetivo é apenas medir a velocidade do vento.
* **Reed** — indicada quando se deseja também *mostrar o sinal do sensor* ao estudante, separando visualmente a etapa de transdução (o pulso elétrico) da etapa de cálculo (a velocidade).

> **Nota:** as duas versões usam o mesmo nome mDNS (`Anemometro.local`), o mesmo identificador de *ping* (`Anemometro`) e o mesmo espaço de calibração na memória flash (`anemo_cfg`). Por isso, não devem operar simultaneamente na mesma rede; em contrapartida, a calibração salva é preservada ao trocar de uma versão para a outra no mesmo ESP32.

---

## 🔬 Fundamentação Física

O princípio de funcionamento baseia-se na contagem de pulsos causados pela passagem de ímãs fixados ao rotor diante do sensor magnético. O firmware utiliza uma rotina de interrupção (ISR) para mensurar o intervalo de tempo $\Delta t$ (em milissegundos) entre detecções consecutivas. Esse cálculo é o mesmo nas duas versões.

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

### Sinal analógico do sensor (somente versão Reed)

Paralelamente ao cálculo de $v$ e $\omega$, a versão Reed amostra a saída **analógica** (AO) do módulo KY-025 a cada $5\text{ ms}$, independentemente da interrupção, e a exibe em um segundo gráfico. Isso evidencia ao estudante a etapa de transdução (o fenômeno magnético convertido em um sinal elétrico) separadamente da etapa de cálculo (o sinal digital convertido em grandezas físicas).

> **Nota sobre DO vs. AO:** o módulo KY-025 expõe duas saídas simultâneas. A saída digital (DO), usada na interrupção para medir $\Delta t$, passa por um comparador interno ao módulo, cujo limiar é ajustado fisicamente por um trimpot. A saída analógica (AO) não passa por esse comparador e entrega a tensão bruta presente no contato do sensor.

> **Nota sobre a forma do sinal:** o *reed switch* é um contato que abre ou fecha, de modo que a tensão em AO alterna essencialmente entre dois níveis a cada passagem do ímã, em vez de variar de forma graduada com a intensidade do campo. O gráfico mostra, portanto, a largura e o espaçamento dos pulsos e eventuais oscilações do contato mecânico (*bounce*) — o que ilustra bem a necessidade do *debouncing* aplicado na interrupção.

---

## ⚡ Recursos e Funcionalidades

### Comuns às duas versões
* **Amostragem Baseada em Interrupção:** Leitura precisa de tempo de pulso via ISR com *debouncing* por software ($15\text{ ms}$) para atenuar ruídos mecânicos/magnéticos.
* **Interface Web Responsiva Integrada:** Servidor *web* assíncrono embarcado em HTML/CSS/JS.
* **Gráfico Dinâmico em Tempo Real:** Renderização por `HTML5 Canvas` que exibe a velocidade do vento $v$ ($\text{m/s}$) e a velocidade angular $\omega$ ($\text{rad/s}$) em um histórico contínuo de $100$ pontos, atualizado a cada $500\text{ ms}$.
* **Calibração Dinâmica:** Ajuste dos parâmetros geométricos (Raio e Ímãs) e aerodinâmicos (Fator $K$) diretamente pelo navegador, sem necessidade de recompilar o firmware.
* **Persistência de Dados (NVS):** Salva as configurações de calibração na memória flash via biblioteca `Preferences`, preservando-as após reinicializações.
* **Conectividade Integrada:** Resolução mDNS (`http://Anemometro.local`) e comunicação de *heartbeat*/ping periódico com a central de comunicação do ecossistema didático.

### Exclusivo da versão Reed
* **Gráfico do Sinal Analógico do Sensor:** Segundo `HTML5 Canvas`, posicionado logo abaixo do primeiro, que exibe a leitura da saída AO ($0$–$4095$, resolução ADC de 12 bits). O firmware mantém um buffer circular com as $100$ amostras mais recentes (cerca de $500\text{ ms}$), e a interface acumula as leituras recebidas em um histórico visível de $300$ pontos (cerca de $1{,}5\text{ s}$).

---

## 🛠️ Esquema de Hardware e Pinagem

### Componentes Necessários
* 1x Microcontrolador ESP32 DevKit v1
* 1x Sensor magnético, conforme a versão escolhida:
  * Sensor de Efeito Hall KY-003 (versão Hall), **ou**
  * Sensor Reed Switch KY-025 (versão Reed)
* Ímãs de Neodímio instalados no rotor
* Rotor impresso em 3D ou artesanal com copos/pás
* Protoboard e Jumper Wires

### Tabela de Conexões — Versão Hall (KY-003)

| Componente | Pino do Módulo KY-003 | Pino do ESP32 | Função |
| :--- | :--- | :--- | :--- |
| **Sensor Hall** | S (Sinal) | **GPIO 14** | Entrada com suporte a Interrupção, usada no cálculo de período/velocidade |
| **Sensor Hall** | VCC (+) | **3.3V** ou **5V** | Alimentação |
| **Sensor Hall** | GND (-) | **GND** | Ponto de Referência Comum |

> **Nota:** a saída do KY-003 permanece em nível alto e vai a nível baixo quando o ímã se aproxima; por isso o firmware usa `INPUT_PULLUP` e interrupção na borda de descida (`FALLING`). O sensor responde a apenas uma das polaridades do ímã — se não houver detecção, inverta a face do ímã voltada para o sensor.

### Tabela de Conexões — Versão Reed (KY-025)

| Componente | Pino do Módulo KY-025 | Pino do ESP32 | Função |
| :--- | :--- | :--- | :--- |
| **Sensor Reed** | DO (Saída Digital) | **GPIO 14** | Entrada com suporte a Interrupção, usada no cálculo de período/velocidade |
| **Sensor Reed** | AO (Saída Analógica) | **GPIO 34** | Entrada ADC1 (somente leitura), usada no gráfico do sinal do sensor |
| **Sensor Reed** | + (VCC) | **3.3V** | Alimentação |
| **Sensor Reed** | G (GND) | **GND** | Ponto de Referência Comum |

> **Nota:** nesta versão, alimente o módulo em **3.3V**. Como a saída AO é ligada diretamente ao conversor analógico-digital do ESP32, alimentar o módulo em 5V aplicaria ao GPIO 34 uma tensão acima do limite suportado.

> **Nota:** o limiar do pino DO é ajustado fisicamente pelo trimpot do módulo; esse ajuste não afeta a leitura do pino AO. O firmware usa `INPUT_PULLDOWN` e interrupção na borda de subida (`RISING`).

---

## 🌐 Arquitetura do Código e Endpoints

O firmware roda um servidor HTTP assíncrono na porta `80` e disponibiliza as seguintes rotas:

| Rota HTTP | Método | Versão | Descrição |
| :--- | :--- | :--- | :--- |
| `/` | `GET` | Hall e Reed | Entrega a interface *web* gráfica (HTML5/CSS/JavaScript). |
| `/dados` | `GET` | Hall e Reed | Retorna um JSON com as leituras atuais: `{"v": float, "w": float}`. |
| `/status` | `GET` | Hall e Reed | Retorna o estado atual das variáveis de calibração salvas. |
| `/set` | `GET` | Hall e Reed | Recebe parâmetros via *query params* (`?raio=X&imas=Y&fator=Z`) para atualização instantânea. |
| `/save` | `GET` | Hall e Reed | Grava os valores atuais de calibração na memória NVS (`Preferences`). |
| `/intensidade` | `GET` | Somente Reed | Retorna um *array* JSON com as $100$ últimas amostras do pino AO ($0$–$4095$), em ordem cronológica, para o gráfico do sinal do sensor. |

---

## 💻 Instalação e Configuração

### 1. Requisitos de Software
* [Arduino IDE](https://www.arduino.cc/en/software) ou [PlatformIO](https://platformio.org/)
* Suporte à placa ESP32 instalado na IDE
* Dependências de bibliotecas (iguais para as duas versões):
  * `ESPAsyncWebServer`
  * `AsyncTCP` (para arquitetura ESP32)
  * `Preferences` (incluso no core do ESP32)
  * `ESPmDNS` (incluso no core do ESP32)

### 2. Escolha da Versão
Abra na IDE a pasta correspondente ao sensor montado — `Anemometro_Digital_Hall` ou `Anemometro_Digital_Reed`. Cada pasta é um *sketch* independente.

### 3. Configuração da Rede
No código-fonte, ajuste as credenciais para as do Ponto de Acesso Central:
```cpp
const char* ssid     = "Rede_Comunicacao";
const char* password = "123456789";
```

> **Nota de implementação:** o *sketch* da versão Reed está atualmente configurado com uma rede de testes de bancada (`"ESP IoT"`) e com intervalo de *ping* de $30\text{ s}$. Para integrá-lo ao ecossistema, restaure o SSID `Rede_Comunicacao` e reduza o intervalo de *ping* para um valor inferior a $15\text{ s}$ — tempo após o qual a Central de Comunicação passa a considerar o módulo *offline*.

---

## 🚀 Como Utilizar

1. Monte o circuito conforme a tabela de pinagem da versão escolhida e acople o sensor à base do rotor, alinhado à trajetória dos ímãs.
2. Imprima em 3D (ou construa artesanalmente) o rotor com o número de pás/conchas e o raio desejados, posicionando o(s) ímã(s) de neodímio no alojamento previsto.
3. Compile e carregue no ESP32 o *sketch* correspondente ao sensor (Hall ou Reed) pela Arduino IDE, ajustando previamente as credenciais de rede.
4. Acesse a interface pelo navegador em `http://Anemometro.local` ou pelo IP exibido no portal da Central de Comunicação.
5. Meça fisicamente o raio do rotor (em cm) e informe no campo **Raio**, junto da **Qtd Ímãs** efetivamente instalada — parâmetros incompatíveis com a montagem real distorcem a leitura de velocidade.
6. Gere um fluxo de ar controlado (ventilador de bancada, sopro ou deslocamento do dispositivo) e observe a velocidade $v$ (m/s) e a velocidade angular $\omega$ (rad/s) no gráfico em tempo real.
7. **(Somente versão Reed)** Observe o segundo gráfico: cada passagem do ímã pelo sensor aparece como um pulso na curva, e o espaçamento entre pulsos diminui à medida que o rotor acelera — o que permite correlacionar visualmente o sinal bruto do transdutor com a velocidade calculada acima. Se os pulsos não aparecerem no gráfico de velocidade, ajuste o trimpot do módulo.
8. Ajuste o **Fator K** comparando a leitura do dispositivo com uma referência conhecida (anemômetro comercial ou velocidade nominal do ventilador), calibrando o fator de correção aerodinâmico das pás.
9. Clique em **Salvar** para persistir a calibração na memória flash do ESP32.
