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
    .live-grid { display: flex; flex-direction: column; gap: 10px; }
    .live-row { display: flex; align-items: center; gap: 8px; font-size: .92rem; }
    .live-label { color: #5b6f64; min-width: 70px; }
    .live-value { font-weight: 600; }
    .live-divider { border: none; border-top: 1px solid #e4ebe6; margin: 2px 0; }
    .ch-bar-row { display: flex; align-items: center; gap: 8px; margin-bottom: 4px; }
    .ch-swatch { width: 14px; height: 14px; border-radius: 50%; flex-shrink: 0; border: 1px solid rgba(0,0,0,.12); }
    .ch-name { min-width: 28px; font-size: .85rem; color: #5b6f64; }
    .ch-bar-track { flex: 1; height: 18px; background: #eef2ee; border-radius: 9px; overflow: hidden; position: relative; }
    .ch-bar-fill { height: 100%; border-radius: 9px; transition: width .3s ease, background .3s ease; min-width: 0; }
    .ch-bar-pct { min-width: 38px; text-align: right; font-size: .85rem; font-weight: 600; font-variant-numeric: tabular-nums; }
    .live-badge { display: inline-block; padding: 2px 8px; border-radius: 6px; font-size: .8rem; font-weight: 600; }
    .badge-on { background: #d4edda; color: #1d6a5c; }
    .badge-off { background: #f8d7da; color: #922b21; }
    .badge-sim { background: #fff3cd; color: #856404; }
    .badge-preview { background: #d1ecf1; color: #0c5460; }
    .topbar { display: flex; align-items: center; gap: 10px; margin-bottom: 12px; }
    .topbar h2 { margin: 0; flex: 1; font-size: 1.25rem; }
    .hint { font-size: .82rem; color: #7a8f82; margin-bottom: 6px; }
    .version-tag { position: fixed; bottom: 6px; right: 10px; font-size: .72rem; color: #7a8f82; opacity: .5; pointer-events: none; z-index: 999; }
  </style>
</head>
<body>
  <div class="topbar">
    <h2>AquaLed Dagcurve</h2>
    <a href="/settings">⚙ Instellingen</a>
    <button id="btnMasterToggle" class="primary" style="min-width:96px;">● AAN</button>
  </div>

  <div class="card toolbar">
    <label for="presetSelect">Preset</label>
    <select id="presetSelect"></select>
    <input id="presetName" placeholder="Naam nieuwe preset">
    <button id="btnSaveNew" class="primary">Opslaan als nieuw</button>
    <button id="btnOverwrite">Overschrijf</button>
    <button id="btnDelete" style="color:#922b21;border-color:#e6a19a;">Verwijder</button>
    <button id="btnExport" title="Download alle presets als JSON-bestand">⬇ Export</button>
    <button id="btnImport" title="Importeer presets uit JSON-bestand">⬆ Import</button>
    <input id="fileImport" type="file" accept=".json" style="display:none">
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
    <span id="simState" class="small">Uit</span>
  </div>

  <div class="card toolbar">
    <strong>Tijdlijn preview</strong>
    <input type="range" id="previewSlider" min="0" max="1439" value="0" style="flex:1;min-width:120px;">
    <span id="previewTime" class="small" style="min-width:44px;">--:--</span>
  </div>

  <div class="card toolbar" id="resumeBar" style="display:none;">
    <button id="btnResume" class="primary" style="flex:1;">Hervat dagcurve</button>
  </div>

  <div class="layout">
    <div class="card">
      <div class="hint">Klik op grafiek om punt te zetten. Sleep voor finetune, rechtsklik om te verwijderen.</div>
      <div id="channels" class="channels"></div>
    </div>
    <div class="card"><h3>Live info</h3><div id="live" class="live-grid">laden...</div></div>
  </div>
  <div id="versionTag" class="version-tag"></div>

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
    masterEnabled: true,
    previewMinute: null,
    moonPhase: 0.5,
    moonlightEnabled: false,
    moonlightChannel: -1,
    moonlightIntensity: 492,
    moonlightActive: false,
    working: null,
    dragging: null,
    canvases: [],
    colors: ["#1f7a8c", "#2d936c", "#8f6c4e", "#ba5a31", "#7b4fa3"]
  };

  const el = {
    presetSelect: document.getElementById("presetSelect"),
    presetName: document.getElementById("presetName"),
    btnSaveNew: document.getElementById("btnSaveNew"),
    btnOverwrite: document.getElementById("btnOverwrite"),
    btnDelete: document.getElementById("btnDelete"),
    status: document.getElementById("status"),
    channels: document.getElementById("channels"),
    live: document.getElementById("live"),
    simSeconds: document.getElementById("simSeconds"),
    btnSimStart: document.getElementById("btnSimStart"),
    simState: document.getElementById("simState"),
    previewSlider: document.getElementById("previewSlider"),
    previewTime: document.getElementById("previewTime"),
    btnPreviewReset: document.getElementById("btnResume"),
    resumeBar: document.getElementById("resumeBar"),
    btnMasterToggle: document.getElementById("btnMasterToggle"),
    btnExport: document.getElementById("btnExport"),
    btnImport: document.getElementById("btnImport"),
    fileImport: document.getElementById("fileImport")
  };

  const clone = (v) => JSON.parse(JSON.stringify(v));
  const minuteToX = (m, w) => Math.max(0, Math.min(w, (m / DAY_MIN) * w));
  const valueToY = (v, h) => Math.max(0, Math.min(h, h - (v / 4095) * h));
  const xToMinute = (x, w) => Math.round((Math.max(0, Math.min(w, x)) / w) * DAY_MIN);
  const yToValue = (y, h) => Math.round(((h - Math.max(0, Math.min(h, y))) / h) * 4095);
  const smoothStep = (t) => (t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t));
  const toPct = (v) => Math.round(v / 4095 * 100);
  const fmtMin = (m) => String(Math.floor(m / 60)).padStart(2, "0") + ":" + String(Math.floor(m % 60)).padStart(2, "0");

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
      p.value = Math.max(0, Math.min(4095, p.value | 0));
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

    ctx.strokeStyle = state.colors[idx % state.colors.length];
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
      ctx.strokeStyle = state.colors[idx % state.colors.length];
      ctx.arc(x, y, 4.5, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
    });

    const nowX = minuteToX(state.previewMinute !== null ? state.previewMinute : state.nowMinute, w);
    ctx.strokeStyle = "#24362b";
    ctx.beginPath();
    ctx.moveTo(nowX, 0);
    ctx.lineTo(nowX, h);
    ctx.stroke();
  }

  function render() {
    for (let i = 0; i < CHANNELS; i++) draw(i);
    const displayMin = state.previewMinute !== null ? state.previewMinute : state.nowMinute;
    const masterOn = state.masterEnabled;
    const simOn = state.simulationActive;
    const prevOn = state.previewMinute !== null;
    let badges = '';
    if (masterOn) badges += '<span class="live-badge badge-on">AAN</span> ';
    else badges += '<span class="live-badge badge-off">UIT</span> ';
    if (simOn) badges += '<span class="live-badge badge-sim">SIM ' + state.simulationDaySeconds + 's</span> ';
    if (prevOn) badges += '<span class="live-badge badge-preview">PREVIEW</span> ';

    let bars = '';
    for (let i = 0; i < CHANNELS; i++) {
      const pct = toPct(state.outputs[i]);
      const col = state.colors[i % state.colors.length];
      bars += '<div class="ch-bar-row">'
        + '<span class="ch-swatch" style="background:' + col + '"></span>'
        + '<span class="ch-name">' + (i + 1) + '</span>'
        + '<div class="ch-bar-track"><div class="ch-bar-fill" style="width:' + pct + '%;background:' + col + ';"></div></div>'
        + '<span class="ch-bar-pct">' + pct + '%</span>'
        + '</div>';
    }

    el.live.innerHTML =
      '<div class="live-row">' + badges + '</div>'
      + '<hr class="live-divider">'
      + '<div class="live-row"><span class="live-label">Preset</span><span class="live-value">' + (state.presets[state.activePreset]?.name || "-") + '</span></div>'
      + '<div class="live-row"><span class="live-label">Datum</span><span class="live-value">' + (state.dateTime || "-") + '</span></div>'
      + (state.moonlightEnabled && state.moonlightChannel >= 0 ? '<div class="live-row"><span class="live-label">Maanlicht</span><span class="live-value">'
          + (()=>{
              const p = Math.round(state.moonPhase * 100);
              const e = p >= 95 ? "🌕" : p >= 70 ? "🌔" : p >= 40 ? "🌓" : p >= 10 ? "🌒" : "🌑";
              const phaseName = p >= 95 ? "volle maan" : p >= 70 ? "bijna vol" : p >= 40 ? "halve maan" : p >= 10 ? "wassende maan" : "nieuwe maan";
              const brightnessPct = Math.round(state.moonlightIntensity * state.moonPhase / 4095 * 100);
              const actief = state.moonlightActive;
              const toelichting = actief ? " <span style=\"opacity:.6;font-size:.85em\">(minimum actief)</span>" : "";
              return e + " " + phaseName + " (" + p + "% vol) — minimumhelderheid kanaal " + (state.moonlightChannel + 1) + ": <strong>" + brightnessPct + "%</strong>" + toelichting;
            })()
          + '</span></div>' : '')
      + '<hr class="live-divider">'
      + '<div style="font-weight:600;font-size:.9rem;margin-bottom:2px;">Kanalen</div>'
      + bars;

    el.simSeconds.value = state.simulationDaySeconds;
    el.simState.textContent = state.simulationActive ? "Actief" : "Uit";

    el.btnMasterToggle.textContent = state.masterEnabled ? "● AAN" : "● UIT";
    el.btnMasterToggle.style.background  = state.masterEnabled ? "" : "#c0392b";
    el.btnMasterToggle.style.borderColor = state.masterEnabled ? "" : "#922b21";

    const showResume = state.simulationActive || state.previewMinute !== null;
    el.resumeBar.style.display = showResume ? "" : "none";

    if (state.previewMinute !== null) {
      el.previewTime.textContent = fmtMin(state.previewMinute);
      el.previewSlider.value = state.previewMinute;
    } else {
      el.previewSlider.value = Math.round(state.nowMinute);
      el.previewTime.textContent = fmtMin(state.nowMinute);
    }
  }

  function nearest(points, x, y, w, h) {
    let best = -1, dist = 14;
    points.forEach((p, i) => {
      const d = Math.hypot(minuteToX(p.minute,w) - x, valueToY(p.value,h) - y);
      if (d < dist) { dist = d; best = i; }
    });
    return best;
  }

  let _previewDebounce = null;
  function sendPreviewDebounced(minute, outputs) {
    if (_previewDebounce) clearTimeout(_previewDebounce);
    _previewDebounce = setTimeout(async () => {
      try {
        const body = { enabled: true, minute };
        if (outputs) body.outputs = outputs;
        await api("/api/preview/set", "POST", body);
      } catch(_){}
    }, 60);
  }

  function activatePointPreview(minute) {
    state.previewMinute = minute;
    const preset = state.working || state.presets[state.activePreset];
    if (preset && preset.channels) {
      state.outputs = preset.channels.map(pts => evaluateSmooth(pts, minute));
    }
    el.previewSlider.value = minute;
    el.previewTime.textContent = fmtMin(minute);
    render();
    sendPreviewDebounced(minute, state.outputs);
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
          const delMinute = pts[hit].minute;
          pts.splice(hit, 1);
          state.working.channels[idx] = sortAndClamp(pts);
          activatePointPreview(delMinute);
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
      activatePointPreview(minute);
    });

    c.addEventListener("pointermove", (e) => {
      if (!state.dragging || state.dragging.idx !== idx) return;
      const r = c.getBoundingClientRect();
      const x = e.clientX - r.left;
      const y = e.clientY - r.top;
      const pts = state.working.channels[idx];
      const pi = state.dragging.point;
      if (pi >= pts.length) return;
      pts[pi].minute = xToMinute(x, r.width);
      pts[pi].value = yToValue(y, r.height);
      state.working.channels[idx] = sortAndClamp(pts);
      const newIdx = nearest(state.working.channels[idx], x, y, r.width, r.height);
      state.dragging.point = Math.max(0, newIdx);
      const draggedPt = state.working.channels[idx][state.dragging.point];
      activatePointPreview(draggedPt ? draggedPt.minute : xToMinute(x, r.width));
    });

    c.addEventListener("pointerup", async () => {
      if (state.dragging && state.previewMinute !== null) {
        try {
          await api("/api/preview/set", "POST", {
            enabled: true, minute: state.previewMinute, outputs: state.outputs
          });
        } catch(_){}
      }
      state.dragging = null;
    });
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
    if (Array.isArray(s.channelColors) && s.channelColors.length === CHANNELS)
      state.colors = s.channelColors;
    state.masterEnabled = s.masterEnabled !== false;
    state.moonPhase         = typeof s.moonPhase === "number" ? s.moonPhase : 0.5;
    state.moonlightEnabled  = !!s.moonlightEnabled;
    state.moonlightChannel  = typeof s.moonlightChannel === "number" ? s.moonlightChannel : -1;
    state.moonlightIntensity = typeof s.moonlightIntensity === "number" ? s.moonlightIntensity : 492;
    state.moonlightActive    = !!s.moonlightActive;
    if (s.version) document.getElementById("versionTag").textContent = s.version;
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
      o.textContent = `${i+1} - ${p.name}`;
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

    el.btnDelete.onclick = async () => {
      const idx = Number(el.presetSelect.value || 0);
      const name = state.presets[idx]?.name || "Preset";
      if (state.presets.length <= 1) { setStatus("Laatste preset kan niet verwijderd worden", true); return; }
      if (!confirm(`Preset "${name}" verwijderen?`)) return;
      try {
        await api("/api/preset/delete", "POST", { index: idx });
        await loadState();
        setStatus("Preset verwijderd", false);
      } catch (e) {
        setStatus("Verwijderen mislukt: " + e.message, true);
      }
    };

    el.btnSimStart.onclick = async () => {
      try {
        state.previewMinute = null;
        stopSimLoop();
        await setSimulation(true);
        setStatus("Simulatie gestart", false);
      } catch (e) {
        setStatus("Simulatie mislukt: " + e.message, true);
      }
    };

    el.btnMasterToggle.onclick = async () => {
      try {
        const next = !state.masterEnabled;
        await api("/api/master/set", "POST", { enabled: next });
        const s = await api("/api/state");
        mergeState(s);
        render();
        setStatus("Master " + (next ? "ingeschakeld" : "uitgeschakeld"), false);
      } catch (e) {
        setStatus("Master toggle mislukt: " + e.message, true);
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

    function localPreviewOutputs(minute) {
      const preset = state.working || state.presets[state.activePreset];
      if (!preset || !preset.channels) return;
      state.outputs = preset.channels.map(pts => evaluateSmooth(pts, minute));
    }

    el.previewSlider.addEventListener("input", () => {
      state.previewMinute = Number(el.previewSlider.value);
      if (state.simulationActive) {
        state.simulationActive = false;
        stopSimLoop();
      }
      localPreviewOutputs(state.previewMinute);
      render();
    });

    el.previewSlider.addEventListener("change", async () => {
      try {
        const r = await api("/api/preview/set", "POST", { enabled: true, minute: state.previewMinute });
        if (r.outputs) { state.outputs = r.outputs; render(); }
      } catch (_) {}
    });

    el.btnPreviewReset.onclick = async () => {
      try {
        if (state.simulationActive) await setSimulation(false);
        if (state.previewMinute !== null) await api("/api/preview/set", "POST", { enabled: false });
      } catch (_) {}
      state.previewMinute = null;
      state.simulationActive = false;
      stopSimLoop();
      render();
      startPoll();
      setStatus("Dagcurve hervat", false);
    };

    el.btnExport.onclick = async () => {
      try {
        setStatus("Exporteren...", false);
        const res = await fetch("/api/schedule/export");
        if (!res.ok) throw new Error("HTTP " + res.status);
        const text = await res.text();
        const blob = new Blob([text], { type: "application/json" });
        const url  = URL.createObjectURL(blob);
        const a    = document.createElement("a");
        a.href = url; a.download = "aqualed-presets.json";
        document.body.appendChild(a); a.click();
        document.body.removeChild(a); URL.revokeObjectURL(url);
        setStatus("Presets geëxporteerd", false);
      } catch (e) {
        setStatus("Export mislukt: " + e.message, true);
      }
    };

    el.btnImport.onclick = () => el.fileImport.click();
    el.fileImport.onchange = async () => {
      const file = el.fileImport.files[0];
      if (!file) return;
      try {
        setStatus("Importeren...", false);
        const text = await file.text();
        let json;
        try { json = JSON.parse(text); } catch (_) { throw new Error("geen geldig JSON-bestand"); }
        const result = await api("/api/schedule/import", "POST", json);
        if (!result.ok) throw new Error(result.error || "onbekende fout");
        await loadState();
        setStatus("Presets geïmporteerd (" + result.presetCount + " stuks)", false);
      } catch (e) {
        setStatus("Import mislukt: " + e.message, true);
      }
      el.fileImport.value = "";
    };

    let pollId = null;
    let simAnchor = null;
    let simRafId = null;

    function localSimOutputs(minute) {
      const preset = state.working || state.presets[state.activePreset];
      if (!preset || !preset.channels) return;
      state.outputs = preset.channels.map(pts => evaluateSmooth(pts, minute));
    }

    function simFrame() {
      if (!state.simulationActive || !simAnchor) { simRafId = null; return; }
      const elapsed = (performance.now() - simAnchor.ts) / 1000;
      const daySeconds = state.simulationDaySeconds || 120;
      let m = simAnchor.minute + (elapsed / daySeconds) * 1440;
      while (m >= 1440) m -= 1440;
      state.nowMinute = m;
      localSimOutputs(m);
      render();
      simRafId = requestAnimationFrame(simFrame);
    }

    function startSimLoop(anchorMinute) {
      simAnchor = { minute: anchorMinute, ts: performance.now() };
      if (!simRafId) simRafId = requestAnimationFrame(simFrame);
    }

    function stopSimLoop() {
      if (simRafId) { cancelAnimationFrame(simRafId); simRafId = null; }
      simAnchor = null;
    }

    function startPoll() {
      if (pollId) return;
      pollId = setInterval(async () => {
        try {
          if (state.previewMinute !== null) { stopPoll(); return; }
          const s = await api("/api/state/light");
          state.nowMinute = s.nowMinute || 0;
          state.outputs = s.outputs || state.outputs;
          state.dateTime = s.dateTime || state.dateTime;
          state.simulationActive = !!s.simulationActive;
          state.simulationDaySeconds = Number(s.simulationDaySeconds || state.simulationDaySeconds);
          state.masterEnabled = s.masterEnabled !== false;
          if (s.previewActive) state.previewMinute = s.nowMinute;
          else if (state.previewMinute === null) state.previewMinute = null;
          if (state.simulationActive) {
            startSimLoop(s.nowMinute);
          } else {
            stopSimLoop();
            render();
          }
        } catch (_) {}
      }, 1000);
    }
    function stopPoll() { clearInterval(pollId); pollId = null; }
    startPoll();
  }

  boot().catch(err => setStatus("Initialisatie fout: " + err.message, true));
})();
</script>
</body>
</html>
)rawliteral";
