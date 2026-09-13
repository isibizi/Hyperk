// File: include/daylight_page.h
//
// Configuration page for the daylight gate, served on port 8080.
// Uses the same Pico CSS stylesheet as the main Hyperk GUI (loaded from port 80),
// so the page looks like part of the regular interface.

#pragma once
#include <Arduino.h>

static const char DAYLIGHT_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Hyperk - Daylight</title>
<script>
(function(){var l=document.createElement('link');l.rel='stylesheet';l.href=location.protocol+'//'+location.hostname+'/css/hyperk.css';document.head.appendChild(l);})();
</script>
<style>
.badge{display:inline-block;padding:.15em .6em;border-radius:1em;font-weight:600;font-size:.9em}
.b-night{background:#1f3a5f;color:#fff}.b-day{background:#f2b705;color:#222}.b-unknown{background:#888;color:#fff}
.b-on{background:#2e8b57;color:#fff}.b-off{background:#b23a3a;color:#fff}
#results{margin:0;padding:0;list-style:none}
#results li{padding:.4em .6em;cursor:pointer;border-radius:.3em}
#results li:hover{background:var(--pico-secondary-background,#ddd);color:var(--pico-secondary-inverse,#000)}
.muted{opacity:.75}
#preview{display:block;margin-top:.5em}
dl.kv{display:grid;grid-template-columns:max-content 1fr;gap:.2em 1em;margin:0}
dl.kv dt{font-weight:600;margin:0}dl.kv dd{margin:0}
main.container{max-width:48rem}
</style>
</head>
<body>
<main class="container">
<nav>
  <ul><li><strong>Hyperk</strong> &middot; Daylight</li></ul>
  <ul><li><a id="homeLink" href="/">Main settings</a></li></ul>
</nav>

<article>
  <header><strong>Status</strong></header>
  <dl class="kv">
    <dt>Outside</dt><dd><span id="stState" class="badge b-unknown">unknown</span></dd>
    <dt>LED stream</dt><dd><span id="stGate" class="badge b-unknown">-</span> <small id="stReason" class="muted"></small></dd>
    <dt>Device time</dt><dd><span id="stTime">-</span> <small id="stSync" class="muted"></small></dd>
    <dt>Next sunrise</dt><dd id="stRise">-</dd>
    <dt>Next sunset</dt><dd id="stSet">-</dd>
    <dt>Next change</dt><dd id="stChange">-</dd>
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
  <header><strong>Location</strong></header>
  <label>Search a place
    <fieldset role="group">
      <input id="search" type="text" placeholder="e.g. Munich, Germany" autocomplete="off">
      <button type="button" onclick="doSearch()">Search</button>
    </fieldset>
  </label>
  <ul id="results"></ul>
  <small id="searchMsg" class="muted"></small>

  <label>Or paste coordinates / a Google Maps link
    <fieldset role="group">
      <input id="paste" type="text" placeholder="48.137154, 11.576124  or  https://www.google.com/maps/@48.137154,11.576124,15z">
      <button type="button" class="secondary" onclick="doPaste()">Use</button>
    </fieldset>
  </label>
  <small id="pasteMsg" class="muted">Google Maps: right-click a spot on the map and click the coordinates to copy them.</small>

  <div class="grid">
    <label>Latitude<input id="lat" type="number" step="any" min="-90" max="90" oninput="preview()"></label>
    <label>Longitude<input id="lon" type="number" step="any" min="-180" max="180" oninput="preview()"></label>
  </div>
  <label>Name (optional)<input id="label" type="text" maxlength="60" placeholder="Home"></label>
  <button type="button" class="secondary outline" onclick="useBrowserPosition()">Use my browser position</button>
  <small id="preview" class="muted"></small>
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
    <label>Evening: allow LEDs <span class="muted">(minutes after "dark" begins; negative = earlier)</span>
      <input id="setOffset" type="number" step="1" min="-360" max="360" value="0" oninput="preview()"></label>
    <label>Morning: block LEDs <span class="muted">(minutes after "dark" ends; negative = earlier)</span>
      <input id="riseOffset" type="number" step="1" min="-360" max="360" value="0" oninput="preview()"></label>
  </div>
  <details>
    <summary>Advanced</summary>
    <label>NTP time server<input id="ntp" type="text" maxlength="46" placeholder="pool.ntp.org"></label>
  </details>
  <footer>
    <button type="button" id="saveBtn" onclick="save()">Save</button>
    <small id="saveMsg" class="muted"></small>
  </footer>
</article>

<footer class="muted"><small id="fwInfo"></small></footer>
</main>

<script>
var $=function(id){return document.getElementById(id);};
var cfg=null;
$('homeLink').href=location.protocol+'//'+location.hostname+'/';

function fmt(epoch){ if(!epoch) return '-'; return new Date(epoch*1000).toLocaleString([], {weekday:'short',hour:'2-digit',minute:'2-digit'}); }
function fmtFull(epoch){ if(!epoch) return '-'; return new Date(epoch*1000).toLocaleString(); }

function render(s){
  var st=$('stState'); st.className='badge';
  if(s.state==='day'||s.state==='polarDay'){st.classList.add('b-day');st.textContent=(s.state==='polarDay'?'polar day':'daylight');}
  else if(s.state==='night'||s.state==='polarNight'){st.classList.add('b-night');st.textContent=(s.state==='polarNight'?'polar night':'dark');}
  else {st.classList.add('b-unknown');st.textContent='unknown';}
  var g=$('stGate'); g.className='badge '+(s.blocked?'b-off':'b-on'); g.textContent=s.blocked?'blocked (LEDs off)':'allowed';
  var reasons={disabled:'daylight control is disabled',override:'manual override',noLocation:'no location configured',noTime:'waiting for time sync - LEDs stay enabled',day:'it is light outside',night:'it is dark outside'};
  $('stReason').textContent=reasons[s.reason]||s.reason;
  $('stTime').textContent=s.timeSynced?fmtFull(s.now):'not synchronized yet';
  $('stSync').textContent=s.timeSynced?'(NTP ok)':'';
  $('stRise').textContent=fmt(s.sunrise); $('stSet').textContent=fmt(s.sunset); $('stChange').textContent=fmt(s.nextChange);
  ['Auto','Allow','Block'].forEach(function(n){var b=$('ov'+n); var active=(s.config.override===n.toLowerCase()); b.className=active?'':'outline';});
  $('fwInfo').textContent='Hyperk '+s.fw+' · daylight build '+s.build+' · uptime '+s.uptime+'s · free heap '+s.freeHeap+' bytes';
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
}

function refresh(initial){
  fetch('/api/daylight',{cache:'no-store'}).then(function(r){return r.json();}).then(function(s){
    render(s);
    if(initial||!cfg){ cfg=s.config; fillForm(cfg); preview(); }
  }).catch(function(){ $('stReason').textContent='device not reachable'; });
}

function collect(){
  var lat=parseFloat($('lat').value), lon=parseFloat($('lon').value);
  var c={enabled:$('enabled').checked, altitude:parseFloat($('altitude').value),
         setOffset:parseInt($('setOffset').value||'0',10), riseOffset:parseInt($('riseOffset').value||'0',10),
         label:$('label').value, ntp:$('ntp').value};
  if(isNaN(lat)||isNaN(lon)) c.clearLocation=true; else { c.lat=lat; c.lon=lon; }
  return c;
}

function post(body,msgEl,okText){
  return fetch('/api/daylight',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(function(r){return r.json();})
    .then(function(j){ if(j.ok){ if(msgEl) msgEl.textContent=okText; setTimeout(function(){refresh(true);},600); } else { if(msgEl) msgEl.textContent='Error: '+(j.error||'rejected'); } })
    .catch(function(){ if(msgEl) msgEl.textContent='Error: device not reachable'; });
}

function save(){
  var c=collect();
  if(c.enabled && c.clearLocation){ $('saveMsg').textContent='Please set a location first.'; return; }
  $('saveMsg').textContent='Saving...';
  post(c,$('saveMsg'),'Saved.');
}
function setOverride(mode){ post({override:mode},$('stReason'),'Override set.'); }

var previewTimer=null;
function preview(){
  clearTimeout(previewTimer);
  previewTimer=setTimeout(function(){
    var lat=parseFloat($('lat').value), lon=parseFloat($('lon').value);
    if(isNaN(lat)||isNaN(lon)){ $('preview').textContent=''; return; }
    var q='/api/daylight?lat='+lat+'&lon='+lon+'&altitude='+$('altitude').value+'&setOffset='+($('setOffset').value||0)+'&riseOffset='+($('riseOffset').value||0);
    fetch(q,{cache:'no-store'}).then(function(r){return r.json();}).then(function(s){
      if(!s.timeSynced){ $('preview').textContent='Preview available once the device time is synchronized.'; return; }
      var txt='For this location: next sunrise '+fmt(s.sunrise)+', next sunset '+fmt(s.sunset)+'. Right now it is '+(s.blocked?'light (LEDs would be off)':'dark (LEDs allowed)')+'.';
      if(s.state==='polarDay') txt='Polar day at this location - LEDs would stay off all day.';
      if(s.state==='polarNight') txt='Polar night at this location - LEDs allowed all day.';
      $('preview').textContent=txt;
    }).catch(function(){});
  },400);
}

function setLocation(lat,lon,name){
  $('lat').value=Math.round(lat*1e6)/1e6; $('lon').value=Math.round(lon*1e6)/1e6;
  if(name) $('label').value=name;
  preview();
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

function parseCoords(s){
  s=s.trim(); var m; var N='(-?\\d{1,3}(?:\\.\\d+)?)';
  function chk(a,b){ var la=parseFloat(a), lo=parseFloat(b); if(isNaN(la)||isNaN(lo)||la<-90||la>90||lo<-180||lo>180) return null; return [la,lo]; }
  if((m=s.match(new RegExp('@'+N+','+N)))) return chk(m[1],m[2]);
  if((m=s.match(new RegExp('[?&](?:q|query|ll|center|destination|daddr|saddr)='+N+'(?:,|%2C)'+N,'i')))) return chk(m[1],m[2]);
  if((m=s.match(new RegExp('!3d'+N+'!4d'+N)))) return chk(m[1],m[2]);
  var dms=/(\d{1,3})[°º]\s*(\d{1,2})['′]\s*(\d{1,2}(?:\.\d+)?)?["″]?\s*([NS])[\s,]+(\d{1,3})[°º]\s*(\d{1,2})['′]\s*(\d{1,2}(?:\.\d+)?)?["″]?\s*([EW])/i;
  if((m=s.match(dms))){
    var la=(+m[1])+(+m[2])/60+((+m[3])||0)/3600; if(/s/i.test(m[4])) la=-la;
    var lo=(+m[5])+(+m[6])/60+((+m[7])||0)/3600; if(/w/i.test(m[8])) lo=-lo;
    return chk(la,lo);
  }
  if((m=s.match(new RegExp('^'+N+'\\s*[,;\\s]\\s*'+N+'$')))) return chk(m[1],m[2]);
  if((m=s.match(/^(-?\d{1,3}),(\d+)\s*[;\s,]\s*(-?\d{1,3}),(\d+)$/))) return chk(m[1]+'.'+m[2],m[3]+'.'+m[4]);
  return null;
}
function doPaste(){
  var r=parseCoords($('paste').value);
  if(!r){ $('pasteMsg').textContent='Could not read coordinates. Expected e.g. 48.137154, 11.576124'; return; }
  setLocation(r[0],r[1],null);
  $('pasteMsg').textContent='Parsed: '+r[0].toFixed(6)+', '+r[1].toFixed(6)+' - remember to save.';
}
$('paste').addEventListener('keydown',function(e){ if(e.key==='Enter'){ e.preventDefault(); doPaste(); } });

function useBrowserPosition(){
  try{
    if(!navigator.geolocation) throw new Error('n/a');
    navigator.geolocation.getCurrentPosition(function(p){ setLocation(p.coords.latitude,p.coords.longitude,null); $('preview').textContent='Browser position applied - remember to save.'; },
      function(){ $('preview').textContent='Browser refused to share the position (most browsers only allow it on https pages). Paste coordinates instead.'; },{timeout:10000});
  }catch(e){ $('preview').textContent='Position not available in this browser. Paste coordinates instead.'; }
}

refresh(true);
setInterval(function(){refresh(false);},10000);
</script>
</body>
</html>
)HTML";
