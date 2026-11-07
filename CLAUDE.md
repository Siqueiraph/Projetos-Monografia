# CLAUDE.md

Este arquivo orienta o Claude Code (claude.ai/code) ao trabalhar com o código deste repositório.

## Visão geral

Cinco sketches Arduino para ESP32 DevKit v1: a central `Rede_Comunicacao/` (SoftAP + portal + OLED) e quatro módulos (`Anemometro_Digital/`, `Balanca_Inercial/`, `Controle_Musical/`, `Irrigacao_Automatica/`). Não há bibliotecas compartilhadas: cada pasta tem o `.ino` e a página web em `pagina_html.h` (`index_html` nos módulos, `portal_html` na central). Os quatro módulos têm `MODO_AUTONOMO` (`true` = SoftAP próprio em 192.168.4.1, sem ping; `false` = estação na rede da central).

`scripts/visualizar_interface.py <projeto>` serve o `pagina_html.h` de um projeto (`anemometro`, `balanca`, `irrigacao`, `musical`, `rede`) com respostas JSON fixas, só para ver o layout sem o ESP32 (só biblioteca padrão). Não simula nada.

## Compilação e verificação

- Não há build por linha de comando, testes, linter nem CI. Compilação e upload são feitos manualmente na Arduino IDE; `arduino-cli` e PlatformIO não estão instalados.
- Ao terminar uma alteração, diga explicitamente que o código não foi compilado nem testado no hardware.
- Nenhuma versão de biblioteca é fixada. Dependências externas: `ESPAsyncWebServer` e `AsyncTCP` (todos), `Adafruit_SSD1306` e `Adafruit_GFX` (central). Não adicione bibliotecas novas (ex.: ArduinoJson) sem pedir.

## O que precisa ficar em sincronia

- `tarefaPing()` e `conectarWiFi()` estão duplicadas nos quatro módulos e só diferem no nome do ping e do mDNS: uma mudança em uma deve ser replicada nas outras. A rede própria usa sempre `ssidAP` (`String`), `senhaAP`, `CANAL_WIFI` e `MAX_CLIENTES`; no Anemômetro, `ssidAP` e `CANAL_WIFI` derivam de `ID_ANEMOMETRO`.
- O `nome` enviado em `/ping?nome=` por cada módulo deve ser idêntico a uma entrada de `listaProjetos[]` na central. O Anemômetro envia `Anemometro <ID>` (espaço codificado como `%20`). A central cadastra só `Anemometro A`; outras letras exigem editar `listaProjetos[]`. O laço do OLED assume quatro projetos (`i < 4`); os demais laços percorrem a lista inteira.
- `INTERVALO_PING_MS` (5 s) deve ficar abaixo de `TIMEOUT_INATIVO_MS` da central (15 s).
- Em todos os módulos o nome do parâmetro de `/set` é igual à chave NVS, e o namespace é `<modulo>_cfg` (`anemometro_cfg`, `balanca_cfg`, `irrigacao_cfg`, `musical_cfg`; máximo de 15 caracteres). As chaves do JSON de `/status` são outro conjunto. Em Irrigação e Musical elas devem ser iguais aos `id` dos inputs (a página percorre as chaves genericamente). No Musical os três coincidem: a página envia `/set?<id>=<valor>`.
- As respostas fixas de `scripts/visualizar_interface.py` usam as chaves de JSON de cada firmware: ao renomear uma chave ou rota, atualize o script.
- Cada pasta tem um `Documentação <Nome>.md` que repete pinos, endpoints, constantes e valores padrão. Atualize o documento no mesmo commit que o código.

## Estilo

- Identificadores e comentários em português, camelCase sem acentos (`velocidadeAngular`, `tarefaPing`). Constantes em UPPER_SNAKE com sufixo de unidade (`DEBOUNCE_US`, `JANELA_MEDIA_MS`); pinos com prefixo `PINO_`. Textos da interface em português.
- Cabeçalho do arquivo com exatamente duas linhas: `// <Nome> - <descrição>` e `// Hardware: ...`.
- Seções com faixas de comentário em maiúsculas, nesta ordem: includes, rede, pinos, parâmetros, globais (incluindo funções auxiliares do módulo, como ISR, filtros e relés), ping, `conectarWiFi`, `setup`, `loop`. A central não tem ping nem `conectarWiFi`. Não há seção HTML: a página vem de `pagina_html.h`, incluído após os demais includes.
- Página servida com `request->send(200, "text/html", index_html)` (não use `send_P`). Números mágicos viram constantes na seção de parâmetros.
- Indentação de 2 espaços, chaves K&R, `=` e comentários finais alinhados em coluna.
- `loop()` não bloqueante: `unsigned long agora = millis()` com timestamps `static`. `delay()` só em `conectarWiFi()`; FreeRTOS só na tarefa de ping.
- JSON montado por concatenação de `String`.

## Armadilhas

- A página de cada projeto fica dentro de `R"rawliteral( ... )rawliteral"` em `pagina_html.h`; o conteúdo não pode conter `)rawliteral"`.
- Relés com lógica oposta: ativo em LOW no Musical, ativo em HIGH na Irrigação.
- Musical: Fs = 9000 Hz, então as bordas das bandas devem ficar abaixo de 4500 Hz (`calcularBase` retorna em silêncio caso contrário).
- Anemômetro: Hall usa `INPUT_PULLUP` e Reed usa `INPUT_PULLDOWN`, selecionados por `SENSOR_HALL`; o KY-025 deve ser alimentado em 3,3 V.
- O arquivo `Balanca_Inercial/Balanca_inercial.ino` tem "i" minúsculo, diferente da pasta. Não renomeie sem pedir.
- Os nomes dos documentos têm acento e espaço; links em Markdown usam a forma codificada (`Documenta%C3%A7%C3%A3o%20...`).

## Git

- Não faça commit sem pedido. Ao terminar uma alteração, sugira uma mensagem de commit breve no padrão Conventional Commits, em português (`feat: adiciona ...`, `fix: corrige ...`, `refactor: ...`, `docs: ...`).
- Só crie branch quando o usuário pedir (nomes `feature/<descricao-em-kebab-case>` ou `fix/<descricao>`).
- O `.gitattributes` (`* text=auto eol=lf`) impõe LF no repositório e na cópia de trabalho, mesmo com `core.autocrlf=true`.
