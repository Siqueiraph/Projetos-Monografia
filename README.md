# Projetos da Monografia — Ecossistema Didático de Física com ESP32

Códigos-fonte e documentação dos módulos experimentais desenvolvidos na monografia. Cada módulo é um experimento de Física de baixo custo, construído em torno de um microcontrolador **ESP32**, que mede uma grandeza, calcula os resultados em tempo real e os apresenta em uma interface *web* embarcada, acessível pelo navegador de qualquer celular ou computador, sem instalação de aplicativos.

**Monografia disponível em:** https://repositorio.unicamp.br/Acervo/Detalhe/1559550

---

## 🧭 Visão Geral

O conjunto é formado por **quatro módulos experimentais** e uma **Central de Comunicação** que os integra:

* A **Central** cria a própria rede Wi-Fi (`Rede_Comunicacao`), dispensando roteador ou internet, e publica um portal em `http://rede.local` com os módulos que estão ativos.
* Cada **módulo experimental** se conecta a essa rede, anuncia sua presença à Central por *pings* periódicos e serve a própria página, com gráfico em tempo real e campos de calibração.
* Os parâmetros de calibração são ajustados pelo navegador e podem ser salvos na memória flash do ESP32, sem necessidade de recompilar o firmware.

Os módulos também funcionam de forma independente: a Central apenas facilita o acesso quando vários operam ao mesmo tempo.

---

## 📚 Projetos e Documentação

| Projeto | Tema de Física | O que faz | Documentação |
| :--- | :--- | :--- | :--- |
| **Central de Comunicação** | — | Ponto de acesso Wi-Fi, portal *web* e display OLED com o status dos módulos | [Documentação](Rede_Comunicacao/Documenta%C3%A7%C3%A3o%20Rede%20Comunicacao.md) |
| **Anemômetro Digital** | Cinemática rotacional | Mede a velocidade angular de um rotor com sensor magnético (Reed Switch ou Hall) e calcula a velocidade do vento | [Documentação](Anemometro_Digital/Documenta%C3%A7%C3%A3o%20Anemometro%20Digital.md) |
| **Balança Inercial** | Oscilador harmônico simples | Mede o período de um sistema massa-mola com sensor ultrassônico e infere a massa da amostra | [Documentação](Balanca_Inercial/Documenta%C3%A7%C3%A3o%20Balanca%20Inercial.md) |
| **Controle Musical** | Acústica e processamento de sinais | Separa o som ambiente em três bandas de frequência (grave, médio e agudo) e aciona relés ou LEDs conforme a intensidade | [Documentação](Controle_Musical/Documenta%C3%A7%C3%A3o%20Controle%20Musical.md) |
| **Irrigação Automática** | Eletrodinâmica (divisor de tensão) | Mede a umidade do solo pela sua resistência elétrica e aciona uma bomba de água | [Documentação](Irrigacao_Automatica/Documenta%C3%A7%C3%A3o%20Irrigacao%20Automatica.md) |

Cada documentação traz a fundamentação física, a lista de componentes e a pinagem, as rotas HTTP do firmware e o passo a passo de instalação e uso.

---

## 📁 Estrutura do Repositório

Cada projeto fica em uma pasta própria, que contém o *sketch* (`.ino`) e a respectiva documentação (`.md`):

```
Rede_Comunicacao/       Central de Comunicação
Anemometro_Digital/     Anemômetro Digital
Balanca_Inercial/       Balança Inercial
Controle_Musical/       Controle Musical
Irrigacao_Automatica/   Irrigação Automática
```

---

## 🚀 Por Onde Começar

1. Instale a [Arduino IDE](https://www.arduino.cc/en/software) com o suporte à placa ESP32.
2. Abra a pasta do projeto desejado e siga a seção **Instalação e Configuração** da documentação correspondente, que lista as bibliotecas necessárias.
3. Para usar vários módulos em conjunto, grave primeiro a [Central de Comunicação](Rede_Comunicacao/Documenta%C3%A7%C3%A3o%20Rede%20Comunicacao.md) e mantenha nos demais as credenciais da rede `Rede_Comunicacao`.
