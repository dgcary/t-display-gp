#include "HomeAssistantConfigPortal.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>

#include <cstdlib>
#include <string>

#include "HomeAssistantConfigStore.h"

namespace {
const char HA_HTML[] PROGMEM = R"html(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Home Assistant</title><style>body{font-family:system-ui;background:#111;color:#eee;margin:0;padding:18px}main{max-width:620px;margin:auto}.card{background:#1d1d1d;border-radius:14px;padding:16px;margin-bottom:14px}h1{font-size:21px;margin:0 0 10px}label{display:block;color:#aaa;font-size:13px;margin-top:10px}input,textarea,button{box-sizing:border-box;width:100%;padding:10px;font-size:15px;border:1px solid #444;border-radius:8px;background:#151515;color:#fff}.row{display:grid;grid-template-columns:1.2fr 1fr;gap:8px}.check{display:flex;gap:8px;align-items:center;color:#eee}.check input{width:auto}textarea{min-height:150px;font-family:monospace}.ok{color:#86d386}.warn{color:#ffcc66}button{margin-top:14px;background:#eee;color:#111;font-weight:700}</style></head><body><main><div class="card"><h1>Home Assistant 只读面板</h1><p>设备到 Home Assistant 强制 HTTPS CA 校验。此配置页位于局域网 HTTP 8081 端口，请仅在可信 LAN 使用。</p><div id="secret"></div></div><form id="cfg"><div class="card"><label class="check"><input type="checkbox" name="enabled" value="1">启用 Home Assistant</label><label>HTTPS Base URL</label><input name="base_url" placeholder="https://home.example.com:8123"><label>刷新秒数（30-300）</label><input name="refresh_sec" type="number" min="30" max="300" value="30"></div><div class="card"><h1>实体（1-4）</h1><div id="rows"></div></div><div class="card"><h1>凭据</h1><label>Long-Lived Access Token（留空=保留当前值）</label><input name="token" type="password" autocomplete="new-password"><label>CA PEM（留空=保留当前值）</label><textarea name="ca_cert" spellcheck="false" placeholder="-----BEGIN CERTIFICATE-----"></textarea><label class="check"><input type="checkbox" name="clear_secrets" value="1">清空已保存 Token/CA（启用 HA 时不可用）</label><button type="submit">保存并重启</button><div id="msg"></div></div></form></main><script>const rows=document.getElementById('rows');for(let i=1;i<=4;i++)rows.insertAdjacentHTML('beforeend',`<label>实体 ${i}</label><div class="row"><input name="entity${i}" placeholder="sensor.temperature"><input name="label${i}" placeholder="显示名称（可选）"></div>`);async function load(){const s=await (await fetch('/api/ha/status')).json();cfg.enabled.checked=!!s.enabled;cfg.base_url.value=s.base_url||'';cfg.refresh_sec.value=String(s.refresh_sec||30);(s.entities||[]).forEach((x,i)=>{cfg[`entity${i+1}`].value=x.entity_id;cfg[`label${i+1}`].value=x.label||''});secret.innerHTML=`Token: <span class="${s.ha_token_set?'ok':'warn'}">${s.ha_token_set?'已设置':'未设置'}</span>　CA: <span class="${s.ha_ca_set?'ok':'warn'}">${s.ha_ca_set?'已设置':'未设置'}</span>`}cfg.onsubmit=async e=>{e.preventDefault();msg.textContent='保存中...';const r=await fetch('/api/ha/config',{method:'POST',body:new URLSearchParams(new FormData(cfg))});const j=await r.json();msg.textContent=j.message||'完成'};load();</script></body></html>)html";

std::string trimTrailingSlash(std::string value) {
  while (value.size() > 8 && value.back() == '/') value.pop_back();
  return value;
}

bool parseRefresh(const String& input, uint32_t& out) {
  char* end = nullptr;
  const unsigned long value = std::strtoul(input.c_str(), &end, 10);
  if (input.isEmpty() || end == input.c_str() || *end != '\0' || value < 30 || value > 300) return false;
  out = static_cast<uint32_t>(value);
  return true;
}
}

struct HomeAssistantConfigPortal::Impl {
  WebServer server{8081};
  HomeAssistantConfigStore store;
  HomeAssistantConfig* config = nullptr;
  bool started = false;
  bool restartScheduled = false;
  uint32_t restartAtMs = 0;

