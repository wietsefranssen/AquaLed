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
    .version-tag { position: fixed; bottom: 6px; right: 10px; font-size: .72rem; color: var(--muted); opacity: .5; pointer-events: none; z-index: 999; }
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
      <h2>Home Assistant (MQTT)</h2>
      <div class="sub">Verbind met een MQTT broker voor HA auto-discovery. Entiteiten verschijnen automatisch in Home Assistant: master-schakelaar, simulatie, preset-keuze en 5 kanaal-sensoren.<br>Tip: gebruik de Mosquitto add-on in HA en maak een MQTT-gebruiker aan.</div>
      <div class="row" style="margin-top:10px;">
        <div style="display:flex;align-items:center;gap:12px;">
          <label style="margin:0;width:auto">MQTT inschakelen</label>
          <input id="mqttEnabled" type="checkbox" style="width:auto;accent-color:var(--brand);">
        </div>
        <div>
          <label for="mqttBroker">Broker adres</label>
          <input id="mqttBroker" placeholder="192.168.1.x of hostname">
        </div>
        <div>
          <label for="mqttPort">Poort</label>
          <input id="mqttPort" type="number" value="1883" min="1" max="65535" placeholder="1883">
        </div>
        <div>
          <label for="mqttUser">Gebruikersnaam (optioneel)</label>
          <input id="mqttUser" placeholder="MQTT gebruikersnaam">
        </div>
        <div>
          <label for="mqttPass">Wachtwoord (optioneel)</label>
          <input id="mqttPass" type="password" placeholder="MQTT wachtwoord">
        </div>
      </div>
      <div class="toolbar" style="margin-top:10px;">
        <button id="btnMqttSave" class="primary">Opslaan en verbinden</button>
      </div>
      <div id="mqttStatus" class="status">Nog niet geladen</div>
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

    <section class="card">
      <h2>Tijdzone</h2>
      <div class="row">
        <div>
          <label for="timezone">Tijdzone</label>
          <select id="timezone" style="width:100%;border-radius:10px;border:1px solid #c4d4ca;padding:10px;font-size:1rem;">
            <option value="CET-1CEST,M3.5.0/2,M10.5.0/3">Europa/Amsterdam</option>
            <option value="GMT0BST,M3.5.0/1,M10.5.0">Europa/Londen</option>
            <option value="CET-1CEST,M3.5.0,M10.5.0/3">Europa/Berlijn</option>
            <option value="CET-1CEST,M3.5.0,M10.5.0/3">Europa/Parijs</option>
            <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Europa/Helsinki</option>
            <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Europa/Athene</option>
            <option value="EST5EDT,M3.2.0,M11.1.0">Amerika/New York</option>
            <option value="CST6CDT,M3.2.0,M11.1.0">Amerika/Chicago</option>
            <option value="MST7MDT,M3.2.0,M11.1.0">Amerika/Denver</option>
            <option value="PST8PDT,M3.2.0,M11.1.0">Amerika/Los Angeles</option>
            <option value="AEST-10AEDT,M10.1.0,M4.1.0/3">Australi&euml;/Sydney</option>
            <option value="JST-9">Azi&euml;/Tokyo</option>
            <option value="GMT0">UTC</option>
          </select>
        </div>
      </div>
      <div id="tzStatus" class="status"></div>
    </section>

    <section class="card">
      <div class="sub">Kies een kleur per kanaal voor de curvegrafieken.</div>
      <div class="row" style="margin-top:10px;">
        <div style="display:flex;gap:14px;flex-wrap:wrap;align-items:center;" id="colorPickers"></div>
      </div>
      <div class="toolbar" style="margin-top:10px;">
        <button id="btnColorSave" class="primary">Kleuren opslaan</button>
      </div>
      <div id="colorStatus" class="status"></div>
    </section>

    <section class="card">
      <h2>Maanlicht simulatie</h2>
      <div class="sub">Reserveer een kanaal voor maanlicht: de maanfase-intensiteit wordt gebruikt als minimumhelderheid. Zit de preset-curve hoger, dan wint de preset.</div>
      <div class="row" style="margin-top:10px;">
        <div style="display:flex;align-items:center;gap:12px;">
          <label style="margin:0;width:auto">Maanlicht inschakelen</label>
          <input id="moonlightEnabled" type="checkbox" style="width:auto;accent-color:var(--brand);">
        </div>
        <div>
          <label for="moonlightChannel">Kanaal</label>
          <select id="moonlightChannel" style="width:100%;border-radius:10px;border:1px solid #c4d4ca;padding:10px;font-size:1rem;">
            <option value="-1">-- geen --</option>
            <option value="0">Kanaal 1</option>
            <option value="1">Kanaal 2</option>
            <option value="2">Kanaal 3</option>
            <option value="3">Kanaal 4</option>
            <option value="4">Kanaal 5</option>
          </select>
        </div>
        <div>
          <label for="moonlightIntensity">Intensiteit: <span id="moonlightIntensityVal">30</span>/255</label>
          <input id="moonlightIntensity" type="range" min="0" max="255" value="30" style="width:100%;accent-color:var(--brand);">
        </div>
      </div>
      <div class="toolbar" style="margin-top:10px;">
        <button id="btnMoonlightSave" class="primary">Opslaan</button>
      </div>
      <div id="moonlightStatus" class="status"></div>
    </section>

    <section class="card">
      <h2>Firmware update (OTA)</h2>
      <div class="sub">Upload een nieuw firmware bestand (.bin) om de controller draadloos bij te werken.</div>
      <div class="row" style="margin-top:10px;">
        <div>
          <label for="firmwareFile">Firmware bestand</label>
          <input id="firmwareFile" type="file" accept=".bin">
        </div>
      </div>
      <div class="toolbar" style="margin-top:10px;">
        <button id="btnOtaUpload" class="primary">Firmware uploaden</button>
      </div>
      <progress id="otaProgress" value="0" max="100" style="width:100%;display:none;margin-top:10px;height:22px;border-radius:8px;"></progress>
      <div id="otaStatus" class="status"></div>
    </section>
  </div>
  <div id="versionTag" class="version-tag"></div>

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
    live: document.getElementById("live"),
    colorPickers: document.getElementById("colorPickers"),
    btnColorSave: document.getElementById("btnColorSave"),
    colorStatus: document.getElementById("colorStatus"),
    timezone: document.getElementById("timezone"),
    tzStatus: document.getElementById("tzStatus"),
    mqttEnabled: document.getElementById("mqttEnabled"),
    mqttBroker:  document.getElementById("mqttBroker"),
    mqttPort:    document.getElementById("mqttPort"),
    mqttUser:    document.getElementById("mqttUser"),
    mqttPass:    document.getElementById("mqttPass"),
    btnMqttSave: document.getElementById("btnMqttSave"),
    mqttStatus:  document.getElementById("mqttStatus"),
    moonlightEnabled:    document.getElementById("moonlightEnabled"),
    moonlightChannel:    document.getElementById("moonlightChannel"),
    moonlightIntensity:  document.getElementById("moonlightIntensity"),
    moonlightIntensityVal: document.getElementById("moonlightIntensityVal"),
    btnMoonlightSave:    document.getElementById("btnMoonlightSave"),
    moonlightStatus:     document.getElementById("moonlightStatus"),
    firmwareFile: document.getElementById("firmwareFile"),
    btnOtaUpload: document.getElementById("btnOtaUpload"),
    otaProgress:  document.getElementById("otaProgress"),
    otaStatus:    document.getElementById("otaStatus")
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
    setTime(payload) { return this.call("/api/time/set", "POST", payload); },
    saveColors(payload) { return this.call("/api/colors/save", "POST", payload); },
    saveMqtt(payload)   { return this.call("/api/mqtt/save",   "POST", payload); },
    saveMoonlight(payload) { return this.call("/api/moonlight/save", "POST", payload); }
  };

  const defaultColors = ["#1f7a8c", "#2d936c", "#8f6c4e", "#ba5a31", "#7b4fa3"];
  let channelColors = [...defaultColors];

  function buildColorPickers() {
    el.colorPickers.innerHTML = "";
    for (let i = 0; i < 5; i++) {
      const label = document.createElement("label");
      label.style.display = "flex";
      label.style.alignItems = "center";
      label.style.gap = "6px";
      label.textContent = "Kanaal " + (i + 1) + " ";
      const input = document.createElement("input");
      input.type = "color";
      input.value = channelColors[i];
      input.dataset.ch = i;
      input.style.width = "48px";
      input.style.height = "36px";
      input.style.padding = "2px";
      input.style.cursor = "pointer";
      label.appendChild(input);
      el.colorPickers.appendChild(label);
    }
  }

  function setStatus(target, text, kind = "") {
    target.textContent = text;
    target.className = "status" + (kind ? " " + kind : "");
  }

  let colorsLoaded = false;
  function renderState(s) {
    if (s.ssid) el.ssid.value = s.ssid;
    if (typeof s.otaPassword === "string") el.otaPassword.value = s.otaPassword;
    el.mqttEnabled.checked = !!s.mqttEnabled;
    if (s.mqttBroker)   el.mqttBroker.value = s.mqttBroker;
    if (s.mqttPort)     el.mqttPort.value   = s.mqttPort;
    if (s.mqttUsername) el.mqttUser.value   = s.mqttUsername;
    if (s.timezone) {
      const opts = el.timezone.options;
      let found = false;
      for (let i = 0; i < opts.length; i++) {
        if (opts[i].value === s.timezone) { el.timezone.selectedIndex = i; found = true; break; }
      }
      if (!found) el.timezone.selectedIndex = 0;
    }
    if (!colorsLoaded && Array.isArray(s.channelColors) && s.channelColors.length === 5) {
      colorsLoaded = true;
      channelColors = [...s.channelColors];
      const inputs = el.colorPickers.querySelectorAll("input[type=color]");
      inputs.forEach((inp, i) => { inp.value = channelColors[i]; });
    }
    const hh = Math.floor((s.nowMinute || 0) / 60);
    const mm = Math.floor((s.nowMinute || 0) % 60);
    const moonLine = (() => {
      if (!s.moonlightEnabled || s.moonlightChannel < 0) return "maanlicht: uitgeschakeld";
      const ch = "kanaal " + (s.moonlightChannel + 1);
      const pct = Math.round((s.moonPhase || 0) * 100);
      const out = Math.round((s.moonlightIntensity || 0) * (s.moonPhase || 0));
      const e = pct>=95?"🌕":pct>=70?"🌔":pct>=40?"🌓":pct>=10?"🌒":"🌑";
      return "maanlicht: AAN op " + ch + " — " + e + " " + pct + "% vol → vanavond " + out + "/255\n" +
             "           (overdag volgt preset; 's nachts: max " + (s.moonlightIntensity||0) + " × maanfase)";
    })();
    el.live.textContent =
      "wifiConnected: " + (!!s.wifiConnected) + "\n" +
      "ssid: " + (s.ssid || "-") + "\n" +
      "stationIp: " + (s.stationIp || "0.0.0.0") + "\n" +
      "apMode: " + (!!s.apMode) + "\n" +
      "apIp: " + (s.apIp || "0.0.0.0") + "\n" +
      "otaPassword: " + ((s.otaPassword || "").length ? "ingesteld" : "leeg") + "\n" +
      "mqtt: " + (s.mqttEnabled ? (s.mqttConnected ? "verbonden (" + (s.mqttBroker || "-") + ")" : "ingeschakeld (niet verbonden)") : "uitgeschakeld") + "\n" +
      "mqttDeviceId: " + (s.mqttDeviceId || "-") + "\n" +
      "ntpSynced: " + (!!s.ntpSynced) + "\n" +
      "manualTime: " + (!!s.manualTime) + "\n" +
      "tijd: " + String(hh).padStart(2, "0") + ":" + String(mm).padStart(2, "0") + "\n" +
      "uptime: " + (()=>{ const u=s.uptimeSec||0; const uh=Math.floor(u/3600); const um=Math.floor((u%3600)/60); const us=u%60; return String(uh).padStart(2,"0")+":"+String(um).padStart(2,"0")+":"+String(us).padStart(2,"0"); })() + "\n" +
      moonLine;
    if (s.version) document.getElementById("versionTag").textContent = s.version;
    // Maanlicht
    if (typeof s.moonlightEnabled === "boolean") el.moonlightEnabled.checked = s.moonlightEnabled;
    if (typeof s.moonlightChannel === "number") el.moonlightChannel.value = String(s.moonlightChannel);
    if (typeof s.moonlightIntensity === "number") {
      el.moonlightIntensity.value = s.moonlightIntensity;
      el.moonlightIntensityVal.textContent = s.moonlightIntensity;
    }
  }

  async function refresh() {
    const s = await Api.state();
    renderState(s);
    setStatus(el.wifiStatus, s.wifiConnected ? "Wifi verbonden" : "Wifi niet verbonden", s.wifiConnected ? "ok" : "");
    setStatus(el.timeStatus, s.ntpSynced ? "NTP tijd actief" : (s.manualTime ? "Handmatige tijd actief" : "Fallback tijd (uptime)"), s.ntpSynced || s.manualTime ? "ok" : "");
    setStatus(el.mqttStatus,
      !s.mqttEnabled  ? "MQTT uitgeschakeld" :
      s.mqttConnected ? "Verbonden met " + (s.mqttBroker || "-") + " (id: " + (s.mqttDeviceId || "-") + ")" :
                        "MQTT ingeschakeld, niet verbonden",
      s.mqttEnabled && !s.mqttConnected ? "err" : (s.mqttConnected ? "ok" : ""));
  }

  async function saveMoonlight() {
    try {
      const out = await Api.saveMoonlight({
        enabled:   el.moonlightEnabled.checked,
        channel:   Number(el.moonlightChannel.value),
        intensity: Number(el.moonlightIntensity.value)
      });
      setStatus(el.moonlightStatus, out.ok ? "Maanlicht opgeslagen" : "Opslaan mislukt", out.ok ? "ok" : "err");
    } catch (e) {
      setStatus(el.moonlightStatus, "Opslaan mislukt: " + e.message, "err");
    }
  }

  async function saveMqtt() {
    try {
      const out = await Api.saveMqtt({
        enabled:  el.mqttEnabled.checked,
        broker:   el.mqttBroker.value.trim(),
        port:     Number(el.mqttPort.value) || 1883,
        username: el.mqttUser.value.trim(),
        password: el.mqttPass.value
      });
      setStatus(el.mqttStatus, out.ok ? "MQTT instellingen opgeslagen" : "Opslaan mislukt", out.ok ? "ok" : "err");
      await refresh();
    } catch (e) {
      setStatus(el.mqttStatus, "Opslaan mislukt: " + e.message, "err");
    }
  }

  async function saveWifi() {
    try {
      const out = await Api.saveWifi({
        ssid: el.ssid.value.trim(),
        password: el.password.value,
        otaPassword: el.otaPassword.value,
        timezone: el.timezone.value
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

  async function saveColors() {
    try {
      const inputs = el.colorPickers.querySelectorAll("input[type=color]");
      const colors = Array.from(inputs).map(inp => inp.value);
      await Api.saveColors({ channelColors: colors });
      channelColors = [...colors];
      colorsLoaded = false;
      setStatus(el.colorStatus, "Kleuren opgeslagen", "ok");
    } catch (e) {
      setStatus(el.colorStatus, "Opslaan mislukt: " + e.message, "err");
    }
  }

  function bind() {
    el.btnWifiSave.onclick = saveWifi;
    el.btnTimeSet.onclick  = setTime;
    el.btnColorSave.onclick = saveColors;
    el.btnMqttSave.onclick  = saveMqtt;
    el.btnMoonlightSave.onclick = saveMoonlight;
    el.moonlightIntensity.oninput = () => { el.moonlightIntensityVal.textContent = el.moonlightIntensity.value; };
    el.btnOtaUpload.onclick = () => {
      const file = el.firmwareFile.files[0];
      if (!file) { setStatus(el.otaStatus, "Kies eerst een bestand", "err"); return; }
      el.otaProgress.style.display = "";
      el.otaProgress.value = 0;
      setStatus(el.otaStatus, "Uploaden...", "");
      const xhr = new XMLHttpRequest();
      xhr.open("POST", "/api/ota/upload");
      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) el.otaProgress.value = Math.round(e.loaded / e.total * 100);
      };
      xhr.onload = () => {
        if (xhr.status === 200) {
          setStatus(el.otaStatus, "Update succesvol! Herstart...", "ok");
        } else {
          el.otaProgress.style.display = "none";
          setStatus(el.otaStatus, "Update mislukt", "err");
        }
      };
      xhr.onerror = () => {
        el.otaProgress.style.display = "none";
        setStatus(el.otaStatus, "Upload fout", "err");
      };
      const formData = new FormData();
      formData.append("firmware", file);
      xhr.send(formData);
    };
    el.timezone.onchange = async () => {
      try {
        await Api.saveWifi({
          ssid: el.ssid.value.trim(),
          password: el.password.value,
          otaPassword: el.otaPassword.value,
          timezone: el.timezone.value
        });
        setStatus(el.tzStatus, "Tijdzone opgeslagen", "ok");
      } catch (e) {
        setStatus(el.tzStatus, "Opslaan mislukt: " + e.message, "err");
      }
    };
  }

  async function boot() {
    buildColorPickers();
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
