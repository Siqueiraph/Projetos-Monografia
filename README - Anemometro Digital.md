# Anemômetro Digital - Monitor de Velocidade do Vento

Módulo experimental de automação e aquisição de dados do ecossistema didático de Física baseado no microcontrolador **ESP32** e em um **sensor magnético** que detecta a passagem de ímãs fixados a um rotor. O sistema captura os pulsos gerados pela rotação, calcula grandezas cinemáticas em tempo real e disponibiliza uma interface *web* interativa para calibração de parâmetros geométricos/aerodinâmicos e visualização gráfica.

O projeto é distribuído em **duas versões de firmware**, uma para cada sensor: **Efeito Hall (KY-003)** e **Reed Switch (KY-025)**. A física e a calibração são idênticas nas duas, e ambas usam apenas o sinal digital do sensor; a versão Reed acrescenta um filtro contra o repique do contato mecânico, o cálculo da velocidade por média em janela de tempo e um gráfico com grade, congelamento (*hold*) e cursor de leitura.

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
| **Sensor** | Efeito Hall KY-003 (3 pinos: S, VCC, GND) | Reed Switch KY-025 (4 pinos: AO, G, +, DO — o pino AO não é utilizado) |
| **Princípio de detecção** | Semicondutor: o campo magnético comuta um transistor interno, sem partes móveis | Eletromecânico: o campo magnético fecha um contato de lâminas metálicas |
| **Sinal digital** | GPIO 14, `INPUT_PULLUP`, interrupção em `FALLING` | GPIO 14, `INPUT_PULLDOWN`, interrupção em `CHANGE` (o pulso é contado na borda de subida) |
| **Filtro de pulsos** | *Debounce* de $15\text{ ms}$ | *Debounce* de $15\text{ ms}$ + largura mínima de $1\text{ ms}$ contra repique do contato |
| **Cálculo da velocidade** | A cada pulso, a partir do último intervalo $\Delta t$ | A cada $1\text{ s}$, a partir do intervalo médio de todos os pulsos da janela |
| **Resolução de tempo** | $1\text{ ms}$ (`millis`) | $1\text{ µs}$ (`micros`) |
| **Gráfico** | Curvas de $v$ e $\omega$ | Curvas de $v$ e $\omega$ em três eixos com grade, escala com piso e teto, botão **Hold** e cursor de leitura |
| **Intervalo do *ping* à Central** | $5\text{ s}$ | $30\text{ s}$ |

**Qual escolher:**
* **Hall** — versão base, com o firmware mais curto e direto de explicar. O sensor não tem partes móveis e, por isso, não sofre de repique.
* **Reed** — indicada para atividades em que os estudantes precisam *extrair valores do gráfico* para cálculos, graças à grade, ao *hold* e ao cursor, e em que se deseja uma leitura mais estável.

> **Nota:** as duas versões usam o mesmo nome mDNS (`Anemometro.local`), o mesmo identificador de *ping* (`Anemometro`) e o mesmo espaço de calibração na memória flash (`anemo_cfg`). Por isso, não devem operar simultaneamente na mesma rede; em contrapartida, a calibração salva é preservada ao trocar de uma versão para a outra no mesmo ESP32.

---

## 🔬 Fundamentação Física

