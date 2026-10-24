#include "panel_actuator_web.h"
#include "panel_ui.h"
#include "bsp/esp-bsp.h"
#include <string.h>

static const char page[] = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Actuators</title>
<style>body{font:18px system-ui;background:#d9e2ed;color:#303247;margin:0}header{background:#303247;color:white;padding:24px}h1{font-size:26px;margin:0}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(290px,1fr));gap:16px;padding:20px;max-width:1100px;margin:auto}article{background:white;padding:24px;border-radius:16px;box-shadow:0 3px 3px #68758b30}h2{font-size:22px;margin:0 0 14px}.position{font-size:32px}progress{width:100%;accent-color:#5356c9}footer{display:flex;justify-content:space-between;margin-top:16px}small{color:#68758b}#status{margin-top:8px}</style>
<header><h1>Actuators</h1><div id="status">Connecting...</div><small style="color:#ddd">Live status / view only</small></header><main id="cards"></main>
<script>const cards=document.querySelector('#cards'),status=document.querySelector('#status');
function text(parent,tag,value,cls){let e=document.createElement(tag);e.textContent=value;if(cls)e.className=cls;parent.append(e);return e}
async function update(){try{const r=await fetch('/actuators/v1/state',{cache:'no-store'});if(!r.ok)throw Error();const s=await r.json();status.textContent=(s.preview?'Preview / ':'')+(!s.actuator_mode?'CNC mode':!s.online?'Not connected':s.state)+(s.alarm?' / Alarm '+s.alarm:s.error?' / Error '+s.error:'');cards.replaceChildren();for(const a of s.actuators){let c=document.createElement('article');cards.append(c);text(c,'h2',a.name+' / '+a.axis);text(c,'div',!a.available?'Axis unavailable':!a.homed?'Home to establish position':a.position.toFixed(3)+' '+a.unit,'position');let p=document.createElement('progress');p.max=100;p.value=a.position_percent??0;p.setAttribute('aria-label',a.name+' position');c.append(p);let f=document.createElement('footer');c.append(f);text(f,'span',a.first_label);text(f,'span',a.second_label);text(c,'small',a.speed.toFixed(3)+' '+a.unit+'/min')}}catch{status.textContent='Connection lost'}finally{setTimeout(update,1000)}}update();</script></html>)HTML";

static esp_err_t handle(httpd_req_t *request) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (!strcmp(request->uri, "/actuators")) {
        httpd_resp_set_type(request, "text/html; charset=utf-8");
        return httpd_resp_send(request, page, HTTPD_RESP_USE_STRLEN);
    }
    char json[3072];
    if (!bsp_display_lock(500)) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "Display busy");
    }
    bool ok = panel_ui_actuator_json(json, sizeof json);
    bsp_display_unlock();
    if (!ok)
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Snapshot unavailable");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json, HTTPD_RESP_USE_STRLEN);
}
void panel_actuator_web_register(httpd_handle_t server) {
    const char *paths[] = {"/actuators", "/actuators/v1/state"};
    for (const char *path : paths) {
        httpd_uri_t route = {};
        route.uri = path;
        route.method = HTTP_GET;
        route.handler = handle;
        httpd_register_uri_handler(server, &route);
    }
}
