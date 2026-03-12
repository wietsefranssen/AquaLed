#pragma once

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="nl">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AquaLed 5-Channel Scheduler</title>
  <style>
    :root {
      --bg: #f3f5ef;
      --card: #ffffff;
      --text: #102018;
      --accent: #1d6a5c;
      --line: #9ab8a5;
      --muted: #587066;
      --warn: #a33d2b;
      --ok: #1f7a36;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Avenir Next", "Segoe UI", sans-serif;
      color: var(--text);
      background:
        radial-gradient(circle at 0% 0%, #dce9dd 0%, transparent 45%),
        radial-gradient(circle at 100% 100%, #d5e6eb 0%, transparent 38%),
        var(--bg);
      min-height: 100vh;
    }
    .wrap {
      width: min(1100px, 94vw);
      margin: 22px auto 30px;
      display: grid;
      gap: 14px;
    }
    .panel {
      background: var(--card);
      border: 1px solid #d5e0d7;
      border-radius: 14px;
      padding: 14px;
      box-shadow: 0 8px 24px rgba(16, 32, 24, 0.08);
    }
    h1 {
      margin: 0;
      font-weight: 700;
      letter-spacing: 0.02em;
      font-size: clamp(1.3rem, 3vw, 2rem);
    }
    .subtitle {
      color: var(--muted);
      margin-top: 6px;
      font-size: 0.95rem;
    }
    .toolbar {
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      align-items: center;
    }
    input, select, button {
      border-radius: 10px;
      border: 1px solid #b7c9bc;
      padding: 9px 11px;
      font-size: 0.95rem;
      background: #fff;
      color: var(--text);
    }
    button {
      cursor: pointer;
      background: linear-gradient(180deg, #f8fcf9, #ecf4ef);
      transition: transform 120ms ease, box-shadow 120ms ease;
    }
    button:hover { transform: translateY(-1px); box-shadow: 0 4px 10px rgba(15, 45, 33, 0.12); }
    button.primary {
      background: linear-gradient(180deg, #2b8d79, #1d6a5c);
      color: #fff;
      border-color: #1d6a5c;
    }
    .status {
      font-size: 0.92rem;
      color: var(--muted);
    }
    .status.ok { color: var(--ok); }
    .status.err { color: var(--warn); }
    .channels {
      display: grid;
      gap: 10px;
    }
    .ch {
      border: 1px solid #d6e2d8;
      border-radius: 12px;
      padding: 10px;
      background: linear-gradient(180deg, #fefefe, #f8fbf8);
    }
    .ch h3 {
      margin: 2px 0 6px;
      font-size: 0.95rem;
      font-weight: 650;
      color: #274035;
    }
    canvas {
      width: 100%;
      height: 145px;
      display: block;
      border: 1px solid #d4e0d8;
      border-radius: 10px;
      background: linear-gradient(180deg, #f6fbf8, #ffffff);
      touch-action: none;
    }
    .legend {
      color: var(--muted);
      font-size: 0.82rem;
      margin-top: 4px;
    }
    .grid2 {
      display: grid;
      gap: 12px;
      grid-template-columns: 1fr;
    }
    @media (min-width: 900px) {
      .grid2 { grid-template-columns: 1.25fr 1fr; }
    }
    .mono { font-family: Menlo, Consolas, monospace; }
  </style>
</head>
<body>
  <div class="wrap">
    <section class="panel">
      <h1>AquaLed Dagcurve Controller</h1>
      <div class="subtitle">Sleep punten op de tijdlijn per kanaal. Rechtsklik op een punt om te verwijderen, tik/klik op de lijn om een punt toe te voegen.</div>
    </section>

    <section class="panel toolbar">
      <label for="presetSelect">Preset</label>
      <select id="presetSelect"></select>
      <button id="btnLoad">Laad preset</button>
      <input id="presetName" placeholder="Naam nieuwe preset">
      <button id="btnSaveNew" class="primary">Opslaan als nieuw</button>
      <button id="btnOverwrite">Overschrijf geselecteerde</button>
      <button id="btnApply" class="primary">Activeren op ESP32</button>
      <span id="status" class="status">Klaar</span>
    </section>

    <div class="grid2">
      <section class="panel">
        <div class="channels" id="channels"></div>
      </section>
      <section class="panel">
        <h3>Live info</h3>
        <p class="mono" id="liveInfo">laden...</p>
        <p class="legend">Tijdas: 00:00 links tot 23:59 rechts. Hoogte is intensiteit 0..255.</p>
      </section>
    </div>
  </div>

<script>
(() => {
  const CHANNELS = 5;
  const MAX_POINTS = 16;
  const state = {
    presets: [],
    activePreset: 0,
    nowMinute: 0,
    outputs: [0,0,0,0,0],
    working: null,
    dragging: null,
    canvases: []
  };

  const palette = ["#1f7a8c", "#2d936c", "#8f6c4e", "#ba5a31", "#7b4fa3"];

  const el = {
    channels: document.getElementById("channels"),
    presetSelect: document.getElementById("presetSelect"),
    presetName: document.getElementById("presetName"),
    btnLoad: document.getElementById("btnLoad"),
    btnSaveNew: document.getElementById("btnSaveNew"),
    btnOverwrite: document.getElementById("btnOverwrite"),
    btnApply: document.getElementById("btnApply"),
    status: document.getElementById("status"),
    liveInfo: document.getElementById("liveInfo")
  };

  function clone(v) { return JSON.parse(JSON.stringify(v)); }

  function setStatus(msg, kind = "") {
    el.status.textContent = msg;
    el.status.className = "status" + (kind ? " " + kind : "");
  }

  function minuteToX(minute, w) { return Math.max(0, Math.min(w, (minute / 1439) * w)); }
  function valueToY(value, h) { return Math.max(0, Math.min(h, h - (value / 255) * h)); }
  function xToMinute(x, w) { return Math.round((Math.max(0, Math.min(w, x)) / w) * 1439); }
  function yToValue(y, h) { return Math.round(((h - Math.max(0, Math.min(h, y))) / h) * 255); }

  function sortAndClamp(points) {
    points.forEach(p => {
      p.minute = Math.max(0, Math.min(1439, p.minute | 0));
      p.value = Math.max(0, Math.min(255, p.value | 0));
    });
    points.sort((a,b) => a.minute - b.minute);
    const dedup = [];
    for (const p of points) {
      if (!dedup.length || dedup[dedup.length - 1].minute !== p.minute) dedup.push(p);
      else dedup[dedup.length - 1].value = p.value;
    }
    if (!dedup.length) dedup.push({minute:0,value:0});
    if (dedup.length === 1) dedup.push({minute:1439,value:dedup[0].value});
    while (dedup.length > MAX_POINTS) dedup.pop();
    return dedup;
  }

  function drawChannel(idx) {
    const canvas = state.canvases[idx];
    const ctx = canvas.getContext("2d");
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    const w = Math.floor(rect.width);
    const h = Math.floor(rect.height);
    canvas.width = w * dpr;
    canvas.height = h * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    const points = state.working.channels[idx];

    ctx.clearRect(0,0,w,h);

    ctx.strokeStyle = "#d6e5da";
    ctx.lineWidth = 1;
    for (let i = 0; i <= 6; i++) {
      const x = (i / 6) * w;
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, h);
      ctx.stroke();
    }
    for (let i = 0; i <= 4; i++) {
      const y = (i / 4) * h;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
      ctx.stroke();
    }

    ctx.strokeStyle = palette[idx % palette.length];
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    points.forEach((p, i) => {
      const x = minuteToX(p.minute, w);
      const y = valueToY(p.value, h);
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();

    points.forEach((p) => {
      const x = minuteToX(p.minute, w);
      const y = valueToY(p.value, h);
      ctx.fillStyle = "#fff";
      ctx.strokeStyle = palette[idx % palette.length];
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(x, y, 5, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
    });

    const nowX = minuteToX(state.nowMinute, w);
    ctx.strokeStyle = "#25352c";
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(nowX, 0);
    ctx.lineTo(nowX, h);
    ctx.stroke();

    ctx.fillStyle = "#25352c";
    ctx.font = "12px Menlo, monospace";
    ctx.fillText("00:00", 6, h - 6);
    ctx.fillText("23:59", w - 50, h - 6);
  }

  function renderAll() {
    for (let i = 0; i < CHANNELS; i++) drawChannel(i);
    const hh = Math.floor(state.nowMinute / 60);
    const mm = Math.floor(state.nowMinute % 60);
    el.liveInfo.textContent =
      "Actief preset: " + (state.presets[state.activePreset]?.name || "-") + "\n" +
      "Tijd: " + String(hh).padStart(2, "0") + ":" + String(mm).padStart(2, "0") + "\n" +
      "Outputs: " + state.outputs.map(v => String(v).padStart(3, " ")).join("  ");
  }

  function nearestPoint(points, x, y, w, h) {
    let best = -1;
    let bestDist = 15;
    points.forEach((p, i) => {
      const px = minuteToX(p.minute, w);
      const py = valueToY(p.value, h);
      const d = Math.hypot(px - x, py - y);
      if (d < bestDist) { bestDist = d; best = i; }
    });
    return best;
  }

  function bindCanvas(canvas, channelIndex) {
    const onPointerDown = (ev) => {
      ev.preventDefault();
      const rect = canvas.getBoundingClientRect();
      const x = ev.clientX - rect.left;
      const y = ev.clientY - rect.top;
      const points = state.working.channels[channelIndex];
      const idx = nearestPoint(points, x, y, rect.width, rect.height);

      if (ev.button === 2) {
        if (idx >= 0 && points.length > 2) {
          points.splice(idx, 1);
          state.working.channels[channelIndex] = sortAndClamp(points);
          drawChannel(channelIndex);
        }
        return;
      }

      if (idx >= 0) {
        state.dragging = { channelIndex, pointIndex: idx };
      } else {
        points.push({ minute: xToMinute(x, rect.width), value: yToValue(y, rect.height) });
        state.working.channels[channelIndex] = sortAndClamp(points);
        drawChannel(channelIndex);
      }
    };

    const onPointerMove = (ev) => {
      if (!state.dragging || state.dragging.channelIndex !== channelIndex) return;
      const rect = canvas.getBoundingClientRect();
      const x = ev.clientX - rect.left;
      const y = ev.clientY - rect.top;
      const points = state.working.channels[channelIndex];
      const p = points[state.dragging.pointIndex];
      if (!p) return;
      p.minute = xToMinute(x, rect.width);
      p.value = yToValue(y, rect.height);
      state.working.channels[channelIndex] = sortAndClamp(points);
      const newIdx = state.working.channels[channelIndex].findIndex(
        q => q.minute === p.minute && q.value === p.value
      );
      state.dragging.pointIndex = Math.max(0, newIdx);
      drawChannel(channelIndex);
    };

    const onPointerUp = () => { state.dragging = null; };

    canvas.addEventListener("pointerdown", onPointerDown);
    canvas.addEventListener("pointermove", onPointerMove);
    canvas.addEventListener("pointerup", onPointerUp);
    canvas.addEventListener("pointerleave", onPointerUp);
    canvas.addEventListener("contextmenu", (ev) => ev.preventDefault());
  }

  function buildChannelUi() {
    el.channels.innerHTML = "";
    state.canvases = [];
    for (let i = 0; i < CHANNELS; i++) {
      const box = document.createElement("div");
      box.className = "ch";
      box.innerHTML = "<h3>Kanaal " + (i + 1) + "</h3><canvas></canvas><div class='legend'>Klik om punt toe te voegen, sleep om te verplaatsen, rechtsklik om te verwijderen.</div>";
      const c = box.querySelector("canvas");
      state.canvases.push(c);
      bindCanvas(c, i);
      el.channels.appendChild(box);
    }
  }

  function refillPresetSelect() {
    el.presetSelect.innerHTML = "";
    state.presets.forEach((p, i) => {
      const opt = document.createElement("option");
      opt.value = String(i);
      opt.textContent = i + " - " + p.name;
      if (i === state.activePreset) opt.selected = true;
      el.presetSelect.appendChild(opt);
    });
  }

  async function api(path, method = "GET", data = null) {
    const init = { method, headers: {} };
    if (data) {
      init.headers["Content-Type"] = "application/json";
      init.body = JSON.stringify(data);
    }
    const res = await fetch(path, init);
    if (!res.ok) throw new Error("HTTP " + res.status);
    const txt = await res.text();
    return txt ? JSON.parse(txt) : {};
  }

  async function loadState() {
    const s = await api("/api/state");
    state.presets = s.presets || [];
    state.activePreset = s.activePreset || 0;
    state.nowMinute = s.nowMinute || 0;
    state.outputs = s.outputs || [0,0,0,0,0];
    state.working = clone(state.presets[state.activePreset] || { name: "Nieuw", channels: Array.from({length: CHANNELS}, () => [{minute:0,value:0},{minute:1439,value:0}]) });
    refillPresetSelect();
    renderAll();
  }

  async function savePreset(asNew) {
    if (!state.working) return;
    const idx = asNew ? -1 : Number(el.presetSelect.value || -1);
    if (asNew) {
      const nm = el.presetName.value.trim();
      state.working.name = nm || ("Preset " + (state.presets.length + 1));
    }
    for (let i = 0; i < CHANNELS; i++) {
      state.working.channels[i] = sortAndClamp(state.working.channels[i]);
    }
    await api("/api/preset/upsert", "POST", {
      index: idx,
      name: state.working.name,
      channels: state.working.channels
    });
    await loadState();
    setStatus("Preset opgeslagen", "ok");
  }

  async function applySelected() {
    const idx = Number(el.presetSelect.value || 0);
    await api("/api/preset/select", "POST", { index: idx });
    await loadState();
    setStatus("Preset geactiveerd", "ok");
  }

  function bindActions() {
    el.btnLoad.onclick = () => {
      const idx = Number(el.presetSelect.value || 0);
      state.working = clone(state.presets[idx]);
      renderAll();
      setStatus("Preset geladen in editor");
    };

    el.btnSaveNew.onclick = async () => {
      try { await savePreset(true); }
      catch (e) { setStatus("Opslaan mislukt: " + e.message, "err"); }
    };

    el.btnOverwrite.onclick = async () => {
      try {
        state.working.name = state.presets[Number(el.presetSelect.value || 0)]?.name || state.working.name;
        await savePreset(false);
      } catch (e) { setStatus("Overschrijven mislukt: " + e.message, "err"); }
    };

    el.btnApply.onclick = async () => {
      try { await applySelected(); }
      catch (e) { setStatus("Activeren mislukt: " + e.message, "err"); }
    };

    window.addEventListener("resize", () => renderAll());
  }

  async function bootstrap() {
    buildChannelUi();
    bindActions();
    await loadState();
    setStatus("Verbonden", "ok");
    setInterval(async () => {
      try {
        const s = await api("/api/state");
        state.nowMinute = s.nowMinute || 0;
        state.outputs = s.outputs || [0,0,0,0,0];
        state.activePreset = s.activePreset || 0;
        refillPresetSelect();
        renderAll();
      } catch (_) {
      }
    }, 5000);
  }

  bootstrap().catch(err => setStatus("Initialisatie fout: " + err.message, "err"));
})();
</script>
</body>
</html>
)rawliteral";
