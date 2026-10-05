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

Paralelamente ao cálculo de $v$ e $\omega$, o firmware também amostra a saída **analógica** (AO) do sensor a cada $5\text{ ms}$ (independentemente da interrupção), permitindo visualizar a intensidade bruta e contínua do campo magnético — antes de qualquer processamento. Isso evidencia ao estudante a etapa de transdução (o fenômeno magnético convertido em um sinal elétrico graduado) separadamente da etapa de cálculo (o sinal digital, já limiarizado, convertido em grandezas físicas).

> **Nota sobre DO vs. AO:** o módulo do sensor expõe duas saídas simultâneas. A saída digital (DO), usada na interrupção para medir $\Delta t$, é um comparador interno ao módulo que já aplica um limiar ajustável fisicamente por um trimpot — ela informa apenas *se* o ímã está próximo o suficiente, não *o quão* próximo. A saída analógica (AO) não passa por esse comparador e entrega a tensão bruta proporcional à intensidade do campo captado, por isso é a fonte correta para o gráfico de intensidade relativa.

---

## ⚡ Recursos e Funcionalidades

* **Amostragem Baseada em Interrupção:** Leitura precisa de tempo de pulso via ISR com *debouncing* por software ($15\text{ ms}$) para atenuar ruídos mecânicos/magnéticos.
* **Interface Web Responsiva Integrada:** Servidor *web* assíncrono embarcado em HTML/CSS/JS.
* **Gráfico Dinâmico em Tempo Real:** Renderização por `HTML5 Canvas` que exibe a velocidade do vento $v$ ($\text{m/s}$) e a velocidade angular $\omega$ ($\text{rad/s}$) em um histórico contínuo.
* **Gráfico de Intensidade Analógica do Sensor:** Segundo `HTML5 Canvas`, posicionado logo abaixo do primeiro, que exibe a leitura contínua ($0$–$4095$, resolução ADC de 12 bits) da saída analógica (AO) do sensor, amostrada em um buffer circular de $100$ pontos a cada $5\text{ ms}$.
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
| **Sensor Hall** | DO (Saída Digital) | **GPIO 14** | Entrada com suporte a Interrupção, usada no cálculo de período/velocidade |
| **Sensor Hall** | AO (Saída Analógica) | **GPIO 34** | Entrada ADC1 (somente leitura), usada no gráfico de intensidade bruta |
| **Sensor Hall** | VCC (+) | **3.3V** ou **5V** | Alimentação |
| **Sensor Hall** | GND (-) | **GND** | Ponto de Referência Comum |

> **Nota:** o limiar do pino DO é ajustado fisicamente pelo trimpot do módulo. Esse ajuste não afeta a leitura do pino AO, que permanece proporcional à intensidade real do campo magnético.

---

## 🌐 Arquitetura do Código e Endpoints

O firmware roda um servidor HTTP assíncrono na porta `80` e disponibiliza as seguintes rotas:

| Rota HTTP | Método | Descrição |
| :--- | :--- | :--- |
| `/` | `GET` | Entrega a interface *web* gráfica (HTML5/CSS/JavaScript). |
| `/dados` | `GET` | Retorna um JSON com as leituras atuais: `{"v": float, "w": float}`. |
| `/intensidade` | `GET` | Retorna um *array* JSON com as $100$ últimas amostras do pino AO ($0$–$4095$), em ordem cronológica, para o gráfico de intensidade. |
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
```

---

## 🚀 Como Utilizar

1. Monte o circuito conforme a tabela de pinagem e acople o sensor à base do rotor, alinhado à trajetória dos ímãs.
2. Imprima em 3D (ou construa artesanalmente) o rotor com o número de pás/conchas e o raio desejados, posicionando o(s) ímã(s) de neodímio no alojamento previsto.
3. Compile e carregue o firmware no ESP32 pela Arduino IDE, ajustando previamente as credenciais de rede.
4. Acesse a interface pelo navegador em `http://Anemometro.local` ou pelo IP exibido no portal da Central de Comunicação.
5. Meça fisicamente o raio do rotor (em cm) e informe no campo **Raio**, junto da **Qtd Ímãs** efetivamente instalada — parâmetros incompatíveis com a montagem real distorcem a leitura de velocidade.
6. Gere um fluxo de ar controlado (ventilador de bancada, sopro ou deslocamento do dispositivo) e observe a velocidade $v$ (m/s) e a velocidade angular $\omega$ (rad/s) no gráfico em tempo real.
6.1. Observe o segundo gráfico (intensidade analógica): cada passagem do ímã pelo sensor deve aparecer como um pico na curva, permitindo correlacionar visualmente o sinal bruto do transdutor com a velocidade calculada acima.
7. Ajuste o **Fator K** comparando a leitura do dispositivo com uma referência conhecida (anemômetro comercial ou velocidade nominal do ventilador), calibrando o fator de correção aerodinâmico das pás.
8. Clique em **Salvar** para persistir a calibração na memória flash do ESP32.