// File: include/daylight_page.h
//
// Configuration page for the daylight gate, served on port 8080.
// Uses the same Pico CSS stylesheet as the main Hyperk GUI (loaded from port 80),
// so the page looks like part of the regular interface.

#pragma once
#include <Arduino.h>

#ifndef APP_VERSION
    #define APP_VERSION "unknown"
#endif
#ifndef HYPERK_DAYLIGHT_BUILD
    #define HYPERK_DAYLIGHT_BUILD "dev"
#endif

static const char DAYLIGHT_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en" data-theme="dark">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Daylight - Hyperk</title>
<link id="app-favicon" rel="icon" href="data:,">
<script>
(function(){
  var l=document.createElement('link');
  l.rel='stylesheet';
  l.href=location.protocol+'//'+location.hostname+'/css/hyperk.css';
  l.onload=function(){
    var icon=getComputedStyle(document.documentElement).getPropertyValue('--app-icon').match(/data:[^")]+/);
    if(icon) document.getElementById('app-favicon').href=icon[0];
  };
  document.head.appendChild(l);
})();
</script>
<style>
.badge{display:inline-block;padding:.1em .7em;border-radius:1em;font-weight:600;font-size:.9em;
  border:1px solid currentColor;background:transparent}
.b-night{color:var(--pico-primary,#0172ad)}
.b-day{color:#eab308}
.b-unknown{color:var(--pico-muted-color,#7b8495)}
.b-on{color:#43a047}
.b-off{color:var(--pico-muted-color,#7b8495)}
#results{margin:0;padding:0;list-style:none}
#results li{padding:.4em .6em;cursor:pointer;border-radius:.3em}
#results li:hover{background:var(--pico-secondary-background,#ddd);color:var(--pico-secondary-inverse,#000)}
.muted{opacity:.75}
#preview{display:block;margin-top:.5em}
dl.kv{display:grid;grid-template-columns:max-content 1fr;gap:.2em 1em;margin:0}
dl.kv dt{font-weight:600;margin:0}dl.kv dd{margin:0}
main.container{max-width:48rem}
@media (max-width:640px){#verTag{display:none}}
</style>
</head>
<body>
<nav class="container-fluid">
  <ul>
    <li style="display:flex;align-items:center">
      <span class="logo-h"></span><strong class="yperk">y<span class="perk">perk</span></strong>
      <small id="verTag" class="muted" style="margin-left:.6rem;white-space:nowrap">Daylight )HTML" APP_VERSION " / " HYPERK_DAYLIGHT_BUILD R"HTML(</small>
    </li>
  </ul>
  <ul>
    <li><a id="navHome" href="/">Home</a></li>
    <li><a id="navSettings" href="/">Settings</a></li>
    <li><a href="#" aria-current="page">Daylight</a></li>
  </ul>
</nav>

<main class="container">

<article>
  <header><strong>Status</strong></header>
  <dl class="kv">
    <dt>Outside</dt><dd><span id="stState" class="badge b-unknown">unknown</span></dd>
    <dt>LED stream</dt><dd><span id="stGate" class="badge b-unknown">-</span> <small id="stReason" class="muted"></small></dd>
    <dt>Next sunrise</dt><dd id="stRise">-</dd>
    <dt>Next sunset</dt><dd id="stSet">-</dd>
    <dt id="stChangeLabel" hidden>Next change</dt><dd id="stChange" hidden>-</dd>
  </dl>
  <footer>
    <div role="group">
      <button id="ovAuto" class="outline" onclick="setOverride('auto')">Automatic</button>
      <button id="ovAllow" class="outline" onclick="setOverride('allow')">Always allow</button>
      <button id="ovBlock" class="outline" onclick="setOverride('block')">Always block</button>
    </div>
    <small class="muted">Override is stored on the device and survives a reboot.</small>
  </footer>
</article>

<article>
  <header><strong>Rules</strong></header>
  <label><input id="enabled" type="checkbox" role="switch"> Enable daylight control (LEDs stay off while it is light outside)</label>
  <label>It is "dark" when the sun is below
    <select id="altitude" onchange="preview()">
      <option value="-0.833">the horizon (sunset / sunrise)</option>
      <option value="-6">civil twilight, 6&deg; below the horizon (recommended)</option>
      <option value="-12">nautical twilight, 12&deg; below the horizon</option>
      <option value="-18">astronomical twilight, 18&deg; below the horizon</option>
    </select>
  </label>
  <div class="grid">
    <label>Evening: switch on later by
      <input id="setOffset" type="number" step="1" min="-360" max="360" value="0" oninput="preview()"></label>
    <label>Morning: switch off later by
      <input id="riseOffset" type="number" step="1" min="-360" max="360" value="0" oninput="preview()"></label>
  </div>
  <small class="muted">Minutes. Negative values shift the other way.</small>
  <footer>
    <button type="button" id="rulesBtn" onclick="saveRules()">Save rules</button>
    <small id="rulesMsg" class="muted"></small>
  </footer>
</article>

<article>
  <header><strong>Location</strong></header>
  <label>Search a place
    <fieldset role="group">
      <input id="search" type="text" placeholder="e.g. Munich, Germany" autocomplete="off">
      <button type="button" onclick="doSearch()">Search</button>
    </fieldset>
  </label>
  <ul id="results"></ul>
  <small id="searchMsg" class="muted"></small>

  <input id="lat" type="hidden"><input id="lon" type="hidden">
  <label id="labelName">Name of the place<input id="label" type="text" maxlength="60" placeholder="Home" oninput="paintName()"></label>
  <button type="button" class="secondary outline" onclick="useBrowserPosition()">Use my browser position</button>
  <small id="preview" class="muted"></small>
  <footer>
    <button type="button" id="locBtn" onclick="saveLocation()">Save location</button>
    <small id="locMsg" class="muted"></small>
  </footer>
</article>

<article>
  <header><strong>Time server</strong></header>
  <p class="muted"><small>The device reads the clock from the network. Leave this as it is unless your network blocks the public time servers.</small></p>
  <label>NTP server<input id="ntp" type="text" maxlength="46" placeholder="pool.ntp.org"></label>
  <footer>
    <button type="button" id="ntpBtn" class="secondary" onclick="saveTimeServer()">Save time server</button>
    <small id="ntpMsg" class="muted"></small>
  </footer>
</article>

<article>
  <header><strong>Firmware update</strong></header>
  <p class="muted"><small>The main Hyperk page can only install updates from the official release server. Here you can upload a firmware file yourself, for example <code>OTA_Hyperk_0.0.5_esp8266.bin</code> from your own release page.</small></p>
  <input id="fwFile" type="file" accept=".bin">
  <button type="button" id="fwBtn" class="secondary" onclick="uploadFirmware()">Upload and flash</button>
  <progress id="fwProgress" value="0" max="100" style="display:none"></progress>
  <small id="fwMsg" class="muted">Do not switch off the device during the update.</small>
</article>

</main>

<script>
var $=function(id){return document.getElementById(id);};
var cfg=null;
var mainGui=location.protocol+'//'+location.hostname+'/';
$('navHome').href=mainGui+'index.html';
$('navSettings').href=mainGui+'settings.html';

function fmt(epoch){ if(!epoch) return '-'; return new Date(epoch*1000).toLocaleString('en-GB',{weekday:'short',hour:'2-digit',minute:'2-digit',hour12:false}); }

var lastStatus=null;

function render(s){
  lastStatus=s;
  var st=$('stState'); st.className='badge';
  if(s.state==='day'||s.state==='polarDay'){st.classList.add('b-day');st.textContent=(s.state==='polarDay'?'polar day':'daylight');}
  else if(s.state==='night'||s.state==='polarNight'){st.classList.add('b-night');st.textContent=(s.state==='polarNight'?'polar night':'dark');}
  else {st.classList.add('b-unknown');st.textContent='unknown';}
  var g=$('stGate'); g.className='badge '+(s.blocked?'b-off':'b-on'); g.textContent=s.blocked?'blocked (LEDs off)':'allowed';
  // only say something when the rule is not simply doing its job
  var notes={disabled:'switched off',override:'manual override',noLocation:'no location set',noTime:'waiting for the time from the network'};
  $('stReason').textContent=notes[s.reason]||'';
  $('stRise').textContent=fmt(s.sunrise); $('stSet').textContent=fmt(s.sunset);
  var shifted=s.nextChange&&s.nextChange!==s.sunrise&&s.nextChange!==s.sunset;
  $('stChangeLabel').hidden=!shifted; $('stChange').hidden=!shifted;
  $('stChange').textContent=fmt(s.nextChange);
  ['Auto','Allow','Block'].forEach(function(n){var b=$('ov'+n); var active=(s.config.override===n.toLowerCase()); b.className=active?'':'outline';});
}

function fillForm(c){
  $('enabled').checked=!!c.enabled;
  $('lat').value=(c.lat===null||c.lat===undefined)?'':c.lat;
  $('lon').value=(c.lon===null||c.lon===undefined)?'':c.lon;
  $('label').value=c.label||'';
  var alt=String(c.altitude); var sel=$('altitude'); var found=false;
  for(var i=0;i<sel.options.length;i++){ if(Math.abs(parseFloat(sel.options[i].value)-c.altitude)<0.01){sel.selectedIndex=i;found=true;break;} }
  if(!found){ var o=document.createElement('option'); o.value=alt; o.textContent=alt+'° (custom)'; sel.appendChild(o); sel.value=alt; }
  $('setOffset').value=c.setOffset; $('riseOffset').value=c.riseOffset; $('ntp').value=c.ntp||'';
  paintName();
}

function fetchStatus(q){
  return fetch(q||'/api/daylight',{cache:'no-store'}).then(function(r){
    if(!r.ok) throw new Error('HTTP '+r.status);
    return r.text();
  }).then(function(t){
    if(!t) throw new Error('empty answer');
    try { return JSON.parse(t); } catch(e){ throw new Error('bad answer: '+t.slice(0,60)); }
  });
}

function refresh(initial,attempt){
  attempt=attempt||1;
  fetchStatus().then(function(s){
    try { render(s); } catch(e){ $('stReason').textContent='display error: '+e.message; }
    if(initial||!cfg){ cfg=s.config; fillForm(cfg); preview(); }
  }).catch(function(err){
    if(attempt<4){ setTimeout(function(){ refresh(initial,attempt+1); },800); return; }
    $('stReason').textContent='no answer from the device ('+err.message+')';
  });
}

function post(body,msgEl,okText){
  var form=new URLSearchParams();
  for(var k in body){ if(body[k]!==undefined&&body[k]!==null) form.append(k,body[k]); }
  return fetch('/api/daylight',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:form.toString()})
    .then(function(r){return r.json();})
    .then(function(j){
      if(j.ok){ if(msgEl) msgEl.textContent=okText; setTimeout(function(){refresh(true);},600); }
      else { if(msgEl) msgEl.textContent='Error: '+(j.error||'rejected'); }
    })
    .catch(function(){ if(msgEl) msgEl.textContent='Error: device not reachable'; });
}

function saveRules(){
  $('rulesMsg').textContent='Saving...';
  post({enabled:$('enabled').checked, altitude:$('altitude').value,
        setOffset:$('setOffset').value||'0', riseOffset:$('riseOffset').value||'0'},
       $('rulesMsg'),'Saved.');
}

function saveLocation(){
  var lat=parseFloat($('lat').value), lon=parseFloat($('lon').value);
  if(isNaN(lat)||isNaN(lon)){ $('locMsg').textContent='Please pick a place first.'; return; }
  if(!$('label').value.trim()){ $('locMsg').textContent='Please name the place.'; $('label').focus(); return; }
  $('locMsg').textContent='Saving...';
  post({lat:lat, lon:lon, label:$('label').value}, $('locMsg'),'Saved.');
}

function saveTimeServer(){
  $('ntpMsg').textContent='Saving...';
  post({ntp:$('ntp').value||'pool.ntp.org'}, $('ntpMsg'),'Saved.');
}

function setOverride(mode){ post({override:mode},$('stReason'),'Override set.'); }

var previewTimer=null;
function preview(){
  clearTimeout(previewTimer);
  previewTimer=setTimeout(function(){
    var lat=parseFloat($('lat').value), lon=parseFloat($('lon').value);
    if(isNaN(lat)||isNaN(lon)){ $('preview').textContent=''; return; }
    var q='/api/daylight?lat='+lat+'&lon='+lon+'&altitude='+$('altitude').value+'&setOffset='+($('setOffset').value||0)+'&riseOffset='+($('riseOffset').value||0);
    fetchStatus(q).then(function(s){
      if(!s.timeSynced){ $('preview').textContent='Preview available once the device time is synchronized.'; return; }
      var txt='For this location: next sunrise '+fmt(s.sunrise)+', next sunset '+fmt(s.sunset)+'. Right now it is '+(s.blocked?'light (LEDs would be off)':'dark (LEDs allowed)')+'.';
      if(s.state==='polarDay') txt='Polar day at this location - LEDs would stay off all day.';
      if(s.state==='polarNight') txt='Polar night at this location - LEDs allowed all day.';
      $('preview').textContent=txt;
    }).catch(function(){});
  },400);
}

function setLocation(lat,lon,name){
  var la=Math.round(lat*1e6)/1e6, lo=Math.round(lon*1e6)/1e6;
  $('lat').value=la; $('lon').value=lo;
  if(name) $('label').value=name;
  paintName();
  preview();
}

function hasPlace(){ return !isNaN(parseFloat($('lat').value)) && !isNaN(parseFloat($('lon').value)); }

function paintName(){
  var needed=hasPlace();
  $('label').required=needed;
  $('labelName').firstChild.nodeValue=needed?'Name of the place':'Name of the place (optional)';
}

function doSearch(){
  var q=$('search').value.trim(); var list=$('results'); list.innerHTML=''; 
  if(!q){ $('searchMsg').textContent='Enter a place name.'; return; }
  $('searchMsg').textContent='Searching...';
  fetch('https://geocoding-api.open-meteo.com/v1/search?name='+encodeURIComponent(q)+'&count=6&language=de&format=json')
    .then(function(r){return r.json();})
    .then(function(j){
      var res=j.results||[];
      if(!res.length){ $('searchMsg').textContent='No results.'; return; }
      $('searchMsg').textContent='Click a result to use it:';
      res.forEach(function(r){
        var li=document.createElement('li');
        var parts=[r.name]; if(r.admin1) parts.push(r.admin1); if(r.country) parts.push(r.country);
        li.textContent=parts.join(', ')+'  ('+r.latitude.toFixed(4)+', '+r.longitude.toFixed(4)+')';
        li.onclick=function(){ setLocation(r.latitude,r.longitude,parts.join(', ')); list.innerHTML=''; $('searchMsg').textContent='Location set - remember to save.'; };
        list.appendChild(li);
      });
    })
    .catch(function(){ $('searchMsg').textContent='Search unavailable (needs internet access from this browser). Paste coordinates instead.'; });
}
$('search').addEventListener('keydown',function(e){ if(e.key==='Enter'){ e.preventDefault(); doSearch(); } });

function useBrowserPosition(){
  try{
    if(!navigator.geolocation) throw new Error('n/a');
    navigator.geolocation.getCurrentPosition(function(p){ setLocation(p.coords.latitude,p.coords.longitude,null); if(!$('label').value) $('label').focus(); },
      function(){ $('preview').textContent='Browser refused to share the position (most browsers only allow it on https pages). Paste coordinates instead.'; },{timeout:10000});
  }catch(e){ $('preview').textContent='Position not available in this browser. Paste coordinates instead.'; }
}

function uploadFirmware(){
  var f=$('fwFile').files[0];
  if(!f){ $('fwMsg').textContent='Please choose a .bin file first.'; return; }
  if(!/\.bin$/i.test(f.name)){ $('fwMsg').textContent='This is not a .bin firmware file.'; return; }
  var bar=$('fwProgress'); bar.style.display='block'; bar.value=0;
  $('fwBtn').disabled=true;
  $('fwMsg').textContent='Uploading '+f.name+' ('+Math.round(f.size/1024)+' kB). Do not switch off the device.';
  var fd=new FormData(); fd.append('update',f,f.name);
  var x=new XMLHttpRequest();
  x.open('POST','/update',true);
  x.upload.onprogress=function(e){ if(e.lengthComputable){ bar.value=Math.round(e.loaded/e.total*100); } };
  x.onload=function(){
    $('fwBtn').disabled=false;
    if(x.status===200){ $('fwMsg').textContent='Done. The device is rebooting, reload this page in about 30 seconds.'; }
    else { bar.style.display='none'; $('fwMsg').textContent='Failed: '+(x.responseText||x.status)+'. The device keeps its current firmware.'; }
  };
  x.onerror=function(){ $('fwBtn').disabled=false; bar.style.display='none'; $('fwMsg').textContent='Connection lost during upload.'; };
  x.send(fd);
}

refresh(true);
setInterval(function(){refresh(false);},10000);
</script>
</body>
</html>
)HTML";
