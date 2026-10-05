# Controle Musical - Equalizador Visual por Bandas de Frequência

Módulo experimental de acústica e processamento digital de sinais do ecossistema didático de Física baseado no microcontrolador **ESP32** e no **microfone digital I2S INMP441**. O sistema captura o áudio ambiente em tempo real, separa o espectro sonoro em três bandas de frequência (grave, médio e agudo) por meio de filtros digitais IIR Biquad, e aciona relés (ou LEDs) correspondentes sempre que a intensidade instantânea de cada banda ultrapassa um limiar configurável. Uma interface *web* embarcada permite o monitoramento gráfico contínuo das três bandas e a calibração remota de ganho, offset, limiar e tempo de resposta.

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

O microfone INMP441 converte a flutuação de pressão sonora captada por sua membrana em uma palavra digital de 24 bits (*left-justified* em um *frame* de 32 bits), transmitida ao ESP32 via protocolo I2S por DMA. A cada bloco de $64$ amostras lidas a $F_s = 9000\text{ Hz}$ (teto de Nyquist em $4500\text{ Hz}$), o firmware executa três etapas:

1. **Filtragem Digital IIR Biquad (Passa-Banda):**
   Cada banda é isolada por uma seção biquadrática de resposta ao impulso infinita, parametrizada pela frequência central $f_0$ (média geométrica das bordas) e pelo fator de qualidade $Q$ (derivado da largura de banda):
   $$f_0 = \sqrt{f_{min} \cdot f_{max}} \qquad Q = \frac{f_0}{f_{max} - f_{min}}$$
   Os coeficientes do filtro são calculados a partir de $\omega_0 = 2\pi f_0/F_s$ e $\alpha = \sin(\omega_0)/(2Q)$:
   $$b_0 = \frac{\alpha}{1+\alpha} \quad b_1 = 0 \quad b_2 = \frac{-\alpha}{1+\alpha} \qquad a_1 = \frac{-2\cos(\omega_0)}{1+\alpha} \quad a_2 = \frac{1-\alpha}{1+\alpha}$$
   Por construção matemática ($b_0 = -b_2$, $b_1 = 0$), o filtro rejeita a componente DC do sinal sem a necessidade de um estágio adicional. As faixas padrão de fábrica são: **Grave** ($180$–$600\text{ Hz}$), **Médio** ($600$–$1500\text{ Hz}$) e **Agudo** ($1500$–$4400\text{ Hz}$).

2. **Detecção de Pico e Conversão em Tensão de Saída:**
   Dentro de cada bloco de amostragem, o algoritmo avalia o valor absoluto máximo do sinal filtrado e aplica o ganho $G$ e o *offset* $V_{off}$ calibráveis pela interface:
   $$V_{out} = \left( G \cdot \max_{i=1}^{N} \left| V_f[i] \right| \right) + V_{off} \quad \text{(limitado ao intervalo } [0,\,2000]\text{)}$$

3. **Acionamento com Histerese Temporal:**
   O canal correspondente é ligado quando $V_{out}$ ultrapassa o Limiar Global e permanece ligado por, no mínimo, o tempo de retenção (*Hold Time*) configurado, mesmo que o sinal caia abaixo do limiar momentaneamente. Uma janela cega (*blanking*) de $60\text{ ms}$ após qualquer acionamento de relé evita que o próprio clique mecânico seja captado como ruído pelo microfone (microfonia).

---

## ⚡ Recursos e Funcionalidades

* **Captura de Áudio via I2S/DMA:** Leitura digital nativa do ESP32 (sem necessidade de ADC analógico), em blocos de $64$ amostras a $9\text{ kHz}$.
* **Três Filtros IIR Biquad Independentes:** Processamento no domínio do tempo por equações de diferenças recursivas, sem necessidade de FFT.
* **Transmissão em Tempo Real (SSE):** *Server-Sent Events* na rota `/events` transmitem os picos de cada banda a cada $40\text{ ms}$ para o gráfico no navegador.
* **Gráfico Dinâmico em Tempo Real:** Renderização por `HTML5 Canvas` das três bandas sobrepostas, com linha tracejada indicando o Limiar Global.
* **Calibração Dinâmica:** Ajuste independente de Ganho e Offset por banda, além de Limiar Global e Hold Time, diretamente pelo navegador, sem recompilar o firmware.
* **Persistência de Dados (NVS):** Salva as configurações de calibração na memória flash via biblioteca `Preferences`.
* **Conectividade Integrada:** Resolução mDNS (`http://Musical.local`) e comunicação de *heartbeat* periódico com a central de comunicação do ecossistema didático.

