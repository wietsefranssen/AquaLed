#pragma once

#include <Arduino.h>

const char SETTINGS_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="nl">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>AquaLed Settings</title>
  <style>
    :root {
      --bg: #f6f5f0;
      --card: #ffffff;
      --text: #1c2a24;
      --muted: #5f7469;
      --line: #d7e1da;
      --brand: #1f7a66;
      --warn: #a54733;
      --ok: #2f7f3e;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      background:
        radial-gradient(circle at 0% 0%, #e8eee8 0, transparent 38%),
        radial-gradient(circle at 100% 100%, #e6eef2 0, transparent 35%),
        var(--bg);
      color: var(--text);
      font-family: "Avenir Next", "Segoe UI", sans-serif;
      min-height: 100vh;
    }
    .wrap {
      width: min(900px, 94vw);
      margin: 22px auto;
      display: grid;
      gap: 14px;
    }
    .card {
      background: var(--card);
      border: 1px solid var(--line);
      border-radius: 14px;
      box-shadow: 0 8px 22px rgba(28, 42, 36, 0.08);
      padding: 14px;
    }
    h1, h2 { margin: 0; }
    .sub { color: var(--muted); margin-top: 5px; }
    .toolbar { display: flex; gap: 10px; flex-wrap: wrap; align-items: center; }
    a.btn, button {
      border: 1px solid #bfd0c6;
      background: linear-gradient(180deg, #fbfdfc, #edf4ef);
      color: var(--text);
      border-radius: 10px;
      padding: 9px 12px;
      cursor: pointer;
      text-decoration: none;
      font-size: 0.95rem;
    }
    button.primary {
      background: linear-gradient(180deg, #2a8b75, #1f7a66);
      color: #fff;
      border-color: #1f7a66;
    }
    label { display: block; margin-bottom: 5px; color: var(--muted); }
    input {
      width: 100%;
      border-radius: 10px;
      border: 1px solid #c4d4ca;
      padding: 10px;
      font-size: 1rem;
    }
    .row { display: grid; grid-template-columns: 1fr; gap: 10px; }
    @media (min-width: 760px) {
      .row.two { grid-template-columns: 1fr 1fr; }
    }
    .status { color: var(--muted); font-size: 0.93rem; }
    .status.ok { color: var(--ok); }
    .status.err { color: var(--warn); }
    .mono { font-family: Menlo, Consolas, monospace; white-space: pre-wrap; }
  </style>
</head>
<body>
  <div class="wrap">
    <section class="card">
      <div class="toolbar">
        <a class="btn" href="/">Terug naar scheduler</a>
      </div>
      <h1>Instellingen</h1>
      <div class="sub">Wifi-credentials en handmatige tijdinstelling wanneer NTP niet beschikbaar is.</div>
    </section>

    <section class="card">
      <h2>Wifi</h2>
      <div class="row">
        <div>
          <label for="ssid">SSID</label>
          <input id="ssid" placeholder="Jouw wifi naam">
        </div>
        <div>
          <label for="password">Wachtwoord</label>
          <input id="password" type="password" placeholder="Jouw wifi wachtwoord">
        </div>
        <div>
          <label for="otaPassword">OTA wachtwoord</label>
          <input id="otaPassword" type="text" placeholder="OTA wachtwoord voor uploads">
        </div>
      </div>
      <div class="toolbar" style="margin-top:10px;">
        <button id="btnWifiSave" class="primary">Opslaan en verbinden</button>
      </div>
      <div id="wifiStatus" class="status">Nog niet geladen</div>
    </section>

    <section class="card">
      <h2>Tijd</h2>
      <div class="row two">
        <div>
          <label for="hour">Uur (0..23)</label>
          <input id="hour" type="number" min="0" max="23" value="12">
        </div>
        <div>
          <label for="minute">Minuut (0..59)</label>
          <input id="minute" type="number" min="0" max="59" value="0">
        </div>
      </div>
      <div class="toolbar" style="margin-top:10px;">
        <button id="btnTimeSet" class="primary">Handmatige tijd instellen</button>
      </div>
      <div id="timeStatus" class="status">Nog niet geladen</div>
    </section>

    <section class="card">
      <h2>Live status</h2>
      <div id="live" class="mono">laden...</div>
    </section>
  </div>

<script>
(() => {
  const el = {
    ssid: document.getElementById("ssid"),
    password: document.getElementById("password"),
    otaPassword: document.getElementById("otaPassword"),
    hour: document.getElementById("hour"),
    minute: document.getElementById("minute"),
    btnWifiSave: document.getElementById("btnWifiSave"),
    btnTimeSet: document.getElementById("btnTimeSet"),
    wifiStatus: document.getElementById("wifiStatus"),
    timeStatus: document.getElementById("timeStatus"),
    live: document.getElementById("live")
  };

  const Api = {
    async call(path, method = "GET", body = null) {
      const init = { method, headers: {} };
      if (body) {
        init.headers["Content-Type"] = "application/json";
        init.body = JSON.stringify(body);
      }
      const res = await fetch(path, init);
      if (!res.ok) throw new Error("HTTP " + res.status);
      const txt = await res.text();
      return txt ? JSON.parse(txt) : {};
    },
    state() { return this.call("/api/state"); },
    saveWifi(payload) { return this.call("/api/wifi/save", "POST", payload); },
    setTime(payload) { return this.call("/api/time/set", "POST", payload); }
  };

  function setStatus(target, text, kind = "") {
    target.textContent = text;
    target.className = "status" + (kind ? " " + kind : "");
  }

  function renderState(s) {
    if (s.ssid) el.ssid.value = s.ssid;
    if (typeof s.otaPassword === "string") el.otaPassword.value = s.otaPassword;
    const hh = Math.floor((s.nowMinute || 0) / 60);
    const mm = Math.floor((s.nowMinute || 0) % 60);
    el.live.textContent =
      "wifiConnected: " + (!!s.wifiConnected) + "\n" +
      "ssid: " + (s.ssid || "-") + "\n" +
      "stationIp: " + (s.stationIp || "0.0.0.0") + "\n" +
      "apMode: " + (!!s.apMode) + "\n" +
      "apIp: " + (s.apIp || "0.0.0.0") + "\n" +
      "otaPassword: " + ((s.otaPassword || "").length ? "ingesteld" : "leeg") + "\n" +
      "ntpSynced: " + (!!s.ntpSynced) + "\n" +
      "manualTime: " + (!!s.manualTime) + "\n" +
      "tijd: " + String(hh).padStart(2, "0") + ":" + String(mm).padStart(2, "0");
  }

  async function refresh() {
    const s = await Api.state();
    renderState(s);
    setStatus(el.wifiStatus, s.wifiConnected ? "Wifi verbonden" : "Wifi niet verbonden", s.wifiConnected ? "ok" : "");
    setStatus(el.timeStatus, s.ntpSynced ? "NTP tijd actief" : (s.manualTime ? "Handmatige tijd actief" : "Fallback tijd (uptime)"), s.ntpSynced || s.manualTime ? "ok" : "");
  }

  async function saveWifi() {
    try {
      const out = await Api.saveWifi({
        ssid: el.ssid.value.trim(),
        password: el.password.value,
        otaPassword: el.otaPassword.value
      });
      setStatus(el.wifiStatus, out.connected ? "Verbonden met wifi" : "Niet verbonden, AP mode actief", out.connected ? "ok" : "");
      await refresh();
    } catch (e) {
      setStatus(el.wifiStatus, "Opslaan mislukt: " + e.message, "err");
    }
  }

  async function setTime() {
    try {
      const hour = Number(el.hour.value);
      const minute = Number(el.minute.value);
      await Api.setTime({ hour, minute });
      setStatus(el.timeStatus, "Handmatige tijd ingesteld", "ok");
      await refresh();
    } catch (e) {
      setStatus(el.timeStatus, "Tijd instellen mislukt: " + e.message, "err");
    }
  }

  function bind() {
    el.btnWifiSave.onclick = saveWifi;
    el.btnTimeSet.onclick = setTime;
  }

  async function boot() {
    bind();
    await refresh();
    setInterval(async () => {
      try { await refresh(); } catch (_) {}
    }, 5000);
  }

  boot().catch(e => {
    setStatus(el.wifiStatus, "Fout: " + e.message, "err");
    setStatus(el.timeStatus, "Fout: " + e.message, "err");
  });
})();
</script>
</body>
</html>
)rawliteral";
