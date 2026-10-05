# Balança Inercial - Medidor de Massa por Oscilador Harmônico Simples

Módulo experimental de mecânica (dinâmica do oscilador harmônico simples) do ecossistema didático de Física baseado no microcontrolador **ESP32** e no **sensor ultrassônico HC-SR04**. Inspirado no método de medição de massa utilizado em ambientes de microgravidade, como a Estação Espacial Internacional (ISS), o dispositivo monitora sem contato físico a oscilação vertical de um porta-amostra preso a uma mola, calcula o período de oscilação por meio de um algoritmo de detecção de picos com histerese e infere a massa inercial da amostra a partir da constante elástica da mola. A interface *web* embarcada disponibiliza gráfico em tempo real da oscilação e calibração dos parâmetros físicos e de amostragem.

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

O sensor HC-SR04 dispara pulsos de ultrassom e mensura o tempo de retorno do eco, permitindo calcular a distância até o porta-amostra sem qualquer contato mecânico que introduza atrito:
$$d = \frac{\Delta t_{eco} \cdot 343}{2} \quad [\text{cm}] \text{, com } \Delta t_{eco} \text{ em microssegundos e } v_{som} \approx 343\text{ m/s}$$

1. **Filtragem e Detecção de Picos:**
   O sinal bruto é suavizado por um filtro de Média Móvel Exponencial (EMA) com peso $0{,}3$ para a nova amostra, atenuando o ruído de reflexão do pulso ultrassônico. Um algoritmo com histerese (margem de $0{,}5\text{ cm}$) identifica os picos e vales da onda $h(t)$, calculando o intervalo de tempo entre máximos sucessivos como o período de oscilação $T$.

2. **Massa Inercial a partir do Período (Lei de Hooke + 2ª Lei de Newton):**
   O período de um oscilador harmônico simples depende apenas da massa $m$ da amostra e da constante elástica $k$ da mola — por isso a medição é válida em qualquer campo gravitacional, inclusive em gravidade zero:
   $$T = 2\pi\sqrt{\frac{m}{k}} \quad \Longrightarrow \quad m = k \cdot \left(\frac{T}{2\pi}\right)^2$$

3. **Estabilização por Média de Múltiplos Ciclos:**
   Para reduzir a incerteza de uma única medição de período, o estudante pode configurar a quantidade de ciclos $n$ utilizada no cálculo da média, em troca de uma medida mais demorada:
   $$m = \frac{k}{4\pi^2} \cdot \left(\frac{1}{n}\sum_{i=1}^{n} T_i\right)^2$$

> **Nota:** Se a oscilação cessar (distância permanecer estável dentro de $0{,}2\text{ cm}$ do último pico e vale por mais de $3\text{ s}$), o sistema zera automaticamente o período e a massa calculados.

---

## ⚡ Recursos e Funcionalidades

* **Medição sem Contato:** Sensor ultrassônico HC-SR04 monitora a posição do porta-amostra sem introduzir atrito mecânico ao sistema massa-mola.
* **Filtro EMA:** Suavização exponencial do sinal bruto de distância para atenuar ruídos de reflexão do eco ultrassônico.
* **Detecção de Picos com Histerese:** Algoritmo robusto contra ruído que identifica ciclos completos de oscilação e calcula o período médio.
* **Amostragem Configurável:** Ajuste do número de ciclos ($1$–$10$) usados na média do período e do intervalo entre leituras do sensor ($20$–$200\text{ ms}$).
* **Cálculo Direto da Massa:** Conversão automática do período medido em massa inercial (gramas), a partir da constante elástica $k$ calibrada previamente por ensaio estático (Lei de Hooke).
* **Gráfico Dinâmico em Tempo Real:** Renderização por `HTML5 Canvas` da oscilação $h(t)$, com sobreposição do período $T$ e massa $m$ calculados.
* **Persistência de Dados (NVS):** Salva a constante elástica e os parâmetros de amostragem na memória flash via biblioteca `Preferences`.
* **Conectividade Integrada:** Resolução mDNS (`http://Balanca.local`) e comunicação de *heartbeat* periódico com a central de comunicação do ecossistema didático.

