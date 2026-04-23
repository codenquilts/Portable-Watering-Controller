#pragma once
#include <Arduino.h>

// ui_index_html.h — clean UI for:
// - Status + schedules + pump + tank reset
// - Last watering (RAM log)
// - Device Settings (device name + AP SSID/pass)
// - WiFiManager controls (setup mode + reset)
//
// Requires these API routes in web.cpp:
// GET  /api/status
// POST /api/schedule/morning
// POST /api/schedule/evening
// POST /api/pump
// POST /api/runNow
// POST /api/tankReset
// POST /api/email/test
// POST /api/device
// POST /api/wifi/setup
// POST /api/wifi/reset

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Portable Watering Control</title>
<style>
:root{
  --blue:#0078d7;--green:#28a745;--red:#dc3545;--yellow:#ffc107;--gray:#f2f2f2;
  --text:#222;--muted:#6b7280;
}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;margin:0;background:var(--gray);color:var(--text);}
h2{background:var(--blue);color:#fff;margin:0;padding:1rem;font-size:1.2rem;text-align:center;}
.card{background:#fff;margin:1rem;padding:1rem;border-radius:.7rem;box-shadow:0 2px 6px rgba(0,0,0,.1);}
.status{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:center;font-size:1rem;gap:.4rem;}
label{display:block;margin-top:.6rem;font-size:.95rem;}
input[type=text],input[type=number],input[type=password],input[type=email]{
  width:100%;box-sizing:border-box;padding:.45rem;margin-top:.2rem;font-size:1rem;
}
input[type=checkbox]{transform:scale(1.4);margin-right:.4rem;}
button{
  width:48%;margin-top:.8rem;padding:.6rem;border:none;border-radius:.4rem;
  font-size:1rem;color:#fff;cursor:pointer;
}
.btn-save{background:var(--blue);} .btn-run{background:var(--yellow);color:#000;}
.btn-on{background:var(--green);} .btn-off{background:var(--red);}
.btn-refresh{background:var(--blue);width:100%;margin-top:.6rem;}
.buttons{display:flex;justify-content:space-between;gap:.4rem;flex-wrap:wrap;}
fieldset{border:none;padding:0;margin:0;} legend{font-weight:700;margin-bottom:.4rem;}
.small{font-size:.85rem;opacity:.9;}
.note{font-size:.92rem;line-height:1.35;color:var(--muted);margin:.35rem 0 .6rem 0;}
.kbd{
  display:inline-block;padding:.06rem .35rem;border:1px solid #cfcfcf;border-bottom-width:2px;
  border-radius:.35rem;background:#fafafa;font-family:ui-monospace, SFMono-Regular, Menlo, monospace;font-size:.85em;color:#111;
}
a.link{color:#fff;text-decoration:underline;}
a.linkDark{color:var(--blue);text-decoration:none;font-weight:600;}
a.linkDark:hover{text-decoration:underline;}
#toast{
  position:fixed;bottom:1rem;left:50%;transform:translateX(-50%);
  background:var(--blue);color:#fff;padding:.6rem 1rem;border-radius:.5rem;font-size:.9rem;
  opacity:0;transition:opacity .35s;z-index:9999;max-width:92vw;text-align:center;
}
@media(min-width:600px){.card{max-width:560px;margin:1rem auto;}}
hr.sep{border:none;border-top:1px solid #eee;margin:.8rem 0;}
.row2{display:flex;gap:.6rem;flex-wrap:wrap;}
.row2 > div{flex:1 1 240px;}
.pill{display:inline-block;background:#f3f4f6;border:1px solid #e5e7eb;border-radius:999px;padding:.15rem .5rem;font-size:.85rem;color:#111;}
.days{display:grid;grid-template-columns:repeat(7,minmax(0,1fr));gap:.35rem;margin-top:.45rem;}
.day{display:flex;align-items:center;justify-content:center;padding:.45rem .2rem;border:1px solid #d1d5db;border-radius:.45rem;background:#fafafa;font-size:.86rem;}
.day input{margin:0 .25rem 0 0;transform:scale(1.1);}
</style>
</head>
<body>
<h2>
  Portable Watering <br>
  <span id="sysName" class="small">Location: ...</span><br>
  <span id="netMode" class="small">Network: ...</span><br>
  <span id="fwver" class="small">FW: ...</span>
</h2>

<div class="card">
  <div class="status">
    <span><b>Voltage:</b> <span id="voltage">--</span> V</span>
    <span><b>Tank:</b> <span id="tanklevel">--</span> mL</span>
    <span><b>Time:</b> <span id="espTime">--:--</span></span>
  </div>

  <div class="note" style="margin-top:.6rem">
    <b>Last watering:</b>
    <span id="lastWatering" class="pill">—</span>
  </div>

  <button class="btn-refresh" onclick="loadAll()">Refresh</button>
</div>

<div class="card">
  <fieldset>
    <legend>Morning Schedule</legend>
    <label>Start Time (24 hr)
      <input type="text" id="m_start" inputmode="numeric" placeholder="0630">
    </label>
    <label>Run Time (min)
      <input type="number" id="m_run" placeholder="5">
    </label>
    <label><input type="checkbox" id="m_enabled"> Enabled</label>
    <label>Days of Week</label>
    <div class="days" id="m_days">
      <label class="day"><input type="checkbox" data-day="0">Sun</label>
      <label class="day"><input type="checkbox" data-day="1">Mon</label>
      <label class="day"><input type="checkbox" data-day="2">Tue</label>
      <label class="day"><input type="checkbox" data-day="3">Wed</label>
      <label class="day"><input type="checkbox" data-day="4">Thu</label>
      <label class="day"><input type="checkbox" data-day="5">Fri</label>
      <label class="day"><input type="checkbox" data-day="6">Sat</label>
    </div>
    <div class="buttons">
      <button class="btn-save" onclick="saveSchedule('morning')">Save</button>
      <button class="btn-run" onclick="runNow('Morning')">Run Now</button>
    </div>
  </fieldset>
</div>

<div class="card">
  <fieldset>
    <legend>Evening Schedule</legend>
    <label>Start Time (24 hr)
      <input type="text" id="e_start" inputmode="numeric" placeholder="1830">
    </label>
    <label>Run Time (min)
      <input type="number" id="e_run" placeholder="5">
    </label>
    <label><input type="checkbox" id="e_enabled"> Enabled</label>
    <label>Days of Week</label>
    <div class="days" id="e_days">
      <label class="day"><input type="checkbox" data-day="0">Sun</label>
      <label class="day"><input type="checkbox" data-day="1">Mon</label>
      <label class="day"><input type="checkbox" data-day="2">Tue</label>
      <label class="day"><input type="checkbox" data-day="3">Wed</label>
      <label class="day"><input type="checkbox" data-day="4">Thu</label>
      <label class="day"><input type="checkbox" data-day="5">Fri</label>
      <label class="day"><input type="checkbox" data-day="6">Sat</label>
    </div>
    <div class="buttons">
      <button class="btn-save" onclick="saveSchedule('evening')">Save</button>
      <button class="btn-run" onclick="runNow('Evening')">Run Now</button>
    </div>
  </fieldset>
</div>

<div class="card">
  <fieldset>
    <legend>Pump Control</legend>
    <div class="buttons">
      <button id="pumpOn"  class="btn-on"  onclick="pump(1)">Pump ON</button>
      <button id="pumpOff" class="btn-off" onclick="pump(0)">Pump OFF</button>
    </div>
    <button class="btn-save" style="width:100%;margin-top:.8rem;" onclick="resetTank()">Reset Tank</button>
  </fieldset>
</div>

<div class="card">
  <fieldset>
    <legend>Device Settings</legend>

    <label>Device Name
      <input type="text" id="devName" placeholder="portable1">
    </label>

    <label>Setup Hotspot Name (AP SSID)
      <input type="text" id="apSsid" placeholder="Watering-Setup">
    </label>

    <label>Setup Hotspot Password (AP Pass)
      <input type="text" id="apPass" autocomplete="off" placeholder="(leave blank to keep current)">
    </label>

    <hr class="sep">

    <label>Time Zone (POSIX format)
      <input type="text" id="timeZone" placeholder="AEST-10AEDT,M10.1.0/2,M4.1.0/3">
    </label>

    <label>Tank Total Capacity (mL)
      <input type="number" id="tankTotal" min="1000" step="100" placeholder="55000">
    </label>

    <label>Return Flow Rate (mL/sec)
      <input type="number" id="returnFlow" min="0" step="0.1" placeholder="0">
    </label>
    <div class="note" id="actualFlowState"></div>

    <hr class="sep">

    <label>Notification Email
      <input type="email" id="notifyEmail" placeholder="you@example.com">
    </label>

    <label><input type="checkbox" id="notifyLowTank"> Low tank alerts</label>
    <label><input type="checkbox" id="notifyErrors"> Error alerts</label>
    <label><input type="checkbox" id="notifyStatus"> Status alerts</label>

    <hr class="sep">

    <div class="row2">
      <div>
        <label>MQTT Host
          <input type="text" id="mqttHost" placeholder="192.168.0.17">
        </label>
      </div>
      <div>
        <label>MQTT Port
          <input type="number" id="mqttPort" min="1" max="65535" placeholder="1883">
        </label>
      </div>
    </div>

    <label>MQTT Username
      <input type="text" id="mqttUser" autocomplete="username">
    </label>

    <label>MQTT Password
      <input type="password" id="mqttPass" autocomplete="current-password" placeholder="(leave blank to keep current)">
    </label>
    <div class="note" id="mqttPassState"></div>

    <hr class="sep">

    <div class="row2">
      <div>
        <label>SMTP Host
          <input type="text" id="smtpHost" placeholder="smtp.example.com">
        </label>
      </div>
      <div>
        <label>SMTP Port
          <input type="number" id="smtpPort" min="1" max="65535" placeholder="587">
        </label>
      </div>
    </div>

    <label>SMTP Username
      <input type="text" id="smtpUser" autocomplete="username">
    </label>

    <label>SMTP Password
      <input type="password" id="smtpPass" autocomplete="current-password" placeholder="(leave blank to keep current)">
    </label>
    <div class="note" id="smtpPassState"></div>

    <label>SMTP From Address
      <input type="email" id="smtpFrom" placeholder="pwb@example.com">
    </label>

    <label><input type="checkbox" id="smtpSsl"> Use SSL/TLS socket</label>

    <div class="buttons">
      <button class="btn-save" onclick="saveDevice()">Save</button>
      <button class="btn-run" onclick="testEmail()">Test Email</button>
    </div>
  </fieldset>
</div>

<div class="card">
  <fieldset>
    <legend>Wi-Fi Setup</legend>

    <div id="wifiConfig" style="display:none">
      <div class="note">
        <b>Built-in Wi-Fi Manager:</b> Scan for networks and connect directly.
      </div>

      <button class="btn-run" onclick="scanWifi()">Scan Wi-Fi Networks</button>

      <div id="scanResults" style="margin-top:.6rem"></div>

      <label>SSID
        <input type="text" id="wifiSsid">
      </label>

      <label>Password
        <input type="password" id="wifiPass">
      </label>

      <button class="btn-save" onclick="connectWifi()">Connect</button>

      <hr class="sep">
    </div>

    <div class="note">
      <b>Start Wi-Fi Setup</b> reboots into WiFiManager portal mode.
      Then connect to the device hotspot and open <span class="kbd">http://192.168.4.1/</span>.
      After saving Wi-Fi, the device reboots back into normal mode.
    </div>

    <div class="buttons">
      <button class="btn-run" style="width:100%" onclick="startWifiSetup()">Start Wi-Fi Setup (reboot)</button>
      <button class="btn-off" style="width:100%" onclick="resetWifi()">Reset Wi-Fi (forget)</button>
    </div>

    <div class="note" style="margin-top:.8rem">
      Portal address (when connected to device hotspot):
      <a class="linkDark" href="http://192.168.4.1/" target="_blank" rel="noopener">192.168.4.1</a>
    </div>
  </fieldset>
</div>

<div id="toast"></div>

<script>
function showToast(msg){
  const t=document.getElementById("toast");
  t.textContent=msg; t.style.opacity=1;
  setTimeout(()=>t.style.opacity=0,2200);
}

function digits4(s){
  s=(s||"").replace(/\D/g,"").slice(0,4);
  if(s.length===3) s="0"+s;
  return s.padStart(4,"0");
}
function clampRun(n){
  n=parseInt(n||"0",10);
  if(n>15){showToast("Max run 15 min"); return 15;}
  if(n<1){return 1;}
  return n;
}
function normalizeDaysMask(mask){
  return Number(mask ?? 127) & 127;
}
function setDays(prefix, days){
  const mask = Array.isArray(days)
    ? days.reduce((acc, on, idx)=>acc | ((on ? 1 : 0) << idx), 0)
    : normalizeDaysMask(days);
  document.querySelectorAll(`#${prefix}_days input[data-day]`).forEach(el=>{
    const bit = Number(el.dataset.day);
    el.checked = !!(mask & (1 << bit));
  });
}
function getDaysMask(prefix){
  let mask = 0;
  document.querySelectorAll(`#${prefix}_days input[data-day]`).forEach(el=>{
    const bit = Number(el.dataset.day);
    if(el.checked) mask |= (1 << bit);
  });
  return normalizeDaysMask(mask);
}

async function apiGet(url){
  const r=await fetch(url+"?_="+Date.now());
  if(!r.ok) throw new Error("GET "+url+" "+r.status);
  return await r.json();
}
async function apiPost(url,obj){
  const r=await fetch(url,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(obj||{})});
  if(!r.ok) throw new Error("POST "+url+" "+r.status);
  return await r.json();
}

function fmtTs(ts){
  // ts expected as unix seconds; if 0/undefined => —
  ts = Number(ts||0);
  if(!ts) return "—";
  const d = new Date(ts*1000);
  return d.toLocaleString();
}

// Helper to set input value only if not currently focused
function setIfNotFocused(elem, value) {
  if (document.activeElement !== elem) {
    elem.value = value;
  }
}

// Helper to set checkbox only if not currently focused
function setCheckIfNotFocused(elem, value) {
  if (document.activeElement !== elem) {
    elem.checked = value;
  }
}
async function loadAll(){
  try{
    const s=await apiGet("/api/status");

    voltage.textContent=(s.voltage_v ?? 0).toFixed(2);
    tanklevel.textContent=Math.round(s.tank_ml ?? 0);
    espTime.textContent=s.time_hhmm ?? "--:--";
    sysName.textContent="Location: "+(s.device_name||"");
    fwver.textContent = (s.app_name ? (s.app_name + " v") : "FW: ") + (s.fw_version || "?");
    const ip = (s.ip || "");
    const nm = (s.net_mode || "");
    const url = ip ? ("http://" + ip + "/") : "";
    netMode.innerHTML = "Network: " + nm + (ip ? (" @ <a class='link' href='"+url+"'>"+ip+"</a>") : "");

    setIfNotFocused(m_start, (s.morning?.start_hhmm ?? 630).toString().padStart(4,"0"));
    setIfNotFocused(m_run, (s.morning?.run_min ?? 5));
    m_enabled.checked=!!(s.morning?.enabled);
    setDays("m", s.morning?.days ?? s.morning?.days_mask ?? 127);

    setIfNotFocused(e_start, (s.evening?.start_hhmm ?? 1830).toString().padStart(4,"0"));
    setIfNotFocused(e_run, (s.evening?.run_min ?? 5));
    setCheckIfNotFocused(e_enabled, !!(s.evening?.enabled));
    setDays("e", s.evening?.days ?? s.evening?.days_mask ?? 127);

    setIfNotFocused(devName, (s.device_name || ""));
    setIfNotFocused(apSsid, (s.ap_ssid || ""));
    setIfNotFocused(timeZone, (s.time_zone || "AEST-10AEDT,M10.1.0/2,M4.1.0/3"));
    setIfNotFocused(tankTotal, (s.tank_total_ml || 55000));
    setIfNotFocused(returnFlow, (s.return_flow_ml_per_sec ?? 0));
    actualFlowState.textContent = "Pump max: " + Number(s.flow_ml_per_sec ?? 0).toFixed(1) + " mL/sec. Actual watering flow: " + Number(s.actual_flow_ml_per_sec ?? 0).toFixed(1) + " mL/sec.";
    setIfNotFocused(notifyEmail, (s.notify_email || ""));
    setCheckIfNotFocused(notifyLowTank, !!s.notify_low_tank);
    setCheckIfNotFocused(notifyErrors, !!s.notify_errors);
    setCheckIfNotFocused(notifyStatus, !!s.notify_status);

    setIfNotFocused(mqttHost, (s.mqtt_host || ""));
    setIfNotFocused(mqttPort, (s.mqtt_port || 1883));
    setIfNotFocused(mqttUser, (s.mqtt_user || ""));
    mqttPass.value = "";
    mqttPassState.textContent = s.mqtt_pass_set ? "MQTT password is configured." : "No MQTT password saved.";

    setIfNotFocused(smtpHost, (s.smtp_host || ""));
    setIfNotFocused(smtpPort, (s.smtp_port || 587));
    setIfNotFocused(smtpUser, (s.smtp_user || ""));
    smtpPass.value = "";
    setIfNotFocused(smtpFrom, (s.smtp_from || ""));
    setCheckIfNotFocused(smtpSsl, !!s.smtp_ssl);
    smtpPassState.textContent = s.smtp_pass_set ? "SMTP password is configured." : "No SMTP password saved.";

    const on = !!s.pump_on;
    pumpOn.style.opacity  = on ? "1" : ".5";
    pumpOff.style.opacity = on ? ".5" : "1";

    // Last watering (RAM log)
    const start = fmtTs(s.last_start_ts);
    const runS  = Number(s.last_run_s || 0);
    const reason = (s.last_reason || "").toString().trim();
    if(start === "—") {
      lastWatering.textContent = "—";
    } else {
      const remaining = Number(s.pump_remaining_s || 0);
      const requested = Number(s.pump_requested_s || 0);
      const active = on && remaining ? (" - " + remaining + "s left") : "";
      const requestedText = requested ? (" / " + requested + "s requested") : "";
      lastWatering.textContent = start + (runS ? (" - " + runS + "s" + requestedText) : active) + (reason ? (" - " + reason) : "");
    }

  }catch(e){
    console.error(e);
    showToast("Status fetch failed");
  }
}

async function saveSchedule(which){
  try{
    if(which==="morning"){
      const start=digits4(m_start.value);
      const run=clampRun(m_run.value);
      const en=!!m_enabled.checked;
      const daysMask=getDaysMask("m");
      await apiPost("/api/schedule/morning",{start_hhmm:parseInt(start,10),run_min:run,enabled:en,days_mask:daysMask});
      showToast("Morning saved");
    } else {
      const start=digits4(e_start.value);
      const run=clampRun(e_run.value);
      const en=!!e_enabled.checked;
      const daysMask=getDaysMask("e");
      await apiPost("/api/schedule/evening",{start_hhmm:parseInt(start,10),run_min:run,enabled:en,days_mask:daysMask});
      showToast("Evening saved");
    }
    loadAll();
  }catch(e){
    console.error(e);
    showToast("Save failed");
  }
}

async function pump(state){
  try{
    await apiPost("/api/pump",{state:state});
    showToast(state?"Pump ON":"Pump OFF");
    setTimeout(loadAll,600);
  }catch(e){
    console.error(e);
    showToast("Pump command failed");
  }
}

async function runNow(which){
  try{
    const r = await apiPost("/api/runNow",{which:which});
    if(r && r.ok===false && r.err==="low_tank") showToast("Low tank - refused");
    else showToast(which+" started");
    setTimeout(loadAll,600);
  }catch(e){
    console.error(e);
    showToast("RunNow failed");
  }
}

async function resetTank(){
  if(!confirm("Reset tank to full?")) return;
  try{
    await apiPost("/api/tankReset",{});
    showToast("Tank reset");
    loadAll();
  }catch(e){
    console.error(e);
    showToast("Tank reset failed");
  }
}

async function saveDevice(){
  try{
    const payload = {
      device_name: (devName.value || "").trim(),
      ap_ssid:     (apSsid.value  || "").trim(),
      time_zone: (timeZone.value || "").trim(),
      tank_total_ml: parseFloat(tankTotal.value || 55000),
      return_flow_ml_per_sec: parseFloat(returnFlow.value || 0),
      notify_email: (notifyEmail.value || "").trim(),
      notify_low_tank: !!notifyLowTank.checked,
      notify_errors: !!notifyErrors.checked,
      notify_status: !!notifyStatus.checked,
      mqtt_host: (mqttHost.value || "").trim(),
      mqtt_port: parseInt(mqttPort.value || "1883", 10),
      mqtt_user: (mqttUser.value || "").trim(),
      smtp_host: (smtpHost.value || "").trim(),
      smtp_port: parseInt(smtpPort.value || "587", 10),
      smtp_user: (smtpUser.value || "").trim(),
      smtp_from: (smtpFrom.value || "").trim(),
      smtp_ssl: !!smtpSsl.checked
    };

    // Only send passwords if user typed one
    const ap = (apPass.value || "");
    if(ap.length > 0) payload.ap_pass = ap;
    const mp = (mqttPass.value || "");
    if(mp.length > 0) payload.mqtt_pass = mp;
    const sp = (smtpPass.value || "");
    if(sp.length > 0) payload.smtp_pass = sp;

    await apiPost("/api/device", payload);
    apPass.value = "";
    mqttPass.value = "";
    smtpPass.value = "";
    showToast("Device settings saved");
    loadAll();
    return true;
  }catch(e){
    console.error(e);
    showToast("Device save failed");
    return false;
  }
}

function emailTestMessage(err){
  if(err === "missing_notify_email") return "Enter notification email first";
  if(err === "missing_smtp_host") return "Enter SMTP host first";
  return "Email test failed";
}

async function testEmail(){
  try{
    if(!(await saveDevice())) return;
    showToast("Sending test email...");
    const r = await apiPost("/api/email/test", {});
    if(r && r.ok) showToast("Test email sent");
    else showToast(emailTestMessage(r && r.err));
  }catch(e){
    console.error(e);
    showToast("Email test failed");
  }
}

async function startWifiSetup(){
  showToast("Rebooting into Wi-Fi setup…");
  try{
    await apiPost("/api/wifi/setup", {});
  }catch(e){
    console.error(e);
    showToast("Setup start failed");
    return;
  }
  setTimeout(()=>{
    alert(
      "Wi-Fi Setup mode started.\n\n" +
      "1) Connect to the device hotspot (Setup AP)\n" +
      "2) Open: http://192.168.4.1/\n\n" +
      "After saving Wi-Fi, the device reboots into normal mode."
    );
  }, 250);
}

async function resetWifi(){
  if(!confirm("Forget saved Wi-Fi and reboot?")) return;
  showToast("Resetting Wi-Fi…");
  try{
    await apiPost("/api/wifi/reset", {});
  }catch(e){
    console.error(e);
    showToast("Reset failed");
  }
}

async function scanWifi(){
  showToast("Scanning Wi-Fi...");
  try{
    const r = await fetch("/api/wifi/scan");
    const data = await r.json();
    const results = document.getElementById("scanResults");
    results.innerHTML = "";

    const networks = Array.isArray(data.networks) ? data.networks : [];
    if(networks.length === 0){
      results.textContent = "No networks found";
      return;
    }

    networks.forEach(ssid => {
      const btn = document.createElement("button");
      btn.className = "btn-run";
      btn.style.width = "100%";
      btn.textContent = ssid;
      btn.onclick = () => {
        document.getElementById("wifiSsid").value = ssid;
      };
      results.appendChild(btn);
    });
  }catch(e){
    console.error(e);
    showToast("Scan failed");
  }
}

async function connectWifi(){
  const ssid = document.getElementById("wifiSsid").value.trim();
  const pass = document.getElementById("wifiPass").value;

  if(!ssid){
    showToast("Enter SSID");
    return;
  }

  showToast("Connecting...");
  try{
    const r = await apiPost("/api/wifi/connect", {ssid, pass});
    if(r && r.ok){
      showToast("Connected: " + r.ip);
      setTimeout(loadAll, 1000);
    }else{
      showToast("Connect failed");
    }
  }catch(e){
    console.error(e);
    showToast("Connect failed");
  }
}

setInterval(loadAll, 10000);
window.onload = () => { loadAll(); };
</script>
</body>
</html>
)HTML";
