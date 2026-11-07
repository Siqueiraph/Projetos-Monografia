// Página web do Controle Musical, servida direto da flash.
// Valores dinâmicos chegam por JSON: /status (calibração); o gráfico chega por SSE em /events.
#pragma once
#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="UTF-8">
  <title>Controle Musical</title>
  <style>
    :root {--preto1: #0a0a0a; --preto2: #1a1a1a; --cinza1: #333; --cinza2: #ccc; --branco: #fff; }
    * { box-sizing: border-box; }
    html { font-size: clamp(14px, 1.2vw, 18px); }
    body { font-family: 'Segoe UI', Arial, sans-serif; background-color: var(--preto1); color: var(--cinza2); margin: 0; padding: 20px; display: flex; justify-content: center; min-height: 100vh;}

    /* ── Malha CSS Grid ── */
    .main-container { 
      display: grid; 
      grid-template-columns: 1.5fr 1fr; /* Coluna esquerda mais larga */
      grid-template-rows: 1fr 1fr 1fr;  /* 3 linhas de altura rigorosamente iguais */
      gap: 20px; 
      width: 100%; height: 100%;
      max-width: 1800px; 
      align-items: stretch;
    }

    /* ── Posicionamento na Grade ── */
    .graph-wrapper {
      grid-column: 1;
      grid-row: 1 / 3;
      position: relative; background: var(--preto1); border: 1px solid var(--cinza1); border-radius: 12px; overflow: hidden; display: flex; flex-direction: column; min-height: 250px;
    }
    .c-master { grid-column: 1; grid-row: 3; }
    .c-grave  { grid-column: 2; grid-row: 1; }
    .c-medio  { grid-column: 2; grid-row: 2; }
    .c-agudo  { grid-column: 2; grid-row: 3; }

    /* ── Gráfico ── */
    canvas { display: block; width: 100%; flex-grow: 1; }
    #btnSave { position: absolute; top: 14px; left: 14px; z-index: 10; padding: 10px 16px; font-size: 14px; font-weight: bold; background-color: var(--preto2); color: var(--branco); border: 1px solid var(--cinza1); border-radius: 6px; cursor: pointer; transition: 0.2s; }
    #btnSave:hover { filter: brightness(1.4); }
    .btn-saved { background-color: #4CAF50 !important; border-color: #4CAF50 !important; color: var(--preto1) !important; }

    /* ── Cards e Controles ── */
    .card { background-color: var(--preto2); padding: 18px 20px; border-radius: 10px; border-left: 5px solid; box-shadow: 0 4px 10px rgba(0,0,0,0.4); display: flex; flex-direction: column; justify-content: center;}
    .c-master { border-color: #ddd; }
    .c-grave  { border-color: #ff4500; }
    .c-medio  { border-color: #00ff00; }
    .c-agudo  { border-color: #1e90ff; }
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
      /* Todos os itens assumem a posição natural de empilhamento */
      .graph-wrapper, .c-master, .c-grave, .c-medio, .c-agudo {
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
      <canvas id="plotCanvas"></canvas>
    </div>
    <div class="card c-master">
      <h3>GERAL</h3>
      <div class="control-row"><label>Limiar</label><button class="step-btn" onclick="stepValue('limiarGlobal', -50)">-</button><input type="number" min="10" max="2000" id="limiarGlobal" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('limiarGlobal', 50)">+</button></div>
      <div class="control-row"><label>Hold</label><button class="step-btn" onclick="stepValue('holdTime', -50)">-</button><input type="number" min="100" max="1000" id="holdTime" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('holdTime', 50)">+</button></div>
    </div>
    <div class="card c-grave">
      <h3>GRAVE</h3>
      <div class="control-row"><label>Offset</label><button class="step-btn" onclick="stepValue('offsetGrave', -20)">-</button><input type="number" min="-500" max="100" id="offsetGrave" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('offsetGrave', 20)">+</button></div>
      <div class="control-row"><label>Ganho</label><button class="step-btn" onclick="stepValue('ganhoGrave', -1)">-</button><input type="number" min="1" max="50" id="ganhoGrave" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('ganhoGrave', 1)">+</button></div>
    </div>
    <div class="card c-medio">
      <h3>MÉDIO</h3>
      <div class="control-row"><label>Offset</label><button class="step-btn" onclick="stepValue('offsetMedio', -20)">-</button><input type="number" min="-500" max="100" id="offsetMedio" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('offsetMedio', 20)">+</button></div>
      <div class="control-row"><label>Ganho</label><button class="step-btn" onclick="stepValue('ganhoMedio', -1)">-</button><input type="number" min="1" max="50" id="ganhoMedio" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('ganhoMedio', 1)">+</button></div>
    </div>
    <div class="card c-agudo">
      <h3>AGUDO</h3>
      <div class="control-row"><label>Offset</label><button class="step-btn" onclick="stepValue('offsetAgudo', -20)">-</button><input type="number" min="-500" max="100" id="offsetAgudo" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('offsetAgudo', 20)">+</button></div>
      <div class="control-row"><label>Ganho</label><button class="step-btn" onclick="stepValue('ganhoAgudo', -1)">-</button><input type="number" min="1" max="50" id="ganhoAgudo" onchange="sendData(this)"><button class="step-btn" onclick="stepValue('ganhoAgudo', 1)">+</button></div>
    </div>
  </div>

  <script>
    let source; let dataG = [], dataM = [], dataA = [];
    const maxPts = 80; const canvas = document.getElementById('plotCanvas');
    const ctx = canvas.getContext('2d');
    for (let i = 0; i < maxPts; i++) { dataG.push(0); dataM.push(0); dataA.push(0); }
    
    function resize() { setTimeout(() => { canvas.width = canvas.parentElement.clientWidth; canvas.height = canvas.parentElement.clientHeight; draw(); }, 50); }
    window.addEventListener('resize', resize);
    window.onload = () => {
      resize();
      source = new EventSource('/events');
      source.addEventListener('plot', e => {
        let v = JSON.parse(e.data);
        dataG.push(v.g); dataM.push(v.m); dataA.push(v.a);
        if (dataG.length > maxPts) { dataG.shift(); dataM.shift(); dataA.shift(); }
        draw();
      });
      fetch('/status').then(res => res.json()).then(data => { for (let key in data) { let el = document.getElementById(key); if (el) el.value = data[key]; } });
    };
    
    function sendData(el) { let val = parseInt(el.value) || 0; val = Math.max(parseInt(el.min), Math.min(parseInt(el.max), val)); el.value = val; fetch(`/update?id=${el.id}&value=${val}`); }
    function stepValue(id, delta) { let el = document.getElementById(id); let val = (parseInt(el.value) || 0) + delta; val = Math.max(parseInt(el.min), Math.min(parseInt(el.max), val)); el.value = val; fetch(`/update?id=${el.id}&value=${val}`); }
    
    function saveData() { 
      let btn = document.getElementById('btnSave');
      fetch('/save').then(() => { btn.innerText = "Salvo!"; btn.classList.add('btn-saved'); setTimeout(() => { btn.innerText = "Salvar"; btn.classList.remove('btn-saved'); }, 2000); });
    }
    
    function draw() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      let limEl = document.getElementById('limiarGlobal');
      let lim = limEl ? parseInt(limEl.value) : 400;
      let scale = canvas.height / (lim * 1.8);
      let yLim = canvas.height - (lim * scale);
      ctx.setLineDash([5, 5]); ctx.strokeStyle = '#555'; ctx.beginPath(); ctx.moveTo(0, yLim); ctx.lineTo(canvas.width, yLim); ctx.stroke(); ctx.setLineDash([]);
      renderLine(dataG, '#ff4500', scale); renderLine(dataM, '#00ff00', scale); renderLine(dataA, '#1e90ff', scale);
    }
    
    function renderLine(arr, col, sc) {
      if (arr.length < 2) return;
      ctx.strokeStyle = col; ctx.lineWidth = 2; ctx.beginPath();
      let step = canvas.width / (maxPts - 1);
      arr.forEach((v, i) => { let x = i * step; let y = canvas.height - (v * sc); if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y); });
      ctx.stroke();
    }
  </script>
</body>
</html>
)rawliteral";