  void sendStatus() {
    DynamicJsonDocument doc(2048);
    const HomeAssistantConfig current = config ? *config : HomeAssistantConfig{};
    doc["enabled"] = current.enabled;
    doc["base_url"] = current.baseUrl;
    doc["refresh_sec"] = current.refreshSeconds;
    doc["ha_token_set"] = !current.token.empty();
    doc["ha_ca_set"] = !current.caCert.empty();
    JsonArray entities = doc.createNestedArray("entities");
    for (size_t i = 0; i < current.entityCount; ++i) {
      JsonObject entity = entities.createNestedObject();
      entity["entity_id"] = current.entities[i].entityId;
      entity["label"] = current.entities[i].label;
    }
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json; charset=utf-8", body);
  }

  void saveFromRequest() {
    if (!config) {
      server.send(503, "application/json", "{\"message\":\"HA 配置服务未就绪\"}");
      return;
    }
    HomeAssistantConfig submitted = *config;
    submitted.enabled = server.hasArg("enabled");
    submitted.baseUrl = trimTrailingSlash(server.arg("base_url").c_str());
    if (!parseRefresh(server.arg("refresh_sec"), submitted.refreshSeconds)) {
      server.send(400, "application/json", "{\"message\":\"刷新间隔必须是 30-300 秒\"}");
      return;
    }
    submitted.entityCount = 0;
    for (size_t i = 0; i < submitted.entities.size(); ++i) {
      const String rawEntity = server.arg(("entity" + std::to_string(i + 1)).c_str());
      const String rawLabel = server.arg(("label" + std::to_string(i + 1)).c_str());
      if (rawEntity.isEmpty() && rawLabel.isEmpty()) continue;
      if (rawEntity.isEmpty()) {
        server.send(400, "application/json", "{\"message\":\"实体名称不能为空\"}");
        return;
      }
      submitted.entities[submitted.entityCount].entityId.assign(rawEntity.c_str(), rawEntity.length());
      submitted.entities[submitted.entityCount].label.assign(rawLabel.c_str(), rawLabel.length());
      ++submitted.entityCount;
    }
    if (server.hasArg("clear_secrets")) {
      submitted.token.clear();
      submitted.caCert.clear();
    } else {
      const String token = server.arg("token");
      const String caCert = server.arg("ca_cert");
      if (!token.isEmpty()) submitted.token.assign(token.c_str(), token.length());
      if (!caCert.isEmpty()) submitted.caCert.assign(caCert.c_str(), caCert.length());
    }
    const HomeAssistantConfigValidationResult validation = validateHomeAssistantConfig(submitted);
    if (!validation.ok()) {
      const String body = String("{\"message\":\"") + validation.message() + "\"}";
      server.send(400, "application/json", body);
      return;
    }
    if (!store.save(submitted)) {
      server.send(500, "application/json", "{\"message\":\"HA 配置保存失败\"}");
      return;
    }
    server.send(200, "application/json; charset=utf-8", "{\"message\":\"已保存，设备将重启\"}");
    restartScheduled = true;
    restartAtMs = millis() + 350U;
  }
};

HomeAssistantConfigPortal::HomeAssistantConfigPortal() : impl_(new Impl()) {}
HomeAssistantConfigPortal::~HomeAssistantConfigPortal() = default;

void HomeAssistantConfigPortal::begin(HomeAssistantConfig& config) {
  impl_->config = &config;
  if (impl_->started) return;
  impl_->server.on("/", HTTP_GET, [this]() { impl_->server.send_P(200, PSTR("text/html; charset=utf-8"), HA_HTML); });
  impl_->server.on("/api/ha/status", HTTP_GET, [this]() { impl_->sendStatus(); });
  impl_->server.on("/api/ha/config", HTTP_POST, [this]() { impl_->saveFromRequest(); });
  impl_->server.onNotFound([this]() { impl_->server.send(404, "application/json", "{\"message\":\"not found\"}"); });
  impl_->server.begin();
  impl_->started = true;
  Serial.printf("[ha] config portal=http://%s:8081/ ha_token_set=%s ha_ca_set=%s\n",
                WiFi.localIP().toString().c_str(), config.token.empty() ? "no" : "yes",
                config.caCert.empty() ? "no" : "yes");
}

void HomeAssistantConfigPortal::process() {
  if (impl_->started) impl_->server.handleClient();
  if (impl_->restartScheduled && static_cast<int32_t>(millis() - impl_->restartAtMs) >= 0) ESP.restart();
}
