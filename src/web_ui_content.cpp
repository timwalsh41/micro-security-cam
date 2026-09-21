#include "web_ui_content.h"

/**
 * @file web_ui_content.cpp
 * @brief Storage for the embedded single-page web UI.
 *
 * The document is a plain read-only string; on ESP32 it is stored in
 * flash automatically. It talks to the firmware's JSON/INI endpoints:
 *   GET  /status                      — status JSON
 *   GET  /control?cmd=...             — control commands
 *   GET  /files?dir=&page=            — directory listing JSON
 *   GET  /download?path=              — file download
 *   GET  /logs                        — log directory listing JSON
 *   GET  /log?file=YYYYMMDD.log       — log tail (text/plain)
 *   POST /update (dest, config)       — bulk config overlay + save
 *   GET  /stream                      — MJPEG preview (after stream_start)
 *
 * The UI is dependency-free: one page, vanilla JS, no frameworks.
 */

const char* web_ui_html()
{
    static const char UI[] = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Micro Security Cam</title>
<style>
:root{--bg:#11151c;--panel:#1b2230;--fg:#dbe2ee;--muted:#8a97ab;--accent:#4da3ff;--ok:#3ecf8e;--warn:#ffb347;--err:#ff6b6b}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:12px 20px;background:var(--panel);display:flex;align-items:center;gap:12px}
header h1{font-size:18px;margin:0}
.badge{font-size:12px;padding:2px 10px;border-radius:10px;background:#2a3346}
main{max-width:900px;margin:0 auto;padding:16px;display:grid;gap:16px}
section{background:var(--panel);border-radius:10px;padding:14px 16px}
h2{font-size:14px;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);margin:0 0 10px}
button{background:#2a3346;color:var(--fg);border:1px solid #3a465e;border-radius:6px;padding:8px 14px;cursor:pointer;font-size:14px}
button:hover{border-color:var(--accent)}
button.primary{background:var(--accent);color:#08243f;border-color:var(--accent)}
button.stop{background:var(--err);color:#3a0d0d;border-color:var(--err)}
button:disabled{opacity:.45;cursor:default}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px}
.stat{background:#151b26;border-radius:8px;padding:8px 10px}
.stat .k{font-size:11px;color:var(--muted)}
.stat .v{font-size:16px;font-weight:600}
ul.list{list-style:none;margin:0;padding:0;max-height:340px;overflow:auto}
ul.list li{display:flex;justify-content:space-between;align-items:center;padding:6px 8px;border-bottom:1px solid #232c3d;gap:8px}
ul.list li span.name{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
ul.list li.dir span.name{color:var(--accent);cursor:pointer;font-weight:600}
a.dl{color:var(--ok);text-decoration:none;font-size:13px}
a.dl:hover{text-decoration:underline}
img#preview{max-width:100%;border-radius:8px;background:#000}
pre.log{background:#0d1117;border:1px solid #232c3d;border-radius:8px;padding:10px;font-size:12px;max-height:320px;overflow:auto;white-space:pre-wrap}
textarea#config{width:100%;min-height:260px;background:#0d1117;color:var(--fg);border:1px solid #3a465e;border-radius:8px;padding:10px;font-family:ui-monospace,monospace;font-size:12px}
.err{color:var(--err);font-size:13px}
.muted{color:var(--muted);font-size:12px}
.crumb{color:var(--muted);font-size:13px}
.crumb b{color:var(--accent);cursor:pointer}
</style>
</head>
<body>
<header>
  <h1>Micro Security Cam</h1>
  <span id="sd-badge" class="badge">SD?</span>
</header>
<main>

<section id="status">
  <h2>Status</h2>
  <div class="grid">
    <div class="stat"><div class="k">Recording</div><div class="v" id="st-rec">-</div></div>
    <div class="stat"><div class="k">Streaming</div><div class="v" id="st-stream">-</div></div>
    <div class="stat"><div class="k">Frames</div><div class="v" id="st-frames">-</div></div>
    <div class="stat"><div class="k">Clip size</div><div class="v" id="st-size">-</div></div>
    <div class="stat"><div class="k">Streams</div><div class="v" id="st-views">-</div></div>
    <div class="stat"><div class="k">SD free</div><div class="v" id="st-free">-</div></div>
    <div class="stat"><div class="k">Photos today</div><div class="v" id="st-photos">-</div></div>
    <div class="stat"><div class="k">Videos today</div><div class="v" id="st-videos">-</div></div>
  </div>
  <p class="muted">Auto-refreshes every 5 s.</p>
</section>

<section id="controls">
  <h2>Controls</h2>
  <div class="row">
    <button id="btn-rec-start" class="primary">Start recording</button>
    <button id="btn-rec-stop">Stop recording</button>
    <button id="btn-photo">Take photo</button>
  </div>
  <div class="row" style="margin-top:8px">
    <button id="btn-stream-start" class="primary">Start preview</button>
    <button id="btn-stream-stop" class="stop">Stop preview</button>
  </div>
  <p id="ctrl-msg" class="muted"></p>
</section>

<section id="preview">
  <h2>Live preview</h2>
  <img id="preview-img" src="" alt="">
  <p class="muted">Preview uses the MJPEG <code>/stream</code> endpoint; it
  works while preview (streaming) is started.</p>
</section>

<section id="files">
  <h2>Files</h2>
  <p class="crumb">Path: <b id="crumb">/</b></p>
  <ul class="list" id="file-list"></ul>
  <div class="row" style="margin-top:8px">
    <button id="btn-prev-page">&#8592; Prev</button>
    <span class="muted" id="page-info"></span>
    <button id="btn-next-page">Next &#8594;</button>
    <button id="btn-up">Up</button>
  </div>
  <p class="muted">The clip currently being recorded is flagged and cannot
  be downloaded until it closes.</p>
</section>

<section id="logs">
  <h2>System logs</h2>
  <ul class="list" id="log-list"></ul>
  <pre class="log" id="log-view">Select a log file above.</pre>
</section>

<section id="config">
  <h2>Configuration</h2>
  <div class="row" style="margin-bottom:8px">
    <button id="btn-config-load">Reload config</button>
    <button id="btn-config-save" class="primary">Save to NVS</button>
    <button id="btn-config-save-sd">Save to SD</button>
    <span id="cfg-msg" class="muted"></span>
  </div>
  <textarea id="config" spellcheck="false"></textarea>
  <p class="muted">Saving applies the INI text as an overlay onto the live
  config (POST /update). New settings take effect per their subsystem;
  some require a reboot.</p>
</section>

</main>
<script>
'use strict';
function $(id){return document.getElementById(id);}
function fmtBytes(n){
  if(!n&&n!==0)return '-';
  if(n>1073741824)return (n/1073741824).toFixed(2)+' GB';
  if(n>1048576)return (n/1048576).toFixed(2)+' MB';
  if(n>1024)return (n/1024).toFixed(1)+' KB';
  return n+' B';
}
function msg(text){$('ctrl-msg').textContent=text;}

async function api(path){
  const r=await fetch(path,{credentials:'same-origin'});
  if(r.status===401){throw new Error('unauthorized (401)');}
  return r;
}

// ---------- status ----------
function isDirName(n){return n!=='logs';}
function renderStatus(s){
  $('st-rec').textContent=s.recording?'REC':'off';
  $('st-rec').style.color=s.recording?'var(--err)':'var(--muted)';
  $('st-stream').textContent=s.streaming?'live':'off';
  $('st-stream').style.color=s.streaming?'var(--ok)':'var(--muted)';
  $('st-frames').textContent=s.frames;
  $('st-size').textContent=fmtBytes(s.file_size);
  $('st-views').textContent=s.connected_streams;
  $('st-free').textContent=s.sd?fmtBytes(s.sd_free_bytes):'no card';
  $('st-photos').textContent=s.sd?s.photos:'-';
  $('st-videos').textContent=s.sd?s.videos:'-';
  const badge=$('sd-badge');
  badge.textContent=s.sd?'SD ready':'SD missing';
  badge.style.background=s.sd?'#1f4433':'#4b2222';
}
async function refreshStatus(){
  try{
    const r=await api('/status');
    renderStatus(await r.json());
  }catch(e){console.warn(e);}
}
setInterval(refreshStatus,5000);

// ---------- controls ----------
async function control(cmd){
  try{
    const r=await api('/control?cmd='+cmd);
    const j=await r.json();
    msg('result: '+(j.result==='ok'?'ok':'error '+j.code));
    if(cmd==='stream_stop'){$('preview-img').src='';}
    refreshStatus();
  }catch(e){msg(e.message);}
}
$('btn-rec-start').onclick=()=>control('start');
$('btn-rec-stop').onclick=()=>control('stop');
$('btn-photo').onclick=async()=>{
  try{
    const r=await api('/control?cmd=photo');
    const j=await r.json();
    msg('photo: '+j);
    refreshStatus();
  }catch(e){msg(e.message);}
};
$('btn-stream-start').onclick=async()=>{
  try{
    await api('/control?cmd=stream_start');
    // The /stream route 409s until streaming has started; give the
    // pipeline a moment, then let the browser retry via the img tag.
    $('preview-img').src='/stream';
    msg('preview starting');
    refreshStatus();
  }catch(e){msg(e.message);}
};
$('btn-stream-stop').onclick=()=>{$('preview-img').src='';control('stream_stop');};

// ---------- files ----------
let curDir='/';
let curPage=0;
let curTotal=0;

function fileRow(e,dir){
  const li=document.createElement('li');
  li.className=e.d?'dir':'file';
  const label=document.createElement('span');
  label.className='name';
  label.textContent=e.n+(e.r?' [recording]':'');
  li.appendChild(label);
  if(e.d){
    label.onclick=()=>{browse(dir==='/'?('/'+e.n):dir+'/'+e.n);};
  }else{
    const size=document.createElement('span');
    size.className='muted';
    size.textContent=fmtBytes(e.s);
    li.appendChild(size);
    if(e.r){
      const flag=document.createElement('span');
      flag.className='err';
      flag.textContent='rec';
      li.appendChild(flag);
    }else{
      const a=document.createElement('a');
      a.className='dl';
      a.textContent='download';
      a.href='/download?path='+
        encodeURIComponent(dir==='/'?('/'+e.n):dir+'/'+e.n);
      li.appendChild(a);
    }
  }
  return li;
}

async function browse(dir,page){
  page=page||0;
  try{
    const r=await api('/files?dir='+encodeURIComponent(dir)+'&page='+page);
    if(!r.ok){throw new Error('HTTP '+r.status);}
    const j=await r.json();
    curDir=j.dir;curPage=j.page;curTotal=j.total;
    $('crumb').textContent=j.dir;
    const ul=$('file-list');
    ul.innerHTML='';
    for(const e of (j.entries||[])){ul.appendChild(fileRow(e,j.dir));}
    if(!(j.entries||[]).length){
      const li=document.createElement('li');
      li.innerHTML='<span class="muted">empty</span>';
      ul.appendChild(li);
    }
    $('page-info').textContent='page '+(j.page+1)+' of '+
      Math.max(1,Math.ceil(j.total/12));
    $('btn-next-page').disabled=!j.has_more;
    $('btn-prev-page').disabled=j.page===0;
  }catch(e){
    $('file-list').innerHTML='<li><span class="err">'+e.message+'</span></li>';
  }
}
$('btn-next-page').onclick=()=>browse(curDir,curPage+1);
$('btn-prev-page').onclick=()=>browse(curDir,curPage-1);
$('btn-up').onclick=()=>{
  if(curDir!=='/'&&curDir!==''){browse('/');}else{browse('/');}
};

// ---------- logs ----------
async function refreshLogs(){
  try{
    const r=await api('/logs');
    if(!r.ok)return;
    const j=await r.json();
    const ul=$('log-list');
    ul.innerHTML='';
    for(const e of (j.entries||[])){
      if(e.d)continue;
      const li=document.createElement('li');
      const label=document.createElement('span');
      label.className='name';
      label.textContent=e.n;
      label.style.cursor='pointer';
      label.style.color='var(--accent)';
      label.onclick=()=>viewLog(e.n);
      li.appendChild(label);
      const size=document.createElement('span');
      size.className='muted';
      size.textContent=fmtBytes(e.s);
      li.appendChild(size);
      ul.appendChild(li);
    }
    if(!(j.entries||[]).length){
      ul.innerHTML='<li><span class="muted">no log files</span></li>';
    }
  }catch(e){/* leave the list as-is */}
}
async function viewLog(name){
  $('log-view').textContent='Loading '+name+' ...';
  try{
    const r=await api('/log?file='+encodeURIComponent(name));
    if(!r.ok){throw new Error('HTTP '+r.status);}
    $('log-view').textContent=await r.text();
  }catch(e){$('log-view').textContent='Error: '+e.message;}
}

// ---------- config editor ----------
async function loadConfig(){
  try{
    const r=await api('/control?cmd=config');
    if(!r.ok){throw new Error('HTTP '+r.status);}
    $('config').value=await r.text();
    $('cfg-msg').textContent='';
  }catch(e){$('cfg-msg').textContent='load failed: '+e.message;}
}
async function saveConfig(dest){
  try{
    const body=new URLSearchParams();
    body.set('config',$('config').value);
    body.set('dest',dest);
    const r=await fetch('/update',{method:'POST',credentials:'same-origin',body:body});
    const j=await r.json().catch(()=>({}));
    $('cfg-msg').textContent=(r.ok&&j.result==='ok')
      ?'saved ('+dest+')':'error '+(j.error||r.status);
  }catch(e){$('cfg-msg').textContent='save failed: '+e.message;}
}
$('btn-config-load').onclick=loadConfig;
$('btn-config-save').onclick=()=>saveConfig('nvs');
$('btn-config-save-sd').onclick=()=>saveConfig('sd');

// ---------- init ----------
refreshStatus();
browse('/');
refreshLogs();
</script>
</body>
</html>)html";

    return UI;
}
