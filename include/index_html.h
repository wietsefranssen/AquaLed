#pragma once

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="nl">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AquaLed Scheduler</title>
  <style>
    body { font-family: "Avenir Next", "Segoe UI", sans-serif; margin: 0; padding: 16px; background: #f3f5ef; color: #102018; }
    .card { background: #fff; border: 1px solid #d6e2d8; border-radius: 12px; padding: 12px; margin-bottom: 12px; }
    .toolbar { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
    button, input, select, a { border: 1px solid #b7c9bc; border-radius: 8px; padding: 8px 10px; background: #fff; color: #102018; text-decoration: none; }
    button.primary { background: #1d6a5c; border-color: #1d6a5c; color: #fff; }
    .status { font-size: .9rem; color: #5b6f64; }
    .channels { display: grid; gap: 10px; }
    .ch { border: 1px solid #d6e2d8; border-radius: 10px; padding: 8px; background: #fff; }
    canvas { width: 100%; height: 160px; display: block; border: 1px solid #d4e0d8; border-radius: 8px; background: #f9fcfa; touch-action: none; }
    pre { white-space: pre-wrap; }
    .small { font-size: .85rem; color: #5b6f64; }
    @media (min-width: 980px) { .layout { display: grid; grid-template-columns: 1.4fr .8fr; gap: 12px; } }
  </style>
</head>
<body>
  <div class="card">
    <div class="toolbar">
      <a href="/settings">Naar instellingen</a>
    </div>
    <h2>AquaLed Dagcurve Controller</h2>
    <div>Curve is smooth. Klik op grafiek om direct een punt op die waarde/tijd te zetten. Sleep voor finetune, rechtsklik om punt te verwijderen.</div>
  </div>

  <div class="card toolbar">
    <label for="presetSelect">Preset</label>
    <select id="presetSelect"></select>
    <input id="presetName" placeholder="Naam nieuwe preset">
    <button id="btnSaveNew" class="primary">Opslaan als nieuw</button>
    <button id="btnOverwrite">Overschrijf</button>
    <span id="status" class="status">Klaar</span>
  </div>

  <div class="card toolbar">
    <strong>Snelle simulatie</strong>
    <label for="simSeconds">1 dag in</label>
    <select id="simSeconds">
      <option value="10">10 sec</option>
      <option value="30">30 sec</option>
      <option value="60">1 min</option>
      <option value="120">2 min</option>
      <option value="300">5 min</option>
      <option value="600">10 min</option>
    </select>
    <button id="btnSimStart" class="primary">Start simulatie</button>
    <button id="btnSimStop">Stop simulatie</button>
    <span id="simState" class="small">Uit</span>
  </div>

  <div class="layout">
    <div class="card"><div id="channels" class="channels"></div></div>
    <div class="card"><h3>Live info</h3><pre id="live">laden...</pre></div>
  </div>

<script>
(() => {
  const CHANNELS = 5;
  const MAX_POINTS = 16;
  const DAY_MIN = 1439;

  const state = {
    presets: [],
    activePreset: 0,
    nowMinute: 0,
    outputs: [0,0,0,0,0],
    dateTime: "-",
    simulationActive: false,
    simulationDaySeconds: 120,
    working: null,
    dragging: null,
    canvases: []
  };

  const colors = ["#1f7a8c", "#2d936c", "#8f6c4e", "#ba5a31", "#7b4fa3"];

  const el = {
    presetSelect: document.getElementById("presetSelect"),
    presetName: document.getElementById("presetName"),
    btnSaveNew: document.getElementById("btnSaveNew"),
    btnOverwrite: document.getElementById("btnOverwrite"),
    status: document.getElementById("status"),
    channels: document.getElementById("channels"),
    live: document.getElementById("live"),
    simSeconds: document.getElementById("simSeconds"),
    btnSimStart: document.getElementById("btnSimStart"),
    btnSimStop: document.getElementById("btnSimStop"),
    simState: document.getElementById("simState")
  };

  const clone = (v) => JSON.parse(JSON.stringify(v));
  const minuteToX = (m, w) => Math.max(0, Math.min(w, (m / DAY_MIN) * w));
  const valueToY = (v, h) => Math.max(0, Math.min(h, h - (v / 255) * h));
  const xToMinute = (x, w) => Math.round((Math.max(0, Math.min(w, x)) / w) * DAY_MIN);
  const yToValue = (y, h) => Math.round(((h - Math.max(0, Math.min(h, y))) / h) * 255);
  const smoothStep = (t) => (t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t));

  async function api(path, method = "GET", body = null) {
    const init = { method, headers: {} };
    if (body) {
      init.headers["Content-Type"] = "application/json";
      init.body = JSON.stringify(body);
    }
    const res = await fetch(path, init);
    if (!res.ok) throw new Error("HTTP " + res.status);
    const t = await res.text();
    return t ? JSON.parse(t) : {};
  }

  function setStatus(text, err = false) {
    el.status.textContent = text;
    el.status.style.color = err ? "#a54733" : "#2b6d3f";
  }

  function sortAndClamp(points) {
    points.forEach(p => {
      p.minute = Math.max(0, Math.min(DAY_MIN, p.minute | 0));
      p.value = Math.max(0, Math.min(255, p.value | 0));
    });
    points.sort((a,b) => a.minute - b.minute);
    const out = [];
    for (const p of points) {
      if (!out.length || out[out.length - 1].minute !== p.minute) out.push(p);
      else out[out.length - 1].value = p.value;
    }
    if (!out.length) out.push({minute:0,value:0});
    if (out.length === 1) out.push({minute:DAY_MIN, value: out[0].value});
    while (out.length > MAX_POINTS) out.pop();
    return out;
  }

  function evaluateSmooth(points, minute) {
    if (!points.length) return 0;
    if (points.length === 1) return points[0].value;

    let m = minute;
    while (m < 0) m += 1440;
    while (m >= 1440) m -= 1440;

    let a = null, b = null;
    let start = 0, end = 0;

    for (let i = 0; i < points.length - 1; i++) {
      if (m >= points[i].minute && m <= points[i + 1].minute) {
        a = points[i];
        b = points[i + 1];
        start = a.minute;
        end = b.minute;
        break;
      }
    }

    if (!a || !b) {
      a = points[points.length - 1];
      b = points[0];
      start = a.minute;
      end = b.minute + 1440;
      if (m < points[0].minute) m += 1440;
    }

    const span = end - start;
    const t = span > 0 ? (m - start) / span : 0;
    return Math.round(a.value + (b.value - a.value) * smoothStep(t));
  }

  function drawAxes(ctx, w, h) {
    ctx.strokeStyle = "#dbe6dd";
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

    ctx.fillStyle = "#5f7066";
    ctx.font = "11px Menlo, monospace";
    for (let hour = 0; hour <= 24; hour += 4) {
      const minute = Math.min(DAY_MIN, hour * 60);
      const x = minuteToX(minute, w);
      const label = String(hour).padStart(2, "0") + ":00";
      const tx = Math.max(2, Math.min(w - 36, x - 16));
      ctx.fillText(label, tx, h - 4);
    }
  }

  function draw(idx) {
    const c = state.canvases[idx];
    const ctx = c.getContext("2d");
    const dpr = window.devicePixelRatio || 1;
    const r = c.getBoundingClientRect();
    const w = Math.floor(r.width), h = Math.floor(r.height);
    c.width = w * dpr;
    c.height = h * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    const points = state.working.channels[idx];

    ctx.clearRect(0,0,w,h);
    drawAxes(ctx, w, h);

    ctx.strokeStyle = colors[idx % colors.length];
    ctx.lineWidth = 2;
    ctx.beginPath();
    const samples = 180;
    for (let i = 0; i <= samples; i++) {
      const minute = (i / samples) * DAY_MIN;
      const value = evaluateSmooth(points, minute);
      const x = minuteToX(minute, w);
      const y = valueToY(value, h);
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();

    points.forEach(p => {
      const x = minuteToX(p.minute, w);
      const y = valueToY(p.value, h);
      ctx.beginPath();
      ctx.fillStyle = "#fff";
      ctx.strokeStyle = colors[idx % colors.length];
      ctx.arc(x, y, 4.5, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
    });

    const nowX = minuteToX(state.nowMinute, w);
    ctx.strokeStyle = "#24362b";
    ctx.beginPath();
    ctx.moveTo(nowX, 0);
    ctx.lineTo(nowX, h);
    ctx.stroke();
  }

  function render() {
    for (let i = 0; i < CHANNELS; i++) draw(i);
    const hh = Math.floor(state.nowMinute / 60);
    const mm = Math.floor(state.nowMinute % 60);
    el.live.textContent =
      "Preset: " + (state.presets[state.activePreset]?.name || "-") + "\n" +
      "Datum/tijd: " + (state.dateTime || "-") + "\n" +
      "Tijd (minuut): " + String(hh).padStart(2, "0") + ":" + String(mm).padStart(2, "0") + "\n" +
      "Simulatie: " + (state.simulationActive ? ("aan (1 dag / " + state.simulationDaySeconds + "s)") : "uit") + "\n" +
      "Outputs: " + state.outputs.join(", ");

    el.simSeconds.value = state.simulationDaySeconds;
    el.simState.textContent = state.simulationActive ? "Actief" : "Uit";
  }

  function nearest(points, x, y, w, h) {
    let best = -1, dist = 14;
    points.forEach((p, i) => {
      const d = Math.hypot(minuteToX(p.minute,w) - x, valueToY(p.value,h) - y);
      if (d < dist) { dist = d; best = i; }
    });
    return best;
  }

  function bindCanvas(c, idx) {
    c.addEventListener("contextmenu", e => e.preventDefault());

    c.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      const r = c.getBoundingClientRect();
      const x = e.clientX - r.left;
      const y = e.clientY - r.top;
      const pts = state.working.channels[idx];
      const hit = nearest(pts, x, y, r.width, r.height);

      if (e.button === 2) {
        if (hit >= 0 && pts.length > 2) {
          pts.splice(hit, 1);
          state.working.channels[idx] = sortAndClamp(pts);
          draw(idx);
        }
        return;
      }

      const minute = xToMinute(x, r.width);
      const value = yToValue(y, r.height);

      if (hit >= 0) {
        pts[hit].minute = minute;
        pts[hit].value = value;
        state.working.channels[idx] = sortAndClamp(pts);
        const newHit = nearest(state.working.channels[idx], x, y, r.width, r.height);
        state.dragging = { idx, point: Math.max(0, newHit) };
      } else {
        pts.push({ minute, value });
        state.working.channels[idx] = sortAndClamp(pts);
        const inserted = nearest(state.working.channels[idx], x, y, r.width, r.height);
        state.dragging = { idx, point: Math.max(0, inserted) };
      }
      draw(idx);
    });

    c.addEventListener("pointermove", (e) => {
      if (!state.dragging || state.dragging.idx !== idx) return;
      const r = c.getBoundingClientRect();
      const x = e.clientX - r.left;
      const y = e.clientY - r.top;
      const pts = state.working.channels[idx];
      const p = pts[state.dragging.point];
      if (!p) return;
      p.minute = xToMinute(x, r.width);
      p.value = yToValue(y, r.height);
      state.working.channels[idx] = sortAndClamp(pts);
      state.dragging.point = nearest(state.working.channels[idx], x, y, r.width, r.height);
      draw(idx);
    });

    c.addEventListener("pointerup", () => { state.dragging = null; });
    c.addEventListener("pointerleave", () => { state.dragging = null; });
  }

  function buildChannels() {
    el.channels.innerHTML = "";
    state.canvases = [];
    for (let i = 0; i < CHANNELS; i++) {
      const box = document.createElement("div");
      box.className = "ch";
      box.innerHTML = `<div>Kanaal ${i + 1}</div><canvas></canvas>`;
      const c = box.querySelector("canvas");
      state.canvases.push(c);
      bindCanvas(c, i);
      el.channels.appendChild(box);
    }
  }

  function mergeState(s) {
    state.presets = s.presets || [];
    state.activePreset = s.activePreset || 0;
    state.nowMinute = s.nowMinute || 0;
    state.outputs = s.outputs || [0,0,0,0,0];
    state.dateTime = s.dateTime || "-";
    state.simulationActive = !!s.simulationActive;
    state.simulationDaySeconds = Number(s.simulationDaySeconds || 120);
  }

  async function loadState() {
    const s = await api("/api/state");
    mergeState(s);

    state.working = clone(state.presets[state.activePreset] || {
      name: "Nieuw",
      channels: Array.from({length: CHANNELS}, () => [{minute:0,value:0},{minute:DAY_MIN,value:0}])
    });

    el.presetSelect.innerHTML = "";
    state.presets.forEach((p,i) => {
      const o = document.createElement("option");
      o.value = String(i);
      o.textContent = `${i} - ${p.name}`;
      if (i === state.activePreset) o.selected = true;
      el.presetSelect.appendChild(o);
    });

    render();
  }

  async function savePreset(asNew) {
    const idx = asNew ? -1 : Number(el.presetSelect.value || 0);
    if (asNew) state.working.name = el.presetName.value.trim() || `Preset ${state.presets.length + 1}`;
    for (let i = 0; i < CHANNELS; i++) state.working.channels[i] = sortAndClamp(state.working.channels[i]);
    await api("/api/preset/upsert", "POST", {
      index: idx,
      name: state.working.name,
      channels: state.working.channels
    });
    await loadState();
  }

  async function setSimulation(enabled) {
    const daySeconds = Number(el.simSeconds.value || 120);
    await api("/api/simulation/set", "POST", { enabled, daySeconds });
    const s = await api("/api/state");
    mergeState(s);
    render();
  }

  async function boot() {
    buildChannels();
    await loadState();
    setStatus("Verbonden", false);

    el.presetSelect.onchange = async () => {
      const idx = Number(el.presetSelect.value || 0);
      try {
        await api("/api/preset/select", "POST", { index: idx });
        await loadState();
        setStatus("Preset geladen", false);
      } catch (e) {
        setStatus("Laden mislukt: " + e.message, true);
      }
    };

    el.btnSaveNew.onclick = async () => {
      try { await savePreset(true); setStatus("Nieuw preset opgeslagen", false); }
      catch (e) { setStatus("Opslaan mislukt: " + e.message, true); }
    };

    el.btnOverwrite.onclick = async () => {
      try {
        const idx = Number(el.presetSelect.value || 0);
        state.working.name = state.presets[idx]?.name || state.working.name;
        await savePreset(false);
        setStatus("Preset overschreven", false);
      } catch (e) {
        setStatus("Opslaan mislukt: " + e.message, true);
      }
    };

    el.btnSimStart.onclick = async () => {
      try {
        await setSimulation(true);
        setStatus("Simulatie gestart", false);
      } catch (e) {
        setStatus("Simulatie starten mislukt: " + e.message, true);
      }
    };

    el.btnSimStop.onclick = async () => {
      try {
        await setSimulation(false);
        setStatus("Simulatie gestopt", false);
      } catch (e) {
        setStatus("Simulatie stoppen mislukt: " + e.message, true);
      }
    };

    el.simSeconds.onchange = async () => {
      try {
        await setSimulation(state.simulationActive);
        setStatus("Simulatieduur opgeslagen", false);
      } catch (e) {
        setStatus("Simulatieduur opslaan mislukt: " + e.message, true);
      }
    };

    window.addEventListener("resize", render);

    setInterval(async () => {
      try {
        const s = await api("/api/state");
        mergeState(s);
        render();
      } catch (_) {
      }
    }, 1000);
  }

  boot().catch(err => setStatus("Initialisatie fout: " + err.message, true));
})();
</script>
</body>
</html>
)rawliteral";