O princípio de funcionamento baseia-se na contagem de pulsos causados pela passagem de ímãs fixados ao rotor diante do sensor magnético. O firmware utiliza uma rotina de interrupção (ISR) para mensurar o intervalo de tempo $\Delta t$ (em milissegundos) entre detecções consecutivas. As equações abaixo valem para as duas versões; o que muda é a forma de obter $\Delta t$ (ver [Estabilidade da medição](#estabilidade-da-medição-somente-versão-reed)).

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

### Estabilidade da medição (somente versão Reed)

Na versão Hall, $\Delta t$ é o intervalo entre os dois últimos pulsos, de modo que qualquer irregularidade em um único pulso aparece diretamente na leitura. A versão Reed adota duas medidas para estabilizá-la:

1. **Média em janela de tempo.** A interrupção apenas conta os pulsos e registra o instante do último. A cada janela de $1\text{ s}$, o firmware divide o tempo decorrido entre o último pulso da janela anterior e o último pulso da janela atual ($\Delta t_{\text{janela}}$) pelo número de pulsos ocorridos nesse trecho ($n$):
   $$\Delta t = \frac{\Delta t_{\text{janela}}}{n}$$
   Como todos os pulsos entram na conta, as flutuações individuais se compensam. Em contrapartida, a leitura passa a responder a variações de vento com atraso de até $1\text{ s}$.

2. **Filtro de repique (*bounce*).** O *reed switch* é um contato mecânico: ao fechar e ao abrir, as lâminas oscilam por uma fração de milissegundo e podem gerar bordas espúrias. O firmware observa as duas bordas do sinal e só aceita uma borda de subida se o nível baixo que a antecedeu durou pelo menos $1\text{ ms}$, além do *debounce* de $15\text{ ms}$ entre pulsos válidos.

> **Nota:** a janela de média e os limites do filtro são constantes no início do *sketch* (`JANELA_MEDIA_MS`, `LARGURA_MIN_US` e `DEBOUNCE_US`). Aumentar a janela suaviza ainda mais a curva, ao custo de uma resposta mais lenta.

> **Nota:** parte da oscilação observada no gráfico é real — o vento de um ventilador ou de um sopro é turbulento, e o rotor acelera e desacelera de fato. A média reduz o ruído de medição, não a variação física do escoamento.

---

## ⚡ Recursos e Funcionalidades

### Comuns às duas versões
* **Amostragem Baseada em Interrupção:** Leitura precisa de tempo de pulso via ISR com *debouncing* por software ($15\text{ ms}$) para atenuar ruídos mecânicos/magnéticos.
* **Interface Web Responsiva Integrada:** Servidor *web* assíncrono embarcado em HTML/CSS/JS.
* **Gráfico Dinâmico em Tempo Real:** Renderização por `HTML5 Canvas` que exibe a velocidade do vento $v$ ($\text{m/s}$) e a velocidade angular $\omega$ ($\text{rad/s}$) em um histórico contínuo de cerca de $50\text{ s}$, atualizado a cada $500\text{ ms}$.
* **Calibração Dinâmica:** Ajuste dos parâmetros geométricos (Raio e Ímãs) e aerodinâmicos (Fator $K$) diretamente pelo navegador, sem necessidade de recompilar o firmware.
* **Persistência de Dados (NVS):** Salva as configurações de calibração na memória flash via biblioteca `Preferences`, preservando-as após reinicializações.
* **Conectividade Integrada:** Resolução mDNS (`http://Anemometro.local`) e comunicação de *heartbeat*/ping periódico com a central de comunicação do ecossistema didático.

### Exclusivo da versão Reed
* **Medição Estabilizada:** Velocidade calculada pela média dos pulsos em janelas de $1\text{ s}$, com filtro de repique do contato e tempos medidos em microssegundos.
* **Gráfico de Três Eixos com Grade:** Eixo horizontal **Tempo passado [s]** (0 = leitura mais recente, à direita), eixo vertical esquerdo **Velocidade Tangencial [m/s]** e eixo vertical direito **Velocidade Angular [rad/s]**. Cada curva é lida no eixo da sua cor (laranja à esquerda, cinza à direita), e as linhas da grade coincidem com as divisões dos dois eixos verticais.
* **Escala com Piso e Teto:** Os eixos verticais sempre partem de zero, e o valor do topo se ajusta automaticamente apenas entre limites fixos — de $5$ a $50\text{ m/s}$ e de $25$ a $250\text{ rad/s}$. O piso impede que a escala amplie o gráfico quando a velocidade é baixa ou estável, o que faria pequenas flutuações parecerem maiores do que são. Os limites são as constantes `ESCALA_V` e `ESCALA_W` no código da página.
* **Separador Decimal Uniforme:** Todos os valores da interface (leituras, eixos, cursor e campo **Fator K**) usam ponto decimal, independentemente do idioma do navegador, e o campo aceita a digitação com ponto ou com vírgula. O separador é definido pela constante `SEP_DECIMAL` no código da página.
* **Botão Hold:** Congela o gráfico e os valores exibidos para que possam ser lidos e anotados; **Retomar** volta à aquisição. O trecho decorrido durante a pausa não é registrado.
* **Cursor de Leitura:** Ao passar o mouse (ou tocar) sobre o gráfico, uma linha vertical marca o ponto mais próximo e exibe seu instante $t$, $v$ e $\omega$ — o que permite, por exemplo, obter $\Delta v / \Delta t$ entre dois instantes.

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
| **Sensor Reed** | AO (Saída Analógica) | — | Não conectado |
| **Sensor Reed** | + (VCC) | **3.3V** | Alimentação |
| **Sensor Reed** | G (GND) | **GND** | Ponto de Referência Comum |

> **Nota:** alimente o módulo em **3.3V**. O nível alto da saída DO acompanha a tensão de alimentação do módulo, e os pinos do ESP32 não toleram 5V.

> **Nota:** o limiar do pino DO é ajustado fisicamente pelo trimpot do módulo. O firmware usa `INPUT_PULLDOWN` e interrupção nas duas bordas (`CHANGE`), contando o pulso na borda de subida.

---

## 🌐 Arquitetura do Código e Endpoints

O firmware roda um servidor HTTP assíncrono na porta `80` e disponibiliza as seguintes rotas, iguais nas duas versões:

| Rota HTTP | Método | Descrição |
| :--- | :--- | :--- |
| `/` | `GET` | Entrega a interface *web* gráfica (HTML5/CSS/JavaScript). |
| `/dados` | `GET` | Retorna um JSON com as leituras atuais: `{"v": float, "w": float}`. |
| `/status` | `GET` | Retorna o estado atual das variáveis de calibração salvas. |
| `/set` | `GET` | Recebe parâmetros via *query params* (`?raio=X&imas=Y&fator=Z`) para atualização instantânea. |
| `/save` | `GET` | Grava os valores atuais de calibração na memória NVS (`Preferences`). |

Na versão Reed, a grade, o *hold* e o cursor de leitura são implementados inteiramente no navegador, sobre os dados da rota `/dados`.

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
7. **(Somente versão Reed)** Para extrair valores, clique em **Hold** para congelar o gráfico e passe o mouse (ou toque) sobre a curva: o cursor exibe o instante $t$ e os valores de $v$ e $\omega$ do ponto selecionado, e a grade permite estimar intervalos de tempo e de velocidade. Clique em **Retomar** para voltar à aquisição. Se nenhuma velocidade for registrada com o rotor girando, ajuste o trimpot do módulo.
8. Ajuste o **Fator K** comparando a leitura do dispositivo com uma referência conhecida (anemômetro comercial ou velocidade nominal do ventilador), calibrando o fator de correção aerodinâmico das pás.
9. Clique em **Salvar** para persistir a calibração na memória flash do ESP32.
