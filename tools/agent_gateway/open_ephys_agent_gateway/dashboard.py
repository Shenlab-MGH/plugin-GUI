DASHBOARD_HTML = """<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Open Ephys Agent Gateway</title>
<style>
body{font:15px system-ui;background:#111827;color:#e5e7eb;margin:0}
main{max-width:900px;margin:32px auto;padding:0 20px}
.card{background:#1f2937;border:1px solid #374151;border-radius:12px;padding:18px;margin:14px 0}
.bad{color:#fca5a5}.good{color:#86efac}.warn{background:#451a1a;border-color:#991b1b}
button,input{font:inherit;padding:9px;border-radius:7px;border:1px solid #4b5563}
input{width:70%;background:#111827;color:#fff}button{cursor:pointer;margin:4px}
button:disabled{cursor:not-allowed;opacity:.4}
pre{white-space:pre-wrap;word-break:break-word}
</style>
<main>
<h1>Open Ephys Agent Gateway v0.0.1</h1>
<div class="card"><input id="token" type="password" placeholder="Bearer token"><button onclick="refresh()">Connect</button></div>
<div class="card"><h2>Status</h2><div id="headline">Not connected</div><pre id="status"></pre></div>
<div id="safety" class="card"><h2>Safety gate</h2><p id="safetyText">Not evaluated.</p></div>
<div class="card"><h2>Transport</h2>
<button class="transport" onclick="submitTarget('IDLE')">IDLE</button>
<button class="transport" onclick="submitTarget('ACQUIRE')">ACQUIRE</button>
<button class="transport" onclick="submitTarget('RECORD')">RECORD</button>
<p>Commands are explicit target states, never blind GUI toggles.</p></div>
<div class="card"><h2>Request timeline</h2><pre id="timeline">No requests.</pre></div>
</main>
<script>
let latest=null;
const timeline=[];
function headers(){return {Authorization:'Bearer '+document.getElementById('token').value,'Content-Type':'application/json'}}
async function refresh(){
 const r=await fetch('/v1/status',{headers:headers()});
 const body=await r.json();
 if(!r.ok){document.getElementById('headline').textContent=body.error||'Connection failed';return}
 latest=body;
 document.getElementById('status').textContent=JSON.stringify(body,null,2);
 const h=document.getElementById('headline');
 h.className=body.online?'good':'bad';
 h.textContent=body.online?`${body.mode} · revision ${body.revision}`:'OFFLINE';
 const safety=document.getElementById('safety');
 const text=document.getElementById('safetyText');
 safety.className=body.mutation_allowed?'card':'card warn';
 text.textContent=body.mutation_allowed?'Mutation armed for validated simulation.':`OBSERVE ONLY · ${body.mutation_disabled_reason}`;
 document.querySelectorAll('.transport').forEach(b=>b.disabled=!body.mutation_allowed);
}
async function submitTarget(target){
 if(!latest||!latest.mutation_allowed)return;
 const request={request_id:crypto.randomUUID(),target_mode:target,expected_revision:latest.revision};
 const r=await fetch('/v1/transport/requests',{method:'POST',headers:headers(),body:JSON.stringify(request)});
 const body=await r.json();
 timeline.unshift({time:new Date().toISOString(),http:r.status,...body});
 document.getElementById('timeline').textContent=JSON.stringify(timeline,null,2);
 await refresh();
}
setInterval(()=>{if(document.getElementById('token').value)refresh()},1000);
</script>
</html>"""
