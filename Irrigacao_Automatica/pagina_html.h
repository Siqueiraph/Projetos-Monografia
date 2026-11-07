// Página web da Irrigação Automática, servida direto da flash.
// Valores dinâmicos chegam por JSON: /status (calibração) e /dados (leituras).
#pragma once
#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="UTF-8">
  <title>Irrigação Automática</title>
  <style>
    :root {--destaque: #00ff00; --preto1: #0a0a0a; --preto2: #1a1a1a; --cinza1: #333; --cinza2: #ccc; --branco: #fff;}
    * { box-sizing: border-box; }
    html { font-size: clamp(14px, 1.2vw, 18px); }
    body { font-family: 'Segoe UI', Arial, sans-serif; background-color: var(--preto1); color: var(--cinza2); margin: 0; padding: 20px; display: flex; justify-content: center; min-height: 100vh;}

    /* ── Malha CSS Grid ── */
    .main-container { 
      display: grid; 
      grid-template-columns: 1.5fr 1fr; 
      grid-template-rows: 1fr 1fr 1fr;  /* 3 linhas de altura rigorosamente iguais */
      gap: 20px; 
      width: 100%; height: 100%;
      max-width: 1800px; 
      align-items: stretch;
    }

    /* ── Posicionamento na Grade ── */
    .graph-wrapper {
      grid-column: 1;
      grid-row: 1 / 3; /* O gráfico ocupa da linha 1 até o final da linha 2 */
      position: relative; background: var(--preto1); border: 1px solid var(--cinza1); border-radius: 12px; overflow: hidden; display: flex; flex-direction: column; min-height: 250px;
    }
    .c-calibra { grid-column: 2; grid-row: 1 / 2; }
    .c-rega    { grid-column: 2; grid-row: 2 / 3; }

    /* ── Elementos do Gráfico ── */
    canvas { display: block; width: 100%; flex-grow: 1; }
    #humOverlay { position: absolute; top: 14px; right: 20px; font-size: clamp(36px, 5vh, 50px); font-weight: bold; color: var(--destaque); text-shadow: 2px 2px 6px var(--preto1); z-index: 10; font-family: monospace; }
    #btnSave { position: absolute; top: 14px; left: 14px; z-index: 10; padding: 10px 16px; font-size: 14px; font-weight: bold; background-color: var(--preto2); color: var(--branco); border: 1px solid var(--cinza1); border-radius: 6px; cursor: pointer; transition: 0.2s; }
    #btnSave:hover { filter: brightness(1.4); }
    .btn-saved { background-color: #4CAF50 !important; border-color: #4CAF50 !important; color: var(--preto1) !important; }

    /* ── Cards e Controles ── */
    .card { background-color: var(--preto2); padding: 18px 20px; border-radius: 10px; border-left: 5px solid var(--destaque); box-shadow: 0 4px 10px rgba(0,0,0,0.4); display: flex; flex-direction: column; justify-content: center;}
    h3 { margin: 0 0 16px 0; font-size: clamp(14px, 3vh, 22px); color: var(--cinza2); border-bottom: 1px solid var(--cinza1); padding-bottom: 8px; letter-spacing: 1px;}
  
    .control-row { display: flex; align-items: center; gap: 10px; margin-top: 8px; }
    .control-row label { width: 60px; font-size: clamp(12px, 2.5vh, 16px); color: var(--cinza2); font-weight: bold; flex-shrink: 0; }
    .step-btn { background-color: var(--preto2); color: var(--branco); border: 1px solid var(--cinza1); border-radius: 6px; width: 36px; height: 36px; font-size: 18px; font-weight: bold; cursor: pointer; flex-shrink: 0; transition: 0.2s; }
    .step-btn:hover { filter: brightness(1.4); }
    
    input[type=number] { flex: 1; background: var(--preto2); color: var(--cinza2); border: 1px solid var(--cinza1); border-radius: 6px; padding: 8px; font-family: monospace; font-size: clamp(14px, 3vh, 18px); font-weight: bold; text-align: center; min-width: 0; }
    input[type=number]:focus { outline: none; filter: brightness(1.3); }
    input[type=number]::-webkit-inner-spin-button, input[type=number]::-webkit-outer-spin-button { -webkit-appearance: none; }
    input[type=number] { -moz-appearance: textfield; }

    /* ── Mobile ── */
    @media (max-width: 768px) {
      body { padding: 10px; }
      .main-container { 
        grid-template-columns: 1fr; /* Coluna única no mobile */
        grid-template-rows: auto;   /* Alturas baseadas no conteúdo */
        gap: 12px; 
      }
      /* Elementos assumem empilhamento natural */
      .graph-wrapper, .c-calibra, .c-rega {
        grid-column: 1;
        grid-row: auto;
      }
      .graph-wrapper { min-height: 250px; }
    }
  </style>
</head>
<body>
  <div class="main-container">
    <div class="graph-wrapper">
      <button id="btnSave" onclick="saveData()">Salvar</button>
      <div id="humOverlay">0.0%</div>
      <canvas id="plotCanvas"></canvas>
    </div>
    <div class="card c-calibra">
      <h3>CALIBRAÇÃO</h3>
      <div class="control-row">
        <label>Seco</label>
        <button class="step-btn" onclick="stepValue('vSeco', -50)">-</button>
        <input type="number" min="0" max="4095" id="vSeco" onchange="sendData()">
        <button class="step-btn" onclick="stepValue('vSeco', 50)">+</button>
      </div>
      <div class="control-row">
        <label>Úmido</label>
        <button class="step-btn" onclick="stepValue('vUmido', -50)">-</button>
        <input type="number" min="0" max="4095" id="vUmido" onchange="sendData()">
        <button class="step-btn" onclick="stepValue('vUmido', 50)">+</button>
      </div>
    </div>
    <div class="card c-rega">
      <h3>ACIONAMENTO</h3>
      <div class="control-row">
        <label>Limiar</label>
        <button class="step-btn" onclick="stepValue('limite', -1)">-</button>
        <input type="number" min="0" max="100" id="limite" onchange="sendData()">
        <button class="step-btn" onclick="stepValue('limite', 1)">+</button>
      </div>
      <div class="control-row">
        <label>Tempo</label>
        <button class="step-btn" onclick="stepValue('tempo', -1)">-</button>
        <input type="number" min="1" max="120" id="tempo" onchange="sendData()">
        <button class="step-btn" onclick="stepValue('tempo', 1)">+</button>
      </div>
    </div>
  </div>
  
  <script>
    let dataUmidade = [];
    const maxPts = 100;
    const canvas = document.getElementById('plotCanvas');
    const ctx = canvas.getContext('2d');
    for(let i = 0; i < maxPts; i++) dataUmidade.push(0);

    function resizeCanvas() {
      setTimeout(() => {
        canvas.width = canvas.parentElement.clientWidth;
        canvas.height = canvas.parentElement.clientHeight;
        drawCanvas();
      }, 50);
    }
    
    window.addEventListener('resize', resizeCanvas);
    window.onload = function() {
      resizeCanvas();
      fetch('/status').then(res => res.json()).then(data => {
        Object.keys(data).forEach(key => {
          let el = document.getElementById(key);
          if(el) { el.value = data[key]; }
        });
      });
    };

    function stepValue(id, delta) {
      let el = document.getElementById(id);
      let val = (parseInt(el.value) || 0) + delta;
      val = Math.max(parseInt(el.min), Math.min(parseInt(el.max), val));
      el.value = val;
      sendData();
    }

    function sendData() {
      let s = document.getElementById('vSeco').value;
      let u = document.getElementById('vUmido').value;
      let l = document.getElementById('limite').value;
      let t = document.getElementById('tempo').value;
      fetch(`/set?seco=${s}&umido=${u}&limite=${l}&tempo=${t}`);
    }

    function saveData() {
      let btn = document.getElementById('btnSave');
      fetch('/save').then(() => {
        btn.innerText = "Salvo!";
        btn.classList.add('btn-saved');
        setTimeout(() => { btn.innerText = "Salvar"; btn.classList.remove('btn-saved'); }, 2000);
      });
    }

    setInterval(function() {
      fetch('/dados').then(res => res.json()).then(data => {
        document.getElementById('humOverlay').innerText = data.bloqueio ? "Verifique o sensor" : data.umidade + "%";
        dataUmidade.push(data.umidade);
        if(dataUmidade.length > maxPts) dataUmidade.shift();
        drawCanvas();
      });
    }, 1000);

    function drawCanvas() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      let limEl = document.getElementById('limite');
      let lim = limEl ? parseInt(limEl.value) : 30;
      let scale = canvas.height / 100;
      let yLim = canvas.height - (lim * scale);
      ctx.setLineDash([5, 5]); ctx.strokeStyle = '#555'; ctx.beginPath(); ctx.moveTo(0, yLim); ctx.lineTo(canvas.width, yLim); ctx.stroke();
      ctx.setLineDash([]); ctx.strokeStyle = '#00ff00'; ctx.lineWidth = 3; ctx.beginPath();
      let step = canvas.width / (maxPts - 1);
      dataUmidade.forEach((v, i) => {
        let x = i * step; let y = canvas.height - (v * scale);
        if(i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      });
      ctx.stroke();
    }
  </script>
</body>
</html>
)rawliteral";