---

## 🛠️ Esquema de Hardware e Pinagem

### Componentes Necessários
* 1x Microcontrolador ESP32 DevKit v1
* 1x Microfone digital I2S INMP441
* 3x LEDs de cores diferentes + 3x resistores de $47\ \Omega$ **ou** 1x módulo relé de (ao menos) 3 canais, para controle de lâmpadas de corrente alternada
* Protoboard e Jumper Wires

> **Nota:** O firmware foi escrito para módulos relé com lógica ativa em nível baixo (`LOW` = relé ligado, `HIGH` = relé desligado), correspondendo à variante de montagem com relés do projeto. Para a variante didática com LEDs ligados diretamente aos GPIOs, a lógica de acionamento em `controlarCanal()` deve ser invertida no código-fonte.

### Tabela de Conexões

| Componente | Pino do Componente | Pino do ESP32 | Função |
| :--- | :--- | :--- | :--- |
| **INMP441** | VCC | **3.3V** | Alimentação |
| **INMP441** | GND e L/R | **GND** | Referência Comum e seleção de canal |
| **INMP441** | SCK | **GPIO 18** | *Bit Clock* (I2S) |
| **INMP441** | WS | **GPIO 19** | *Word Select* (I2S) |
| **INMP441** | SD | **GPIO 32** | Dados de Áudio (I2S) |
| **Canal Grave** | IN / ⊕ | **GPIO 25** | Acionamento da banda grave |
| **Canal Médio** | IN / ⊕ | **GPIO 26** | Acionamento da banda média |
| **Canal Agudo** | IN / ⊕ | **GPIO 27** | Acionamento da banda aguda |

---

## 🌐 Arquitetura do Código e Endpoints

O firmware roda um servidor HTTP assíncrono na porta `80` e disponibiliza as seguintes rotas:

| Rota HTTP | Método | Descrição |
| :--- | :--- | :--- |
| `/` | `GET` | Entrega a interface *web* gráfica (HTML5/CSS/JavaScript). |
| `/status` | `GET` | Retorna um JSON com todos os parâmetros de calibração atuais. |
| `/update` | `GET` | Recebe parâmetros via *query params* (`?id=NOME&value=X`) para atualização instantânea de um único campo. |
| `/save` | `GET` | Grava os valores atuais de calibração na memória NVS (`Preferences`). |
| `/events` | `GET` (SSE) | Canal de eventos contínuo que transmite `{"g":X,"m":Y,"a":Z}` com os picos de cada banda. |

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
  * `driver/i2s.h` (incluso no core do ESP32)

### 2. Configuração da Rede
No código-fonte, ajuste as credenciais do seu Ponto de Acesso Central. Por padrão, o módulo integra-se à rede gerada pela Central de Comunicação (`Rede_Comunicacao`); as linhas atualmente ativas no arquivo apontam para uma rede de bancada utilizada durante o desenvolvimento e devem ser editadas antes da compilação:
```cpp
// const char* ssid     = "Rede_Comunicacao";
// const char* password = "123456789";
const char* ssid     = "ESP IoT";
const char* password = "123456789";
```

---

## 🚀 Como Utilizar

1. Monte o circuito conforme a tabela de pinagem, posicionando o microfone de modo a captar o som ambiente (caixas de som, instrumentos etc.).
2. Compile e carregue o firmware no ESP32 pela Arduino IDE, ajustando previamente as credenciais de rede.
3. Acesse a interface pelo navegador em `http://Musical.local` ou pelo IP exibido no portal da Central de Comunicação.
4. Reproduza uma música e observe o gráfico: ajuste o **Offset** de cada banda para eliminar o ruído de fundo e o **Ganho** para aumentar a sensibilidade da banda desejada.
5. Defina o **Limiar** (linha tracejada no gráfico) como o ponto de disparo dos canais e o **Hold Time** para controlar a duração mínima de acionamento.
6. Clique em **Salvar** para persistir a calibração na memória flash do ESP32.
