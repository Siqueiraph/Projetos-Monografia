// Página web do portal da Rede de Comunicação, servida direto da flash.
// Valores dinâmicos chegam por JSON: /api/projetos (módulos cadastrados e status).
#pragma once
#include <Arduino.h>

const char portal_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="UTF-8">
  <title>Rede de Comunicação</title>
  <style>
    body { font-family: 'Segoe UI', Arial; background: #0a0a0a; color: white; text-align: center; padding: 20px; }
    h1 { color: #ffffff; font-size: 24px; }
    .container { display: flex; flex-direction: column; gap: 15px; align-items: center; }
    .btn { display: block; width: 90%; max-width: 350px; padding: 20px; 
           text-decoration: none; color: white; border-radius: 12px; font-weight: bold; 
           transition: 0.3s; border: 1px solid #333; text-transform: capitalize;}
    .online { background: #1e90ff; border-color: #fff; box-shadow: 0 4px 15px rgba(30,144,255,0.4); }
    .offline { background: #222; color: #555; pointer-events: none; }
    .status-dot { height: 10px; width: 10px; border-radius: 50%; display: inline-block; margin-right: 10px; }
    .dot-on { background-color: #00ff00; }
    .dot-off { background-color: #ff0000; }
  </style>
</head>
<body>
  <h1>Interface de Acesso</h1>
  <div class="container" id="links">Carregando...</div>
  <script>
    function atualizar() {
      fetch('/api/projetos').then(res => res.json()).then(data => {
        let html = "";
        data.forEach(p => {
          let status = p.ativo ? "online" : "offline";
          let dot = p.ativo ? "dot-on" : "dot-off";
          let link = p.ativo ? `href="http://${p.ip}"` : "";
          html += `<a ${link} class="btn ${status}"><span class="status-dot ${dot}"></span>${p.nome}</a>`;
        });
        document.getElementById('links').innerHTML = html;
      });
    }
    setInterval(atualizar, 3000);
    atualizar();
  </script>
</body></html>
)rawliteral";