---

## 🛠️ Esquema de Hardware e Pinagem

### Componentes Necessários
* 1x Microcontrolador ESP32 DevKit v1
* 1x Sensor ultrassônico HC-SR04
* 1x Suporte universal de laboratório
* Molas ou elásticos de escritório
* 1x Copo descartável com clipes de papel (porta-amostra)
* Protoboard e Jumper Wires

### Tabela de Conexões

| Componente | Pino do HC-SR04 | Pino do ESP32 | Função |
| :--- | :--- | :--- | :--- |
| **HC-SR04** | VCC | **Vin** | Alimentação |
| **HC-SR04** | GND | **GND** | Referência Comum |
| **HC-SR04** | Trig | **GPIO 5** | Disparo do pulso ultrassônico (saída) |
| **HC-SR04** | Echo | **GPIO 18** | Leitura do tempo de eco (entrada) |

---

## 🌐 Arquitetura do Código e Endpoints

O firmware roda um servidor HTTP assíncrono na porta `80` e disponibiliza as seguintes rotas:

| Rota HTTP | Método | Descrição |
| :--- | :--- | :--- |
| `/` | `GET` | Entrega a interface *web* gráfica (HTML5/CSS/JavaScript). |
| `/status` | `GET` | Retorna um JSON com a constante elástica e os parâmetros de amostragem salvos. |
| `/dados` | `GET` | Retorna a leitura atual: `{"h": float, "T": float, "m": float}`. |
| `/set` | `GET` | Recebe parâmetros via *query params* (`?k=X&ciclos=Y&delay=Z`) para atualização instantânea. |
| `/save` | `GET` | Grava os valores atuais de calibração na memória NVS (`Preferences`). |

---

## 💻 Instalação e Configuração

### 1. Requisitos de Software
* [Arduino IDE](https://www.arduino.cc/en/software) ou [PlatformIO](https://platformio.org/)
* Suporte à placa ESP32 instalado na IDE
* Dependências de bibliotecas:
  * `ESPAsyncWebServer`
  * `HTTPClient` (incluso no core do ESP32)
  * `Preferences` (incluso no core do ESP32)
  * `ESPmDNS` (incluso no core do ESP32)
  * `math.h` (incluso no core do ESP32)

### 2. Configuração da Rede
No código-fonte, ajuste as credenciais do seu Ponto de Acesso Central, se necessário:
```cpp
const char* ssid     = "Rede_Comunicacao";
const char* password = "123456789";
```

---

## 🚀 Como Utilizar

1. Monte a estrutura física: fixe a mola ou elástico ao suporte universal, pendure o copo descartável (porta-amostra) e posicione o sensor ultrassônico logo abaixo, apontado para a base do copo.
2. **Caracterize a mola:** realize um ensaio estático aplicando a Lei de Hooke (massa conhecida e deformação linear) para determinar a constante elástica $k$ antes de qualquer medição.
3. Compile e carregue o firmware no ESP32 pela Arduino IDE.
4. Acesse a interface pelo navegador em `http://Balanca.local` ou pelo IP exibido no portal da Central de Comunicação.
5. Insira o valor de $k$ (N/m) determinado no ensaio estático no campo **k (N/m)** da interface.
6. Coloque a amostra de interesse no porta-amostra, provoque uma oscilação vertical e observe o período $T$ e a massa $m$ calculados em tempo real.
7. Ajuste o número de **Ciclos** para aumentar a estabilidade da leitura (em troca de uma medida mais demorada) e o **Delay** para adequar a janela de amostragem às limitações físicas do sensor.
8. Clique em **Salvar** para persistir a calibração na memória flash do ESP32, e compare as medidas com uma balança comercial para validar o modelo.
