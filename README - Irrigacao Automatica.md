# Irrigação Automática - Monitor de Umidade do Solo e Acionamento de Bomba

Módulo experimental de eletrodinâmica (condução elétrica e divisores de tensão) do ecossistema didático de Física baseado no microcontrolador **ESP32**. O sistema utiliza duas hastes metálicas inseridas no solo como elemento resistivo de um divisor de tensão, convertendo a umidade do substrato em uma leitura analógica. Uma bomba de água de $5\text{ V}$ é acionada automaticamente por meio de um relé sempre que a umidade cai abaixo de um limiar configurável, respeitando um tempo de rega e um período de resfriamento (*cooldown*) entre acionamentos. A interface *web* embarcada disponibiliza visualização gráfica contínua e calibração dos dois pontos de referência de solo (seco/úmido).

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

O solo seco comporta-se como um meio isolante, enquanto o solo úmido conduz corrente elétrica graças aos sais minerais dissolvidos na água. O ESP32 explora essa propriedade por meio de um divisor de tensão formado pela resistência do solo ($R_{solo}$) em série com um resistor de referência conhecido ($R_{ref} = 10\text{ k}\Omega$):

1. **Divisor de Tensão:**
   A partir das Leis de Kirchhoff, a tensão lida no pino analógico ($V_{out}$) relaciona-se com a tensão de alimentação ($V_{in} = 3{,}3\text{ V}$) por:
   $$V_{out} = V_{in} \cdot \left(\frac{R_{ref}}{R_{solo} + R_{ref}}\right)$$
   Quando o solo está seco, $R_{solo}$ tende a valores muito altos e $V_{out}$ decresce em direção a zero; à medida que o solo fica úmido, $R_{solo}$ diminui e $V_{out}$ cresce em direção a $V_{in}$.

2. **Calibração por Interpolação Linear:**
   O Conversor Analógico-Digital (ADC) de 12 bits do ESP32 entrega uma leitura bruta entre $0$ e $4095$. O firmware converte essa leitura em um percentual de umidade por meio de uma interpolação linear entre dois pontos calibrados experimentalmente — a leitura do solo completamente seco ($ADC_{seco}$) e a do solo saturado de água ($ADC_{umido}$):
   $$H_\% = \left(\frac{ADC_{atual} - ADC_{seco}}{ADC_{umido} - ADC_{seco}}\right) \cdot 100 \quad \text{(limitado ao intervalo } [0,\,100]\,\%\text{)}$$
   Como cada tipo de solo possui composição química e, portanto, condutividade distinta, os dois pontos de calibração devem ser redeterminados sempre que o substrato for alterado.

---

## ⚡ Recursos e Funcionalidades

* **Leitura Resistiva do Solo:** Conversão da resistência variável do solo em sinal de tensão analógico, sem a necessidade de sensores capacitivos comerciais.
* **Calibração de Dois Pontos:** Ajuste independente dos valores brutos de ADC para solo "Seco" e "Úmido" diretamente pela interface.
* **Acionamento Automático da Bomba:** Liga a bomba quando a umidade cai abaixo do limiar configurado, por um tempo de rega ajustável (em segundos).
* **Cooldown Anti-Encharcamento:** Período fixo de $60\text{ s}$ entre o fim de uma rega e o início da próxima, evitando acionamentos excessivos mesmo que a leitura oscile perto do limiar.
* **Gráfico Dinâmico em Tempo Real:** Renderização por `HTML5 Canvas` da umidade percentual, com linha tracejada indicando o limiar de acionamento.
* **Persistência de Dados (NVS):** Salva os pontos de calibração e os parâmetros de acionamento na memória flash via biblioteca `Preferences`.
* **Conectividade Integrada:** Resolução mDNS (`http://Irrigacao.local`) e comunicação de *heartbeat* periódico com a central de comunicação do ecossistema didático.

---

## 🛠️ Esquema de Hardware e Pinagem

### Componentes Necessários
* 1x Microcontrolador ESP32 DevKit v1
* 2x Pregos galvanizados ou hastes de metal (eletrodos inseridos no solo)
* 1x Resistor de $10\ \text{k}\Omega$ (referência do divisor de tensão)
* 1x Módulo relé
* 1x Bomba de água de $5\text{ V}$
* 1x Fonte ou bateria de $5\text{ V}$
* Protoboard e Jumper Wires

### Tabela de Conexões

| Componente | Pino do Componente | Pino do ESP32 | Função |
| :--- | :--- | :--- | :--- |
| **Haste 1** | — | **3V3** | Alimentação do divisor de tensão |
| **Haste 2** | — | **GPIO 34** | Leitura analógica (ADC, somente entrada) |
| **Resistor $10\text{k}\Omega$** | — | **GND** ↔ **GPIO 34** | Referência do divisor de tensão |
| **Módulo Relé** | VCC | **VIN** | Alimentação do módulo |
| **Módulo Relé** | GND | **GND** | Referência Comum |
| **Módulo Relé** | IN | **GPIO 26** | Sinal de acionamento da bomba |

---

## 🌐 Arquitetura do Código e Endpoints

O firmware roda um servidor HTTP assíncrono na porta `80` e disponibiliza as seguintes rotas:

| Rota HTTP | Método | Descrição |
| :--- | :--- | :--- |
| `/` | `GET` | Entrega a interface *web* gráfica (HTML5/CSS/JavaScript). |
| `/status` | `GET` | Retorna um JSON com os parâmetros de calibração e acionamento salvos. |
| `/dados` | `GET` | Retorna a leitura atual: `{"umidade": int, "statusBomba": bool}`. |
| `/set` | `GET` | Recebe parâmetros via *query params* (`?seco=X&umido=Y&limite=Z&tempo=W`) para atualização instantânea. |
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

### 2. Configuração da Rede
No código-fonte, ajuste as credenciais do seu Ponto de Acesso Central, se necessário:
```cpp
const char* ssid     = "Rede_Comunicacao";
const char* password = "123456789";
```

---

## 🚀 Como Utilizar

1. Monte o circuito conforme a tabela de pinagem, inserindo as hastes metálicas no vaso ou canteiro a ser monitorado.
2. Compile e carregue o firmware no ESP32 pela Arduino IDE.
3. Acesse a interface pelo navegador em `http://Irrigacao.local` ou pelo IP exibido no portal da Central de Comunicação.
4. Com o solo completamente seco, anote a leitura bruta e insira-a no campo **Seco**; em seguida, umedeça bem o solo (ou mergulhe as hastes em água) e insira a nova leitura no campo **Úmido**.
5. Defina o **Limiar** (percentual mínimo de umidade antes do acionamento) e o **Tempo** de rega em segundos.
6. Clique em **Salvar** para persistir a calibração na memória flash do ESP32.
7. Observe o gráfico de umidade em tempo real e o acionamento automático da bomba sempre que a leitura cruzar o limiar definido.
