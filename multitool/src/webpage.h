#pragma once
#include <Arduino.h>



static const char APP_HTML[] PROGMEM = R"HTML(<!doctype html><html lang=de><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>ESP Multitool</title><style>
:root{--bg:#0d1117;--card:#161b22;--line:#30363d;--fg:#e6edf3;--mut:#8b949e;--acc:#2f81f7;--bad:#f85149}
*{box-sizing:border-box}body{margin:0;font:15px system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:12px 16px;border-bottom:1px solid var(--line);display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap}
h1{font-size:17px;margin:0}#st{color:var(--mut);font-size:13px}
nav{display:flex;overflow-x:auto;border-bottom:1px solid var(--line)}
nav button{flex:none;background:none;border:0;border-bottom:2px solid transparent;color:var(--mut);padding:10px 14px;font:inherit;cursor:pointer}
nav button.on{color:var(--fg);border-color:var(--acc)}
main{max-width:720px;margin:0 auto;padding:16px}section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px;margin-bottom:14px}
.btn{background:#21262d;color:var(--fg);border:1px solid var(--line);border-radius:8px;padding:9px 14px;font:inherit;cursor:pointer;touch-action:manipulation}
.btn.p{background:var(--acc);border-color:var(--acc);color:#fff}.btn.d{color:var(--bad)}
canvas{width:100%;max-width:512px;image-rendering:pixelated;background:#000;border-radius:6px;display:block;margin:0 auto}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:360px;margin:14px auto 0}.pad .btn{padding:14px 6px}
.row{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:8px 0;border-bottom:1px solid var(--line);flex-wrap:wrap}.row:last-child{border:0}
.kv{display:grid;grid-template-columns:auto 1fr;gap:6px 14px}.kv b{color:var(--mut);font-weight:400}
input[type=text],input[type=password]{width:100%;padding:9px;border-radius:8px;border:1px solid var(--line);background:#0d1117;color:var(--fg);font:inherit;margin:4px 0 12px}
progress{width:100%;margin-top:10px}small{color:var(--mut)}a{color:var(--acc)}
</style></head><body>
<header><h1>ESP Multitool</h1><span id=st>…</span></header>
<nav id=tabs><button data-t=ctl class=on>Steuerung</button><button data-t=ir>IR</button><button data-t=nrf>NRF24</button><button data-t=fs>Dateien</button><button data-t=ota>Firmware</button><button data-t=net>WLAN</button></nav>
<main>
<section id=ctl class=on><div class=card><canvas id=scr width=128 height=64></canvas>
<div class=pad><span></span><button class=btn onclick="k('up')">&#9650; Hoch</button><span></span>
<button class=btn onclick="k('back')">&#8617; Zurück</button><button class="btn p" onclick="k('sel')">OK</button><span></span>
<span></span><button class=btn onclick="k('down')">&#9660; Runter</button><span></span></div></div>
<div class=card><div class=kv id=info></div></div></section>
<section id=ir><div class=card><div id=irl></div></div><div class=card><small>Neue Codes am Gerät aufnehmen: Menü → IR → Aufnehmen. Alle Codes stehen in <b>/ir_codes.txt</b> (Zeilenformat <code>Name|Protokoll|Bits|Daten</code>) – herunterladen, bearbeiten, wieder hochladen.</small></div></section>
<section id=nrf><div class=card><canvas id=spc width=126 height=64></canvas>
<p><button class="btn p" onclick="post('/api/nrfstart')">Scanner am Gerät starten</button></p><small id=nrfi></small></div></section>
<section id=fs><div class=card><input type=file id=uf multiple> <button class="btn p" onclick="up()">Hochladen</button><progress id=upg value=0 max=100 hidden></progress><p id=um></p><small id=uff></small></div><div class=card id=fl></div></section>
<section id=ota><div class=card><p>Nur die <b>firmware.bin</b> hochladen (nicht das Komplett-Image <i>esp-multitool-full.bin</i>).</p>
<input type=file id=of accept=".bin"> <button class="btn p" onclick="ota()">Flashen</button><progress id=opg value=0 max=100 hidden></progress><p id=om></p></div></section>
<section id=net><div class=card><div class=kv id=ni></div></div>
<div class=card><button class=btn onclick="scan()">Netze suchen</button><div id=nl></div>
<label>SSID<input type=text id=ss></label><label>Passwort<input type=password id=pw></label>
<button class="btn p" onclick="save()">Speichern &amp; neu starten</button><p id=m></p></div>
<div class=card><button class="btn d" onclick="wr()">WLAN zurücksetzen</button> <small>Löscht die Zugangsdaten → Gerät startet als Access Point mit QR-Code.</small></div></section>
</main><script>
const $=id=>document.getElementById(id),q=encodeURIComponent;
const esc=s=>String(s).replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';');
const post=(u,b)=>fetch(u,{method:'POST',body:b});
function pick(e){$('ss').value=e.dataset.s;$('pw').focus()}
async function scan(){$('nl').innerHTML='<small>Suche…</small>';for(let i=0;i<25;i++){const d=await (await fetch('/api/scan')).json();
if(d.nets){d.nets.sort((a,b)=>b.r-a.r);$('nl').innerHTML=d.nets.length?d.nets.map(n=>'<div class=row><span>'+esc(n.s)+'</span><button class=btn data-s="'+esc(n.s)+'" onclick="pick(this)">'+n.r+' dBm '+(n.o?'offen':'&#128274;')+'</button></div>').join(''):'<small>Keine Netze gefunden</small>';return}
await new Promise(r=>setTimeout(r,700))}$('nl').innerHTML='<small>Scan fehlgeschlagen</small>'}
function save(){if(!$('ss').value)return alert('SSID fehlt');post('/api/wifisave',new URLSearchParams({ssid:$('ss').value,pass:$('pw').value})).then(()=>{$('m').textContent='Gespeichert. Gerät startet neu und verbindet sich mit „'+$('ss').value+'“. Danach den QR-Code auf dem Display scannen.'})}

let tab='ctl';
document.querySelectorAll('#tabs button').forEach(b=>b.onclick=()=>{document.querySelectorAll('#tabs button,section').forEach(e=>e.classList.remove('on'));b.classList.add('on');tab=b.dataset.t;$(tab).classList.add('on');if(tab=='ir')irl();if(tab=='fs')fl();if(tab=='net')$('ni').innerHTML=$('info').innerHTML});
const k=b=>post('/api/btn?b='+b).then(()=>setTimeout(scr,120));
const fb=n=>n<1024?n+' B':n<1048576?(n/1024).toFixed(1)+' kB':(n/1048576).toFixed(2)+' MB';
const ut=s=>(s/3600|0)+'h '+(s/60%60|0)+'m '+s%60+'s';
async function stat(){try{const S=await (await fetch('/api/status')).json();$('st').textContent=(S.mode=='AP'?'AP · ':'')+S.ssid+' · '+S.ip;
$('info').innerHTML=[['Bildschirm',S.scr],['Modus',S.mode],['SSID',S.ssid],['IP',S.ip],['RSSI',S.rssi+' dBm'],['Uptime',ut(S.up)],['Freier RAM',fb(S.heap)],['LittleFS',fb(S.fsU)+' / '+fb(S.fsT)+' ('+fb(S.fsT-S.fsU)+' frei)'],['OLED',S.oled?'ok':'nicht erkannt'],['NRF24',S.nrf?'erkannt':'nicht erkannt'],['Firmware',S.fw]].map(r=>'<b>'+r[0]+'</b><span>'+esc(r[1])+'</span>').join('')}catch(e){$('st').textContent='offline'}}
async function scr(){const b=new Uint8Array(await (await fetch('/api/screen')).arrayBuffer());if(b.length<1024)return;const c=$('scr').getContext('2d'),im=c.createImageData(128,64),d=im.data;
for(let p=0;p<8;p++)for(let x=0;x<128;x++){const v=b[p*128+x];for(let i=0;i<8;i++){const y=p*8+i,o=(y*128+x)*4;if(v>>i&1){if(y<16){d[o]=255;d[o+1]=212;d[o+2]=0}else{d[o]=79;d[o+1]=195;d[o+2]=247}}d[o+3]=255}}c.putImageData(im,0,0)}
async function irl(){const l=await (await fetch('/api/irlist')).json();$('irl').innerHTML=l.length?l.map(n=>'<div class=row><span>'+esc(n)+'</span><span><button class="btn p" data-n="'+esc(n)+'" onclick="irs(this)">Senden</button> <button class=btn data-n="'+esc(n)+'" onclick="irr(this)">Umbenennen</button> <button class="btn d" data-n="'+esc(n)+'" onclick="ird(this)">&#10005;</button></span></div>').join(''):'<small>Noch keine Codes gespeichert.</small>'}
function irs(e){post('/api/irsend?name='+q(e.dataset.n))}
function ird(e){if(confirm('Löschen: '+e.dataset.n+'?'))post('/api/irdel?name='+q(e.dataset.n)).then(irl)}
function irr(e){const t=prompt('Neuer Name',e.dataset.n);if(t&&t!=e.dataset.n)post('/api/irren?name='+q(e.dataset.n)+'&to='+q(t)).then(irl)}
async function nrf(){const d=await (await fetch('/api/nrf')).json(),c=$('spc').getContext('2d');c.fillStyle='#000';c.fillRect(0,0,126,64);
let m=0;for(let i=0;i<126;i++){const h=d.lv[i],p=d.pk[i];c.fillStyle='#4fc3f7';c.fillRect(i,64-h,1,h);if(p){c.fillStyle='#ffd400';c.fillRect(i,63-p,1,1)}if(p>d.pk[m])m=i}
$('nrfi').textContent=d.on?'Scan läuft · stärkster Peak: Kanal '+m+' ('+(2400+m)+' MHz)':'Scanner am Gerät ist nicht aktiv.'}
async function fl(){fetch('/api/status').then(r=>r.json()).then(S=>{$('uff').textContent='Frei: '+fb(S.fsT-S.fsU)+' von '+fb(S.fsT)}).catch(()=>{});const l=await (await fetch('/api/files')).json();$('fl').innerHTML=l.length?l.map(f=>'<div class=row><a href="/api/dl?f='+q(f.n)+'">'+esc(f.n)+'</a><span><small>'+fb(f.s)+'</small> <button class="btn d" data-n="'+esc(f.n)+'" onclick="fd(this)">&#10005;</button></span></div>').join(''):'<small>Keine Dateien.</small>'}
function fd(e){if(confirm('Löschen: '+e.dataset.n+'?'))post('/api/fdel?f='+q(e.dataset.n)).then(fl)}
function xhr(u,file,bar,done){const f=new FormData();f.append('file',file,file.name);const x=new XMLHttpRequest();x.open('POST',u);bar.hidden=false;bar.value=0;
x.upload.onprogress=e=>{if(e.lengthComputable)bar.value=e.loaded*100/e.total};x.onload=()=>done(x.status,x.responseText);x.onerror=()=>done(0,'Verbindung abgebrochen');x.send(f)}
function up(){const fs=[...$('uf').files];if(!fs.length){$('um').textContent='Erst eine Datei auswählen.';return}let i=0,log=[];
const nx=(s,t)=>{if(t!==undefined){log.push((s==200?'✔ ':'✖ ')+t);$('um').innerHTML=log.map(esc).join('<br>')}
if(i>=fs.length){$('upg').hidden=true;$('uf').value='';fl();return}const f=fs[i++];$('um').innerHTML=log.map(esc).concat(['Lade '+esc(f.name)+' …']).join('<br>');xhr('/api/upload',f,$('upg'),nx)};nx()}
function ota(){const f=$('of').files[0];if(!f)return;if(f.size>0x1C0000)return alert('Datei zu groß – bitte firmware.bin nehmen');$('om').textContent='Lade hoch…';
xhr('/api/ota',f,$('opg'),(s,t)=>{$('om').textContent=t;if(s==200&&t.startsWith('OK'))setTimeout(()=>location.reload(),12000)})}
function wr(){if(confirm('WLAN-Zugangsdaten löschen?'))post('/api/wifireset').then(()=>alert('Gerät startet als Access Point „ESP-MT“ neu.'))}
stat();scr();setInterval(stat,2000);
setInterval(()=>{if(document.hidden)return;if(tab=='ctl')scr().catch(()=>{});if(tab=='nrf')nrf().catch(()=>{})},400);
</script></body></html>)HTML";

static const char PORTAL_HTML[] PROGMEM = R"HTML(<!doctype html><html lang=de><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>WLAN einrichten</title><style>
:root{--bg:#0d1117;--card:#161b22;--line:#30363d;--fg:#e6edf3;--mut:#8b949e;--acc:#2f81f7;--bad:#f85149}
*{box-sizing:border-box}body{margin:0;font:15px system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:12px 16px;border-bottom:1px solid var(--line);display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap}
h1{font-size:17px;margin:0}#st{color:var(--mut);font-size:13px}
nav{display:flex;overflow-x:auto;border-bottom:1px solid var(--line)}
nav button{flex:none;background:none;border:0;border-bottom:2px solid transparent;color:var(--mut);padding:10px 14px;font:inherit;cursor:pointer}
nav button.on{color:var(--fg);border-color:var(--acc)}
main{max-width:720px;margin:0 auto;padding:16px}section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px;margin-bottom:14px}
.btn{background:#21262d;color:var(--fg);border:1px solid var(--line);border-radius:8px;padding:9px 14px;font:inherit;cursor:pointer;touch-action:manipulation}
.btn.p{background:var(--acc);border-color:var(--acc);color:#fff}.btn.d{color:var(--bad)}
canvas{width:100%;max-width:512px;image-rendering:pixelated;background:#000;border-radius:6px;display:block;margin:0 auto}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:360px;margin:14px auto 0}.pad .btn{padding:14px 6px}
.row{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:8px 0;border-bottom:1px solid var(--line);flex-wrap:wrap}.row:last-child{border:0}
.kv{display:grid;grid-template-columns:auto 1fr;gap:6px 14px}.kv b{color:var(--mut);font-weight:400}
input[type=text],input[type=password]{width:100%;padding:9px;border-radius:8px;border:1px solid var(--line);background:#0d1117;color:var(--fg);font:inherit;margin:4px 0 12px}
progress{width:100%;margin-top:10px}small{color:var(--mut)}a{color:var(--acc)}
</style></head><body>
<header><h1>ESP Multitool · WLAN einrichten</h1></header><main>
<div class=card><button class=btn onclick="scan()">Netze suchen</button><div id=nl></div>
<label>SSID<input type=text id=ss></label><label>Passwort<input type=password id=pw></label>
<button class="btn p" onclick="save()">Speichern &amp; verbinden</button><p id=m></p></div>
<p><a href="/app">Zur vollen Oberfläche (Dateien, IR, OTA …) ohne Heim-WLAN</a></p></main>
<script>
const $=id=>document.getElementById(id),q=encodeURIComponent;
const esc=s=>String(s).replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';');
const post=(u,b)=>fetch(u,{method:'POST',body:b});
function pick(e){$('ss').value=e.dataset.s;$('pw').focus()}
async function scan(){$('nl').innerHTML='<small>Suche…</small>';for(let i=0;i<25;i++){const d=await (await fetch('/api/scan')).json();
if(d.nets){d.nets.sort((a,b)=>b.r-a.r);$('nl').innerHTML=d.nets.length?d.nets.map(n=>'<div class=row><span>'+esc(n.s)+'</span><button class=btn data-s="'+esc(n.s)+'" onclick="pick(this)">'+n.r+' dBm '+(n.o?'offen':'&#128274;')+'</button></div>').join(''):'<small>Keine Netze gefunden</small>';return}
await new Promise(r=>setTimeout(r,700))}$('nl').innerHTML='<small>Scan fehlgeschlagen</small>'}
function save(){if(!$('ss').value)return alert('SSID fehlt');post('/api/wifisave',new URLSearchParams({ssid:$('ss').value,pass:$('pw').value})).then(()=>{$('m').textContent='Gespeichert. Gerät startet neu und verbindet sich mit „'+$('ss').value+'“. Danach den QR-Code auf dem Display scannen.'})}
scan();</script></body></html>)HTML";
