# CLAUDE.md

Este arquivo orienta o Claude Code (claude.ai/code) ao trabalhar com o código deste repositório.

## Visão geral

Cinco sketches Arduino de arquivo único para ESP32 DevKit v1: a central `Rede_Comunicacao/` (SoftAP + portal + OLED) e quatro módulos (`Anemometro_Digital/`, `Balanca_Inercial/`, `Controle_Musical/`, `Irrigacao_Automatica/`). Não há headers nem bibliotecas compartilhadas: cada `.ino` é autocontido, com a página web embutida.

## Compilação e verificação

- Não há build por linha de comando, testes, linter nem CI. Compilação e upload são feitos manualmente na Arduino IDE; `arduino-cli` e PlatformIO não estão instalados.
- Ao terminar uma alteração, diga explicitamente que o código não foi compilado nem testado no hardware.
- Nenhuma versão de biblioteca é fixada. Dependências externas: `ESPAsyncWebServer` e `AsyncTCP` (todos), `Adafruit_SSD1306` e `Adafruit_GFX` (central). Não adicione bibliotecas novas (ex.: ArduinoJson) sem pedir.

## O que precisa ficar em sincronia

- `tarefaPing()` e `conectarWiFi()` estão duplicadas nos quatro módulos: uma mudança em uma deve ser replicada nas outras.
- O `nome` enviado em `/ping?nome=` por cada módulo deve ser idêntico a uma entrada de `listaProjetos[]` na central. O tamanho 4 dessa lista está fixo em vários laços (e `i < 3` na montagem do JSON) e o layout do OLED assume quatro linhas.
- `INTERVALO_PING_MS` (5 s) deve ficar abaixo do tempo limite de inatividade da central (15 s).
- Em Irrigação e Musical, as chaves do JSON de `/status` devem ser iguais aos `id` dos inputs do HTML (a página percorre as chaves genericamente). Nomes de parâmetros no JS, nos handlers e as chaves NVS são três conjuntos distintos: confira os três.
- Cada pasta tem um `Documentação <Nome>.md` que repete pinos, endpoints, constantes e valores padrão. Atualize o documento no mesmo commit que o código.

## Configuração de bancada vs. produção

O estado versionado é o de bancada: SSID `"ESP IoT"` ativo no Anemômetro e no Musical, e `listaProjetos[]` com `"Anemometro A".."Anemometro D"` (os valores de produção estão comentados logo ao lado). Não "corrija" isso sem que seja pedido.

## Estilo

- Identificadores e comentários em português, camelCase sem acentos (`velocidadeAngular`, `tarefaPing`). Constantes em UPPER_SNAKE com sufixo de unidade (`DEBOUNCE_US`, `JANELA_MEDIA_MS`); pinos com prefixo `PINO_`. Textos da interface em português.
- Cabeçalho do arquivo com exatamente duas linhas: `// <Nome> - <descrição>` e `// Hardware: ...`.
- Seções com faixas de comentário em maiúsculas, nesta ordem: includes, rede, pinos, parâmetros, globais, HTML, ping, `conectarWiFi`, `setup`, `loop`.
- Indentação de 2 espaços, chaves K&R, `=` e comentários finais alinhados em coluna.
- `loop()` não bloqueante: `unsigned long agora = millis()` com timestamps `static`. `delay()` só em `conectarWiFi()`; FreeRTOS só na tarefa de ping.
- JSON montado por concatenação de `String`.

## Armadilhas

- A página de cada módulo fica dentro de `R"rawliteral( ... )rawliteral"` no `.ino`; o conteúdo não pode conter `)rawliteral"`.
- Relés com lógica oposta: ativo em LOW no Musical, ativo em HIGH na Irrigação.
- Musical: Fs = 9000 Hz, então as bordas das bandas devem ficar abaixo de 4500 Hz (`calcularBase` retorna em silêncio caso contrário).
- Anemômetro: Hall usa `INPUT_PULLUP` e Reed usa `INPUT_PULLDOWN`, selecionados por `SENSOR_HALL`; o KY-025 deve ser alimentado em 3,3 V.
- O arquivo `Balanca_Inercial/Balanca_inercial.ino` tem "i" minúsculo, diferente da pasta. Não renomeie sem pedir.
- Os nomes dos documentos têm acento e espaço; links em Markdown usam a forma codificada (`Documenta%C3%A7%C3%A3o%20...`).

## Git

- Mensagens de commit em português, uma frase descritiva na terceira pessoa do presente ("Adiciona...", "Atualiza...", "Remove..."), sem prefixos de conventional commits.
- Antes de mudanças substanciais, crie um branch `feature/<descricao-em-kebab-case>` ou `fix/<descricao>` em vez de commitar direto na `main`.
