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
    :root {
      --bg: #f4f2eb;
      --bg-accent: #e5efe8;
      --card: rgba(255, 255, 255, 0.88);
      --card-strong: rgba(255, 255, 255, 0.96);
      --text: #102018;
      --muted: #5b6f64;
      --line: #d6e2d8;
      --brand: #1d6a5c;
      --brand-deep: #164f45;
      --brand-soft: #dbece5;
      --warn: #922b21;
      --warn-soft: #f8d7da;
      --shadow: 0 14px 36px rgba(26, 45, 34, 0.10);
    }
    * { box-sizing: border-box; }
    body {
      font-family: "Avenir Next", "Segoe UI", sans-serif;
      margin: 0;
      padding: 18px;
      background:
        radial-gradient(circle at top left, rgba(214, 232, 220, 0.95), transparent 34%),
        radial-gradient(circle at bottom right, rgba(214, 225, 239, 0.9), transparent 28%),
        linear-gradient(180deg, var(--bg-accent), var(--bg));
      color: var(--text);
    }
    button, input, select, a {
      border: 1px solid #b7c9bc;
      border-radius: 10px;
      padding: 10px 12px;
      background: linear-gradient(180deg, #ffffff, #f3f7f4);
      color: var(--text);
      text-decoration: none;
      transition: border-color .2s ease, box-shadow .2s ease, transform .2s ease;
    }
    button:hover, a:hover, select:hover, input:hover { border-color: #99b1a2; }
    button:focus-visible, a:focus-visible, select:focus-visible, input:focus-visible {
      outline: none;
      box-shadow: 0 0 0 3px rgba(29, 106, 92, 0.18);
    }
    button.primary { background: linear-gradient(180deg, #2a8b79, var(--brand)); border-color: var(--brand); color: #fff; }
    .page { width: min(1240px, 100%); margin: 0 auto; }
    .card {
      background: var(--card);
      backdrop-filter: blur(10px);
      border: 1px solid rgba(214, 226, 216, 0.95);
      border-radius: 18px;
      padding: 16px;
      margin-bottom: 14px;
      box-shadow: var(--shadow);
    }
    .hero {
      display: grid;
      gap: 14px;
      align-items: center;
      background:
        linear-gradient(135deg, rgba(255,255,255,0.96), rgba(238, 246, 241, 0.92)),
        var(--card-strong);
    }
    @media (min-width: 900px) { .hero { grid-template-columns: 1.35fr .85fr; } }
    .hero h1 { margin: 0; font-size: clamp(1.6rem, 3vw, 2.4rem); }
    .hero p { margin: 8px 0 0; color: var(--muted); max-width: 60ch; }
    .hero-actions { display: flex; flex-wrap: wrap; gap: 10px; justify-content: flex-start; }
    .hero-stats { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 10px; }
    .stat {
      padding: 12px;
      border-radius: 14px;
      background: rgba(255, 255, 255, 0.75);
      border: 1px solid rgba(214, 226, 216, 0.95);
      min-height: 82px;
    }
    .stat-kicker { display: block; font-size: .78rem; text-transform: uppercase; letter-spacing: .06em; color: #6d8277; margin-bottom: 6px; }
    .stat-value { font-size: 1.15rem; font-weight: 700; }
    .stat-note { margin-top: 4px; font-size: .86rem; color: var(--muted); }
    .section-head { display: flex; flex-wrap: wrap; align-items: flex-end; justify-content: space-between; gap: 8px; margin-bottom: 12px; }
    .section-head h2, .section-head h3 { margin: 0; }
    .section-sub { color: var(--muted); font-size: .92rem; }
    .toolbar { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }
    .preset-grid { display: grid; gap: 12px; }
    @media (min-width: 880px) { .preset-grid { grid-template-columns: minmax(0, 1.2fr) minmax(0, .8fr); } }
    .control-panel {
      border: 1px solid var(--line);
      border-radius: 14px;
      background: rgba(255,255,255,0.65);
      padding: 12px;
      display: grid;
      gap: 10px;
    }
    .control-grid { display: grid; gap: 10px; }
    @media (min-width: 760px) { .control-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); } }
    .field { display: grid; gap: 6px; }
    .field label { font-size: .85rem; color: var(--muted); font-weight: 600; }
    .button-cluster { display: flex; flex-wrap: wrap; gap: 8px; }
    .button-cluster.vertical { flex-direction: column; align-items: stretch; }
    .status { font-size: .9rem; color: var(--muted); }
    .channels { display: grid; gap: 12px; }
    .ch {
      border: 1px solid var(--line);
      border-radius: 14px;
      padding: 10px;
      background: linear-gradient(180deg, rgba(255,255,255,0.88), rgba(246,250,247,0.92));
    }
    .ch-title { font-weight: 700; margin-bottom: 6px; }
    canvas { width: 100%; height: 160px; display: block; border: 1px solid #d4e0d8; border-radius: 10px; background: #f9fcfa; touch-action: none; }
    pre { white-space: pre-wrap; }
    .small { font-size: .85rem; color: var(--muted); }
    .small.strong { color: var(--brand-deep); font-weight: 600; }
    @media (min-width: 980px) { .layout { display: grid; grid-template-columns: 1.45fr .8fr; gap: 14px; } }
    .live-grid { display: flex; flex-direction: column; gap: 10px; }
    .live-row { display: flex; align-items: center; gap: 8px; font-size: .92rem; flex-wrap: wrap; }
    .live-label { color: var(--muted); min-width: 84px; }
    .live-value { font-weight: 600; }
    .live-divider { border: none; border-top: 1px solid #e4ebe6; margin: 2px 0; }
    .ch-bar-row { display: flex; align-items: center; gap: 8px; margin-bottom: 6px; }
    .ch-swatch { width: 14px; height: 14px; border-radius: 50%; flex-shrink: 0; border: 1px solid rgba(0,0,0,.12); }
    .ch-name { min-width: 28px; font-size: .85rem; color: var(--muted); }
    .ch-bar-track { flex: 1; height: 18px; background: #eef2ee; border-radius: 999px; overflow: hidden; position: relative; }
    .ch-bar-fill { height: 100%; border-radius: 999px; transition: width .3s ease, background .3s ease; min-width: 0; }
    .ch-bar-pct { min-width: 38px; text-align: right; font-size: .85rem; font-weight: 600; font-variant-numeric: tabular-nums; }
    .live-badge { display: inline-block; padding: 4px 10px; border-radius: 999px; font-size: .76rem; font-weight: 700; letter-spacing: .03em; }
    .badge-on { background: #d4edda; color: var(--brand); }
    .badge-off { background: var(--warn-soft); color: var(--warn); }
    .badge-sim { background: #fff3cd; color: #856404; }
    .badge-preview { background: #d1ecf1; color: #0c5460; }
    .hint {
      font-size: .84rem;
      color: #6d8277;
      margin-bottom: 10px;
      padding: 10px 12px;
      border-radius: 12px;
      background: rgba(239, 246, 241, 0.9);
      border: 1px solid rgba(214, 226, 216, 0.9);
    }
    .curve-shell { padding: 2px; }
    .preview-stack { display: grid; gap: 12px; }
    .slider-row { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
    .slider-row input[type="range"] { flex: 1; min-width: 150px; }
    .info-pills { display: flex; flex-wrap: wrap; gap: 8px; }
    .pill {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      padding: 6px 10px;
      border-radius: 999px;
      background: var(--brand-soft);
      color: var(--brand-deep);
      font-size: .82rem;
      font-weight: 600;
    }
    .ghost-button { background: #fff; }
    .danger-button { color: var(--warn); border-color: #e6a19a; }
    .version-tag { position: fixed; bottom: 6px; right: 10px; font-size: .72rem; color: #7a8f82; opacity: .5; pointer-events: none; z-index: 999; }
  </style>
</head>
<body>
  <div class="page">
    <section class="card hero">
      <div>
        <h1>AquaLed Dagcurve</h1>
        <p>Stel lichtcurves per kanaal in, bekijk het resultaat op een gekozen tijdstip en beheer meerdere presets voor verschillende dagprofielen.</p>
        <div class="hero-actions" style="margin-top:14px;">
          <a href="/settings">⚙ Instellingen</a>
          <button id="btnMasterToggle" class="primary" style="min-width:120px;">● Verlichting aan</button>
        </div>
      </div>
      <div class="hero-stats">
        <div class="stat">
          <span class="stat-kicker">Actieve preset</span>
          <div id="presetMetaName" class="stat-value">-</div>
          <div id="presetMetaCount" class="stat-note">Presetbibliotheek wordt geladen</div>
        </div>
        <div class="stat">
          <span class="stat-kicker">Testmodus</span>
          <div id="simStateHero" class="stat-value">Live</div>
          <div class="stat-note">Schakel snel tussen live weergave, preview en simulatie</div>
        </div>
        <div class="stat">
          <span class="stat-kicker">Weergave</span>
          <div id="previewHeroTime" class="stat-value">--:--</div>
          <div class="stat-note">Tijdstip dat nu in beeld is</div>
        </div>
      </div>
    </section>

    <section class="card">
      <div class="section-head">
        <div>
          <h2>Presetbibliotheek</h2>
          <div class="section-sub">Kies een bestaand dagprofiel, werk het bij of maak vanuit je huidige curve snel een nieuwe preset aan.</div>
        </div>
        <span id="status" class="status">Klaar</span>
      </div>
      <div class="preset-grid">
        <div class="control-panel">
          <div class="field">
            <label for="presetSelect">Actieve preset</label>
            <select id="presetSelect"></select>
          </div>
          <div class="field">
            <label for="activePresetName">Naam van actieve preset</label>
            <input id="activePresetName" placeholder="Huidige presetnaam aanpassen">
          </div>
          <div class="button-cluster">
            <button id="btnOverwrite">Preset bijwerken</button>
            <button id="btnRename">Preset hernoemen</button>
            <button id="btnDelete" class="danger-button">Preset verwijderen</button>
          </div>
          <div class="small">Bijwerken slaat de huidige curve op in de geselecteerde preset. Hernoemen wijzigt alleen de naam van de geselecteerde preset.</div>
        </div>
        <div class="control-panel">
          <div class="field">
            <label for="presetName">Naam voor nieuwe preset</label>
            <input id="presetName" placeholder="Bijvoorbeeld: Ochtendrif of Avondblauw">
          </div>
          <div class="button-cluster vertical">
            <button id="btnSaveNew" class="primary">Nieuwe preset opslaan</button>
          </div>
          <div class="field">
            <label>Bibliotheekacties</label>
            <div class="button-cluster">
              <button id="btnExport" title="Download alle presets als JSON-bestand">⬇ Presets exporteren</button>
              <button id="btnImport" title="Importeer presets uit JSON-bestand">⬆ Presets importeren</button>
            </div>
            <input id="fileImport" type="file" accept=".json" style="display:none">
          </div>
          <div class="info-pills">
            <span class="pill">Tot 10 presets beschikbaar</span>
            <span class="pill">Import/export neemt actieve selectie mee</span>
          </div>
        </div>
      </div>
    </section>

    <section class="card">
      <div class="section-head">
        <div>
          <h2>Testen & tijdsweergave</h2>
          <div class="section-sub">Start een simulatie voor een volledige dag of kies een previewtijd om direct te zien hoe de huidige preset eruitziet.</div>
        </div>
      </div>
      <div class="preview-stack">
        <div class="control-grid">
          <div class="control-panel">
            <div class="field">
              <label for="simSeconds">Simulatiesnelheid</label>
              <div class="slider-row">
                <select id="simSeconds">
                  <option value="10">10 sec</option>
                  <option value="30">30 sec</option>
                  <option value="60">1 min</option>
                  <option value="120">2 min</option>
                  <option value="300">5 min</option>
                  <option value="600">10 min</option>
                </select>
                <button id="btnSimStart" class="primary">Simulatie starten</button>
              </div>
            </div>
            <span id="simState" class="small strong">Live weergave</span>
          </div>
          <div class="control-panel">
            <div class="field">
              <label for="brightnessSlider">Master helderheid</label>
              <div class="slider-row">
                <input type="range" id="brightnessSlider" min="0" max="200" value="100" step="1">
                <span id="brightnessVal" class="small strong" style="min-width:52px;">100%</span>
              </div>
            </div>
            <div class="small">Schaalt alleen de getoonde lichtsterkte en verandert de preset zelf niet.</div>
          </div>
        </div>

        <div class="control-panel">
          <div class="field">
            <label for="previewSlider">Previewtijd</label>
            <div class="slider-row">
              <input type="range" id="previewSlider" min="0" max="1439" value="0">
              <span id="previewTime" class="small strong" style="min-width:54px;">--:--</span>
            </div>
          </div>
          <div class="info-pills">
            <span class="pill">Sleep om een tijdstip te bekijken</span>
            <span class="pill">Preview pauzeert live verversen tijdelijk</span>
          </div>
        </div>

        <div class="control-panel" id="resumeBar" style="display:none;">
          <button id="btnResume" class="primary" style="flex:1;">Live weergave hervatten</button>
        </div>
      </div>
    </section>

    <div class="layout">
      <section class="card curve-shell">
        <div class="section-head">
          <div>
            <h2>Curve-editor</h2>
            <div class="section-sub">Klik om punten toe te voegen, sleep voor finetuning en gebruik rechtsklik om een punt te verwijderen.</div>
          </div>
          <div class="button-cluster">
            <button id="btnCurveEditLock" class="ghost-button" title="Voorkom per ongeluk aanpassen van de curve">🔒 Curve vergrendeld</button>
            <button id="btnRevert">Aanpassingen ongedaan maken</button>
          </div>
        </div>
        <div class="hint">De verticale markering volgt live tijd of previewtijd. Ongedaan maken herlaadt de opgeslagen preset zonder iets op te slaan. De gecombineerde grafiek toont het resultaat na helderheidsscaling.</div>
        <div style="font-weight:700;font-size:.86rem;margin-bottom:6px;color:var(--muted);">Gecombineerd kanaaloverzicht</div>
        <canvas id="canvasCombined" style="width:100%;height:110px;display:block;border:1px solid #d4e0d8;border-radius:10px;background:#f9fcfa;margin-bottom:12px;"></canvas>
        <div id="channels" class="channels"></div>
      </section>
      <section class="card">
        <div class="section-head">
          <div>
            <h3>Live overzicht</h3>
            <div class="section-sub">Status, output en geschat verbruik van de actieve preset.</div>
          </div>
        </div>
        <div id="live" class="live-grid">laden...</div>
      </section>
    </div>
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
    masterBrightness: 100,
    moonPhase: 0.5,
    moonlightEnabled: false,
    moonlightChannel: -1,
    moonlightIntensity: 492,
    moonlightActive: false,
    cloudSimEnabled: false,
    cloudActive: false,
    cloudNextInSec: -1,
    cloudEventsPerDay: 100,
    cloudAvgDurationSec: 5,
    curveEditUnlocked: false,
    working: null,
    dragging: null,
    canvases: [],
    colors: ["#1f7a8c", "#2d936c", "#8f6c4e", "#ba5a31", "#7b4fa3"],
    channelMaxWatts: [0, 0, 0, 0, 0]
  };

  const el = {
    presetMetaName: document.getElementById("presetMetaName"),
    presetMetaCount: document.getElementById("presetMetaCount"),
    simStateHero: document.getElementById("simStateHero"),
    previewHeroTime: document.getElementById("previewHeroTime"),
    presetSelect: document.getElementById("presetSelect"),
    activePresetName: document.getElementById("activePresetName"),
    presetName: document.getElementById("presetName"),
    btnSaveNew: document.getElementById("btnSaveNew"),
    btnOverwrite: document.getElementById("btnOverwrite"),
    btnRename: document.getElementById("btnRename"),
    btnRevert: document.getElementById("btnRevert"),
    btnDelete: document.getElementById("btnDelete"),
    btnCurveEditLock: document.getElementById("btnCurveEditLock"),
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
    fileImport: document.getElementById("fileImport"),
    brightnessSlider: document.getElementById("brightnessSlider"),
    brightnessVal:    document.getElementById("brightnessVal"),
    canvasCombined:   document.getElementById("canvasCombined")
  };

  const clone = (v) => JSON.parse(JSON.stringify(v));
  const minuteToX = (m, w) => Math.max(0, Math.min(w, (m / DAY_MIN) * w));
  const valueToY = (v, h) => Math.max(0, Math.min(h, h - (v / 4095) * h));
  const xToMinute = (x, w) => Math.round((Math.max(0, Math.min(w, x)) / w) * DAY_MIN);
  const yToValue = (y, h) => Math.round(((h - Math.max(0, Math.min(h, y))) / h) * 4095);
  const smoothStep = (t) => (t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t));
  const toPct = (v) => Math.round(v / 4095 * 100);
  const fmtMin = (m) => String(Math.floor(m / 60)).padStart(2, "0") + ":" + String(Math.floor(m % 60)).padStart(2, "0");
  const fmtClock = (d) => String(d.getHours()).padStart(2, "0") + ":" + String(d.getMinutes()).padStart(2, "0") + ":" + String(d.getSeconds()).padStart(2, "0");

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

  function drawCombined() {
    const c = el.canvasCombined;
    if (!c) return;
    const ctx = c.getContext("2d");
    const dpr = window.devicePixelRatio || 1;
    const r = c.getBoundingClientRect();
    const w = Math.floor(r.width), h = Math.floor(r.height);
    if (w === 0 || h === 0) return;
    c.width = w * dpr; c.height = h * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    drawAxes(ctx, w, h);

    const scale = state.masterBrightness / 100;
    const moonTarget = state.moonlightEnabled && state.moonlightChannel >= 0
      ? (state.moonlightIntensity || 0) * (state.moonPhase || 0)
      : -1;
    const preset = state.working || state.presets[state.activePreset];
    if (preset && preset.channels) {
      const samples = 240;
      for (let i = 0; i < CHANNELS; i++) {
        const points = preset.channels[i];
        const col = state.colors[i % state.colors.length];
        ctx.strokeStyle = col;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        for (let j = 0; j <= samples; j++) {
          const minute = (j / samples) * DAY_MIN;
          let val = evaluateSmooth(points, minute);
          if (i === state.moonlightChannel && moonTarget >= 0) val = Math.max(val, moonTarget);
          const scaled = Math.min(4095, val * scale);
          const x = minuteToX(minute, w);
          const y = valueToY(scaled, h);
          if (j === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        }
        ctx.stroke();
      }
    }
    // Verticale tijdlijn
    const nowX = minuteToX(state.previewMinute !== null ? state.previewMinute : state.nowMinute, w);
    ctx.strokeStyle = "#24362b";
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(nowX, 0); ctx.lineTo(nowX, h); ctx.stroke();
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
    drawCombined();
    const displayMin = state.previewMinute !== null ? state.previewMinute : state.nowMinute;
    const masterOn = state.masterEnabled;
    const simOn = state.simulationActive;
    const prevOn = state.previewMinute !== null;
    let badges = '';
    if (masterOn) badges += '<span class="live-badge badge-on">AAN</span> ';
    else badges += '<span class="live-badge badge-off">UIT</span> ';
    if (simOn) badges += '<span class="live-badge badge-sim">SIM ' + state.simulationDaySeconds + 's</span> ';
    if (prevOn) badges += '<span class="live-badge badge-preview">PREVIEW</span> ';
    if (state.cloudSimEnabled) {
      badges += '<span class="live-badge badge-sim">WOLKEN ' + (state.cloudActive ? 'ACTIEF' : 'AAN') + '</span> ';
    }
    if (state.moonlightEnabled && state.moonlightChannel >= 0) {
      badges += '<span class="live-badge badge-sim">MAAN ' + (state.moonlightActive ? 'ACTIEF' : 'AAN') + '</span> ';
    }

    let bars = '';
    for (let i = 0; i < CHANNELS; i++) {
      const pct = toPct(state.outputs[i]);
      const col = state.colors[i % state.colors.length];
      const maxW = state.channelMaxWatts[i] || 0;
      const wattStr = maxW > 0 ? ' <span style="color:var(--muted);font-size:.83rem">(' + (pct / 100 * maxW).toFixed(1) + ' W)</span>' : '';
      bars += '<div class="ch-bar-row">'
        + '<span class="ch-swatch" style="background:' + col + '"></span>'
        + '<span class="ch-name">' + (i + 1) + '</span>'
        + '<div class="ch-bar-track"><div class="ch-bar-fill" style="width:' + pct + '%;background:' + col + ';"></div></div>'
        + '<span class="ch-bar-pct">' + pct + '%' + wattStr + '</span>'
        + '</div>';
    }

    // Dagverbruik
    const estimateDailyWh = () => {
      const preset = state.working || state.presets[state.activePreset];
      if (!preset || !preset.channels) return null;
      const scale = state.masterBrightness / 100;
      const moonTarget = state.moonlightEnabled && state.moonlightChannel >= 0
        ? (state.moonlightIntensity || 0) * (state.moonPhase || 0) : -1;
      let totalWh = 0, hasWatts = false;
      for (let i = 0; i < CHANNELS; i++) {
        const maxW = state.channelMaxWatts[i] || 0;
        if (maxW <= 0) continue;
        hasWatts = true;
        const samples = 240;
        let sum = 0;
        for (let j = 0; j <= samples; j++) {
          const minute = (j / samples) * DAY_MIN;
          let val = evaluateSmooth(preset.channels[i], minute);
          if (i === state.moonlightChannel && moonTarget >= 0) val = Math.max(val, moonTarget);
          sum += Math.min(4095, val * scale);
        }
        totalWh += (sum / (samples + 1) / 4095) * maxW * 24;
      }
      return hasWatts ? totalWh : null;
    };
    const dailyWh = estimateDailyWh();
    const dailyRow = dailyWh !== null
      ? '<div class="live-row" style="margin-top:4px;"><span class="live-label">Dagverbruik</span><span class="live-value">~' + (dailyWh >= 1000 ? (dailyWh / 1000).toFixed(2) + ' kWh' : dailyWh.toFixed(0) + ' Wh') + '</span></div>'
      : '';
    const cloudEtaText = (() => {
      if (!state.cloudSimEnabled) return "";
      if (state.cloudActive) return "nu actief";
      const sec = Number(state.cloudNextInSec);
      if (!Number.isFinite(sec) || sec <= 0) return "binnenkort";
      const around = new Date(Date.now() + sec * 1000);
      return "over " + sec + " sec (rond " + fmtClock(around) + ")";
    })();

    el.live.innerHTML =
      '<div class="live-row">' + badges + '</div>'
      + '<hr class="live-divider">'
      + '<div class="live-row"><span class="live-label">Preset</span><span class="live-value">' + (state.presets[state.activePreset]?.name || "-") + '</span></div>'
      + '<div class="live-row"><span class="live-label">Datum</span><span class="live-value">' + (state.dateTime || "-") + '</span></div>'
        + (state.cloudSimEnabled
          ? '<div class="live-row"><span class="live-label">Volgende wolk</span><span class="live-value">'
            + cloudEtaText
            + ' (gem. ' + state.cloudEventsPerDay + 'x/dag, ~' + state.cloudAvgDurationSec + ' sec)</span></div>'
          : '')
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
      + bars
      + dailyRow;

    el.simSeconds.value = state.simulationDaySeconds;
    el.simState.textContent = state.simulationActive
      ? "Simulatie actief · 1 dag in " + state.simulationDaySeconds + " sec"
      : (state.previewMinute !== null ? "Preview · " + fmtMin(state.previewMinute) : "Live weergave");
    el.simStateHero.textContent = state.simulationActive
      ? "Simulatie"
      : (state.previewMinute !== null ? "Preview" : "Live");
    el.previewHeroTime.textContent = fmtMin(displayMin);
    el.presetMetaName.textContent = state.presets[state.activePreset]?.name || "-";
    el.presetMetaCount.textContent = state.presets.length + " preset" + (state.presets.length === 1 ? "" : "s") + " beschikbaar";

    el.btnMasterToggle.textContent = state.masterEnabled ? "● Verlichting aan" : "● Verlichting uit";
    el.btnMasterToggle.style.background  = state.masterEnabled ? "" : "#c0392b";
    el.btnMasterToggle.style.borderColor = state.masterEnabled ? "" : "#922b21";
    el.brightnessSlider.value = state.masterBrightness;
    el.brightnessVal.textContent = state.masterBrightness + "%";

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
      if (!state.curveEditUnlocked) {
        state.dragging = null;
        return;
      }
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
      box.innerHTML = `<div class="ch-title">Kanaal ${i + 1}</div><canvas></canvas>`;
      const c = box.querySelector("canvas");
      state.canvases.push(c);
      bindCanvas(c, i);
      el.channels.appendChild(box);
    }
  }

  function updateCurveEditLockUi() {
    if (!el.btnCurveEditLock) return;
    const unlocked = !!state.curveEditUnlocked;
    el.btnCurveEditLock.textContent = unlocked ? "🔓 Curve bewerken ingeschakeld" : "🔒 Curve vergrendeld";
    el.btnCurveEditLock.style.background = unlocked ? "#fff3cd" : "#fff";
    el.btnCurveEditLock.style.borderColor = unlocked ? "#d8b861" : "#b7c9bc";
    el.btnCurveEditLock.style.color = unlocked ? "#6a4f00" : "#102018";
    state.canvases.forEach((c) => {
      c.style.cursor = unlocked ? "crosshair" : "default";
      c.style.opacity = unlocked ? "1" : "0.88";
    });
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
    state.masterBrightness = typeof s.masterBrightness === "number" ? Math.round(s.masterBrightness * 100) : state.masterBrightness;
    state.moonPhase         = typeof s.moonPhase === "number" ? s.moonPhase : 0.5;
    state.moonlightEnabled  = !!s.moonlightEnabled;
    state.moonlightChannel  = typeof s.moonlightChannel === "number" ? s.moonlightChannel : -1;
    state.moonlightIntensity = typeof s.moonlightIntensity === "number" ? s.moonlightIntensity : 492;
    state.moonlightActive    = !!s.moonlightActive;
    state.cloudSimEnabled    = !!s.cloudSimEnabled;
    state.cloudActive        = !!s.cloudActive;
    state.cloudNextInSec     = typeof s.cloudNextInSec === "number" ? s.cloudNextInSec : -1;
    state.cloudEventsPerDay  = typeof s.cloudEventsPerDay === "number" ? s.cloudEventsPerDay : 100;
    state.cloudAvgDurationSec = typeof s.cloudAvgDurationSec === "number" ? s.cloudAvgDurationSec : 5;
    if (Array.isArray(s.channelMaxWatts) && s.channelMaxWatts.length === CHANNELS)
      state.channelMaxWatts = s.channelMaxWatts.map(Number);
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

    el.activePresetName.value = state.presets[state.activePreset]?.name || "";

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
    updateCurveEditLockUi();
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
      try { await savePreset(true); setStatus("Nieuwe preset opgeslagen", false); }
      catch (e) { setStatus("Opslaan mislukt: " + e.message, true); }
    };

    el.btnOverwrite.onclick = async () => {
      try {
        const idx = Number(el.presetSelect.value || 0);
        state.working.name = state.presets[idx]?.name || state.working.name;
        await savePreset(false);
        setStatus("Actieve preset bijgewerkt", false);
      } catch (e) {
        setStatus("Opslaan mislukt: " + e.message, true);
      }
    };

    el.btnRename.onclick = async () => {
      try {
        const idx = Number(el.presetSelect.value || 0);
        const newName = el.activePresetName.value.trim();
        if (!newName) {
          setStatus("Geef eerst een naam op voor de actieve preset", true);
          return;
        }
        state.working.name = newName;
        for (let i = 0; i < CHANNELS; i++) state.working.channels[i] = sortAndClamp(state.working.channels[i]);
        await api("/api/preset/upsert", "POST", {
          index: idx,
          name: newName,
          channels: state.working.channels
        });
        await loadState();
        setStatus("Preset hernoemd", false);
      } catch (e) {
        setStatus("Hernoemen mislukt: " + e.message, true);
      }
    };

    el.btnRevert.onclick = async () => {
      try {
        await loadState();
        setStatus("Lokale aanpassingen ongedaan gemaakt", false);
      } catch (e) {
        setStatus("Ongedaan maken mislukt: " + e.message, true);
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

    el.btnCurveEditLock.onclick = () => {
      state.curveEditUnlocked = !state.curveEditUnlocked;
      if (!state.curveEditUnlocked) state.dragging = null;
      updateCurveEditLockUi();
      setStatus(state.curveEditUnlocked ? "Curve bewerken ingeschakeld" : "Curve veilig vergrendeld", false);
    };

    el.btnSimStart.onclick = async () => {
      try {
        state.previewMinute = null;
        stopSimLoop();
        await setSimulation(true);
        setStatus("Versnelde simulatie gestart", false);
      } catch (e) {
        setStatus("Simulatie mislukt: " + e.message, true);
      }
    };

    el.btnMasterToggle.onclick = async () => {
      try {
        const next = !state.masterEnabled;
        await api("/api/master/set", "POST", { enabled: next });
        state.masterEnabled = next;
        render();
        setStatus("Master " + (next ? "ingeschakeld" : "uitgeschakeld"), false);

        // Animeer de bars lokaal over 2 sec, synchroon met de firmware-fade
        const from = state.outputs.slice();
        const preset = state.presets[state.activePreset];
        const to = next && preset
          ? preset.channels.map(pts => evaluateSmooth(pts, state.nowMinute))
          : state.outputs.map(() => 0);
        const duration = 2000;
        const start = performance.now();
        const rafId = { id: null };
        function animateMaster() {
          const t = Math.min(1, (performance.now() - start) / duration);
          const ease = t * t * (3 - 2 * t);
          state.outputs = from.map((f, i) => Math.round(f + (to[i] - f) * ease));
          render();
          if (t < 1) rafId.id = requestAnimationFrame(animateMaster);
        }
        rafId.id = requestAnimationFrame(animateMaster);
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

    window.addEventListener("resize", () => { render(); });

    let _brightDebounce = null;
    el.brightnessSlider.addEventListener("input", () => {
      state.masterBrightness = Number(el.brightnessSlider.value);
      el.brightnessVal.textContent = state.masterBrightness + "%";
      drawCombined();
      if (_brightDebounce) clearTimeout(_brightDebounce);
      _brightDebounce = setTimeout(async () => {
        try { await api("/api/brightness/set", "POST", { brightness: state.masterBrightness / 100 }); }
        catch(_) {}
      }, 200);
    });

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
          state.cloudSimEnabled = !!s.cloudSimEnabled;
          state.cloudActive = !!s.cloudActive;
          state.moonlightEnabled = !!s.moonlightEnabled;
          state.moonlightChannel = typeof s.moonlightChannel === "number" ? s.moonlightChannel : state.moonlightChannel;
          state.moonlightIntensity = typeof s.moonlightIntensity === "number" ? s.moonlightIntensity : state.moonlightIntensity;
          state.moonPhase = typeof s.moonPhase === "number" ? s.moonPhase : state.moonPhase;
          state.moonlightActive = !!s.moonlightActive;
          if (typeof s.cloudNextInSec === "number") state.cloudNextInSec = s.cloudNextInSec;
          if (typeof s.cloudEventsPerDay === "number") state.cloudEventsPerDay = s.cloudEventsPerDay;
          if (typeof s.cloudAvgDurationSec === "number") state.cloudAvgDurationSec = s.cloudAvgDurationSec;
          if (typeof s.masterBrightness === "number") {
            state.masterBrightness = Math.round(s.masterBrightness * 100);
            el.brightnessSlider.value = state.masterBrightness;
            el.brightnessVal.textContent = state.masterBrightness + "%";
          }
          if (Array.isArray(s.channelMaxWatts) && s.channelMaxWatts.length === CHANNELS)
            state.channelMaxWatts = s.channelMaxWatts.map(Number);
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
