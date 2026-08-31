/*
 * SparkMiner - Embedded Dashboard HTML
 * Self-contained HTML/CSS/JS for the live mining dashboard.
 * Polls /api/stats every 1s and updates the UI.
 * GPL v3 License
 */
#ifndef DASHBOARD_HTML_H
#define DASHBOARD_HTML_H

static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SparkMiner Live Dashboard</title>
<style>
:root{
  --bg:#0b0e14; --card:#151a24; --card2:#1c2331; --border:#242c3b;
  --text:#e6eaf2; --muted:#8b96a8; --accent:#f7931a; --ok:#2ecc71;
  --warn:#e67e22; --err:#e74c3c;
}
*{box-sizing:border-box;margin:0;padding:0}
body{
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  background:var(--bg);color:var(--text);min-height:100vh;padding:20px;
}
header{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:12px;margin-bottom:20px}
h1{font-size:1.5rem;font-weight:700;letter-spacing:.3px}
h1 .dot{color:var(--accent)}
.status-badge{padding:6px 14px;border-radius:20px;font-size:.85rem;font-weight:600}
.status-badge.on{background:rgba(46,204,113,.15);color:var(--ok);border:1px solid var(--ok)}
.status-badge.off{background:rgba(231,76,60,.15);color:var(--err);border:1px solid var(--err)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:14px;margin-bottom:20px}
.card{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:18px}
.card h3{font-size:.72rem;text-transform:uppercase;letter-spacing:1px;color:var(--muted);margin-bottom:10px}
.big{font-size:1.85rem;font-weight:700;line-height:1.1}
.big .unit{font-size:.9rem;color:var(--muted);font-weight:500;margin-left:4px}
.row{display:flex;justify-content:space-between;font-size:.92rem;padding:3px 0;border-bottom:1px solid #1a2130}
.row:last-child{border-bottom:none}
.row .k{color:var(--muted)}
.row .v{font-weight:600}
.small{font-size:.8rem;color:var(--muted);margin-top:6px}
.wallet{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:.72rem;word-break:break-all;color:var(--muted)}
.footer{text-align:center;color:var(--muted);font-size:.75rem;margin-top:24px;opacity:.7}
@media(max-width:520px){
  .grid{grid-template-columns:1fr 1fr}
  h1{font-size:1.2rem}
}
</style>
</head>
<body>
<header>
  <h1>SparkMiner <span class="dot">●</span> Live</h1>
  <span id="statusBadge" class="status-badge off">verbinde…</span>
</header>

<div class="grid">
  <div class="card">
    <h3>Aktuelle Hashrate</h3>
    <div class="big" id="hashrate">0 H/s</div>
    <div class="small" id="uptime">Uptime: –</div>
  </div>
  <div class="card">
    <h3>Shares</h3>
    <div class="row"><span class="k">Gesendet</span><span class="v" id="shares">0</span></div>
    <div class="row"><span class="k" style="color:var(--ok)">✓ Akzeptiert</span><span class="v" id="accepted">0</span></div>
    <div class="row"><span class="k" style="color:var(--err)">✗ Abgelehnt</span><span class="v" id="rejected">0</span></div>
  </div>
  <div class="card">
    <h3>Pool</h3>
    <div class="row"><span class="k">Status</span><span class="v" id="poolStatus">–</span></div>
    <div class="row"><span class="k">Pool</span><span class="v" id="poolName">–</span></div>
    <div class="row"><span class="k">Latenz</span><span class="v" id="latency">–</span></div>
    <div class="small" id="poolFailovers"></div>
  </div>
  <div class="card">
    <h3>Mining</h3>
    <div class="row"><span class="k">Blocks gefunden</span><span class="v" id="blocks">0</span></div>
    <div class="row"><span class="k">Best Difficulty</span><span class="v" id="bestDiff">–</span></div>
    <div class="row"><span class="k">Templates</span><span class="v" id="templates">–</span></div>
    <div class="row"><span class="k">Pool-Difficulty</span><span class="v" id="poolDiff">–</span></div>
  </div>
  <div class="card">
    <h3>Netzwerk</h3>
    <div class="row"><span class="k">Blockhöhe</span><span class="v" id="blockHeight">–</span></div>
    <div class="row"><span class="k">Netz-Hashrate</span><span class="v" id="netHashrate">–</span></div>
    <div class="row"><span class="k">Difficulty</span><span class="v" id="netDiff">–</span></div>
    <div class="row"><span class="k">BTC-Preis</span><span class="v" id="btcPrice">–</span></div>
  </div>
  <div class="card">
    <h3>System</h3>
    <div class="row"><span class="k">WiFi-Signal</span><span class="v" id="wifiRssi">–</span></div>
    <div class="row"><span class="k">IP</span><span class="v" id="ip">–</span></div>
    <div class="row"><span class="k">Freier Heap</span><span class="v" id="heap">–</span></div>
  </div>
</div>

<div class="card">
  <h3>Wallet-Adresse</h3>
  <div class="wallet" id="wallet">–</div>
</div>

<div class="card">
  <h3>WiFi-Netzwerke</h3>
  <div id="wifiStatus" class="small" style="margin-top:0;margin-bottom:8px">lädt…</div>
  <div id="wifiList" style="margin-bottom:10px"></div>
  <div style="display:flex;gap:8px;flex-wrap:wrap">
    <input id="newSsid" placeholder="SSID" style="flex:2;min-width:120px;background:var(--card2);border:1px solid var(--border);color:var(--text);border-radius:8px;padding:8px 10px">
    <input id="newPwd" type="password" placeholder="Passwort" style="flex:2;min-width:120px;background:var(--card2);border:1px solid var(--border);color:var(--text);border-radius:8px;padding:8px 10px">
    <button onclick="addWifi()" style="background:var(--accent);color:#0b0e14;border:none;border-radius:8px;padding:8px 14px;font-weight:700;cursor:pointer">Hinzufügen</button>
  </div>
  <div class="small">Bis zu 5 Netze. Im Fallback-AP (192.168.4.1) hier ein Heimnetz hinzufügen.</div>
</div>

<div class="footer">SparkMiner · ESP32 Solo Mining · aktualisiert <span id="lastUpdate">nie</span></div>

<script>
const $=id=>document.getElementById(id);
const fmtNum=n=>Number(n).toLocaleString('de-DE');
function fmtHR(h){
  if(h>=1e9)return (h/1e9).toFixed(2)+' GH/s';
  if(h>=1e6)return (h/1e6).toFixed(2)+' MH/s';
  if(h>=1e3)return (h/1e3).toFixed(1)+' KH/s';
  return Math.round(h)+' H/s';
}
function fmtDuration(sec){
  sec=Math.floor(sec);
  const d=Math.floor(sec/86400),h=Math.floor(sec%86400/3600),m=Math.floor(sec%3600/60),s=sec%60;
  let out='';
  if(d>0)out+=d+'d ';
  if(h>0||d>0)out+=h+'h ';
  if(m>0||h>0||d>0)out+=m+'m ';
  out+=s+'s';
  return out;
}
async function refresh(){
  try{
    const r=await fetch('/api/stats');
    const d=await r.json();
    $('hashrate').textContent=fmtHR(d.hashRate);
    $('shares').textContent=fmtNum(d.shares);
    $('accepted').textContent=fmtNum(d.accepted);
    $('rejected').textContent=fmtNum(d.rejected);
    $('blocks').textContent=fmtNum(d.blocks);
    $('bestDiff').textContent=d.bestDifficulty!==0?(d.bestDifficulty>1e6?(d.bestDifficulty/1e6).toFixed(2)+'M':d.bestDifficulty.toFixed(2)):'–';
    $('templates').textContent=fmtNum(d.templates);
    $('poolDiff').textContent=d.poolDifficulty!==0?d.poolDifficulty.toFixed(4):'–';
    $('blockHeight').textContent=d.blockHeight?fmtNum(d.blockHeight):'–';
    $('netHashrate').textContent=d.networkHashrate||'–';
    $('netDiff').textContent=d.networkDifficulty||'–';
    $('btcPrice').textContent=d.btcPriceUsd?'$ '+d.btcPriceUsd.toLocaleString('de-DE'):'–';
    $('poolStatus').textContent=d.poolConnected?'Verbunden':'Getrennt';
    $('poolName').textContent=d.poolName||'–';
    $('latency').textContent=d.avgLatency?(d.avgLatency+' ms'):'–';
    $('poolFailovers').textContent=d.failovers&&d.failovers>0?('Failovers: '+d.failovers):'';
    $('uptime').textContent='Uptime: '+(d.uptimeSeconds?fmtDuration(d.uptimeSeconds):'–');
    $('wifiRssi').textContent=d.rssi+' dBm';
    $('ip').textContent=d.ip||'–';
    $('heap').textContent=fmtNum(d.freeHeap)+' KB';
    $('wallet').textContent=d.wallet||'–';
    const badge=$('statusBadge');
    if(d.poolConnected){badge.textContent='● Mining aktiv';badge.className='status-badge on';}
    else if(d.wifiConnected){badge.textContent='● Verbunden (kein Pool)';badge.className='status-badge off';}
    else{badge.textContent='● Kein WiFi';badge.className='status-badge off';}
    $('lastUpdate').textContent=new Date().toLocaleTimeString('de-DE');
  }catch(e){
    $('statusBadge').textContent='● Warte auf Gerät';
    $('statusBadge').className='status-badge off';
  }
}
refresh();
setInterval(refresh,1000);

async function loadWifi(){
  try{
    const r=await fetch('/api/wifi');
    const d=await r.json();
    const list=$('wifiList');
    list.innerHTML='';
    (d.networks||[]).forEach(n=>{
      const row=document.createElement('div');
      row.style.cssText='display:flex;justify-content:space-between;align-items:center;gap:8px;padding:7px 0;border-bottom:1px solid #1a2130';
      const name=document.createElement('span');
      name.textContent=n.ssid+(n.active?' (aktiv)':'');
      name.style.fontWeight=n.active?'700':'400';
      const del=document.createElement('button');
      del.textContent='✕';
      del.title='Entfernen';
      del.style.cssText='background:none;border:1px solid var(--border);color:var(--err);border-radius:6px;padding:3px 8px;cursor:pointer';
      del.onclick=async()=>{
        await fetch('/api/wifi?ssid='+encodeURIComponent(n.ssid),{method:'DELETE'});
        loadWifi();
      };
      row.appendChild(name);
      row.appendChild(del);
      list.appendChild(row);
    });
    if(!d.networks||d.networks.length===0)list.textContent='Keine Netze gespeichert.';
    $('wifiStatus').textContent=(d.ap_mode?'⚠ Fallback-AP aktiv (192.168.4.1) · ':'')+
      (d.connected?'Verbunden mit '+d.current_ssid:'Nicht verbunden')+' · IP: '+(d.ip||'–');
  }catch(e){
    $('wifiStatus').textContent='WiFi-Liste nicht verfügbar';
  }
}
async function addWifi(){
  const ssid=$('newSsid').value.trim();
  const pwd=$('newPwd').value;
  if(!ssid){alert('Bitte SSID eingeben');return;}
  const r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:pwd})});
  $('newSsid').value='';$('newPwd').value='';
  loadWifi();
  if(r.status===400){const t=await r.text();alert(t);}
}
loadWifi();
</script>
</body>
</html>
)rawliteral";

#endif // DASHBOARD_HTML_H
