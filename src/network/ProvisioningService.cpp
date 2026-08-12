#include "ProvisioningService.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include <array>
#include <cstdio>
#include <string>

#include "device/ConfigStore.h"
#include "ProvisioningForm.h"
#include "build_config.h"

namespace {
constexpr char AP_NAME[] = "TDisplay-GP-Setup";
constexpr char FORCE_PORTAL_KEY[] = "force_portal";

const char SETTINGS_HTML[] PROGMEM = R"html(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>T-Display GP</title><style>
body{font-family:system-ui,-apple-system,sans-serif;background:#111;color:#eee;margin:0;padding:18px}main{max-width:520px;margin:auto}
.card{background:#1d1d1d;border-radius:14px;padding:16px;margin-bottom:14px}h1{font-size:22px;margin:0 0 12px}label{display:block;font-size:13px;color:#aaa;margin-top:10px}
.row{display:grid;grid-template-columns:1fr 1fr;gap:8px}input,select,button{box-sizing:border-box;width:100%;font-size:16px;padding:11px;border-radius:9px;border:1px solid #444;background:#151515;color:#fff}
button{background:#eee;color:#111;font-weight:700;margin-top:14px}.danger{background:#5b2020;color:#fff}.msg{min-height:20px;color:#8fd18f}small{color:#999}
</style></head><body><main><div class="card"><h1>T-Display GP</h1><div id="wifi">读取状态...</div><small id="uptime"></small></div>
<form id="cfg" class="card"><h1>股票池</h1><div id="rows"></div><label>刷新间隔</label><select name="refresh"><option>3</option><option>4</option><option selected>5</option></select>
<button type="submit">保存配置</button><div class="msg" id="msg"></div></form>
<div class="card"><button class="danger" id="wifiBtn">更换 Wi-Fi</button><small>设备会重启并进入 TDisplay-GP-Setup 配网热点。</small></div></main>
<script>
const rows=document.getElementById('rows');for(let i=1;i<=5;i++){rows.insertAdjacentHTML('beforeend',`<label>股票 ${i}</label><div class="row"><input name="stock${i}" placeholder="600519"><input name="name${i}" placeholder="显示名称（可选）"></div>`)}
async function load(){let s=await (await fetch('/api/status')).json();wifi.textContent=`Wi-Fi: ${s.ip} / RSSI ${s.rssi} dBm`;uptime.textContent=`运行 ${Math.floor(s.uptime_ms/1000)} 秒`;let c=s.config||{};(c.stocks||[]).forEach((x,i)=>{if(i<5){cfg[`stock${i+1}`].value=x.symbol;cfg[`name${i+1}`].value=x.name||''}});if(c.quote_refresh_sec)cfg.refresh.value=String(c.quote_refresh_sec)}
cfg.onsubmit=async e=>{e.preventDefault();msg.textContent='保存中...';let r=await fetch('/api/config',{method:'POST',body:new URLSearchParams(new FormData(cfg))});let j=await r.json();msg.textContent=j.message|| (r.ok?'已保存':'保存失败');if(r.ok)load()};
wifiBtn.onclick=async()=>{if(!confirm('重启并重新配置 Wi-Fi？'))return;await fetch('/api/wifi/reconfigure',{method:'POST'});wifiBtn.textContent='设备正在重启...';wifiBtn.disabled=true};load();
</script></body></html>)html";

bool readForcePortal() {
  Preferences p;
  if (!p.begin(BuildConfig::CONFIG_NAMESPACE, true)) return false;
  const bool force = p.getBool(FORCE_PORTAL_KEY, false);
  p.end();
  return force;
}

void writeForcePortal(bool force) {
  Preferences p;
  if (!p.begin(BuildConfig::CONFIG_NAMESPACE, false)) return;
  p.putBool(FORCE_PORTAL_KEY, force);
  p.end();
}

std::string htmlEscape(const std::string& value) {
  std::string out;
  for (char c : value) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

}  // namespace

struct ProvisioningService::Impl {
  WebServer server{80};
  ConfigStore store;
  AppConfig* config = nullptr;
  bool webStarted = false;
  bool restartScheduled = false;
  uint32_t restartAtMs = 0;

  ProvisioningFields readWebFields() {
    ProvisioningFields fields;
    for (size_t i = 0; i < 5; ++i) {
      const String stock = server.arg(("stock" + std::to_string(i + 1)).c_str());
      const String name = server.arg(("name" + std::to_string(i + 1)).c_str());
      fields.symbols[i].assign(stock.c_str(), stock.length());
      fields.names[i].assign(name.c_str(), name.length());
    }
    const String refresh = server.arg("refresh");
    fields.refresh.assign(refresh.c_str(), refresh.length());
    return fields;
  }
};

ProvisioningService::ProvisioningService() : impl_(new Impl()) {}
ProvisioningService::~ProvisioningService() = default;

bool ProvisioningService::ensureConnected(AppConfig& config) {
  const bool haveValidConfig = impl_->store.load(config);
  ProvisioningFields fields = haveValidConfig ? ProvisioningForm::fromConfig(config) : ProvisioningFields{};
  bool forcePortal = readForcePortal() || !haveValidConfig;
  std::string portalError;

  while (true) {
    WiFiManager wm;
    wm.setDebugOutput(false);
    wm.setBreakAfterConfig(false);

    std::array<std::array<char, 16>, 5> symbolBuffers{};
    std::array<std::array<char, 31>, 5> nameBuffers{};
    std::array<WiFiManagerParameter*, 11> params{};
    std::array<std::string, 10> ids;
    std::array<std::string, 10> labels;
    for (size_t i = 0; i < 5; ++i) {
      std::snprintf(symbolBuffers[i].data(), symbolBuffers[i].size(), "%s", fields.symbols[i].c_str());
      std::snprintf(nameBuffers[i].data(), nameBuffers[i].size(), "%s", fields.names[i].c_str());
      ids[i * 2] = "stock" + std::to_string(i + 1);
      ids[i * 2 + 1] = "name" + std::to_string(i + 1);
      labels[i * 2] = "股票 " + std::to_string(i + 1) + " 代码";
      labels[i * 2 + 1] = "股票 " + std::to_string(i + 1) + " 名称(可选)";
      params[i * 2] = new WiFiManagerParameter(ids[i * 2].c_str(), labels[i * 2].c_str(), symbolBuffers[i].data(), 15);
      params[i * 2 + 1] = new WiFiManagerParameter(ids[i * 2 + 1].c_str(), labels[i * 2 + 1].c_str(), nameBuffers[i].data(), 30);
      wm.addParameter(params[i * 2]);
      wm.addParameter(params[i * 2 + 1]);
    }
    char refreshBuffer[4] = {};
    std::snprintf(refreshBuffer, sizeof(refreshBuffer), "%s", fields.refresh.c_str());
    WiFiManagerParameter refreshParam("refresh", "刷新间隔(3/4/5秒)", refreshBuffer, 3);
    wm.addParameter(&refreshParam);

    if (!portalError.empty()) {
      const std::string custom = "<div style='margin:8px;color:#b00020;font-weight:bold'>" + htmlEscape(portalError) + "</div>";
      wm.setCustomHeadElement(custom.c_str());
    }

    bool paramsSubmitted = false;
    wm.setSaveParamsCallback([&paramsSubmitted]() { paramsSubmitted = true; });
    const bool connected = forcePortal ? wm.startConfigPortal(AP_NAME) : wm.autoConnect(AP_NAME);

    if (paramsSubmitted) {
      for (size_t i = 0; i < 5; ++i) {
        fields.symbols[i] = params[i * 2]->getValue();
        fields.names[i] = params[i * 2 + 1]->getValue();
      }
      fields.refresh = refreshParam.getValue();
    }
    for (auto* param : params) delete param;

    AppConfig submitted;
    std::string validationError;
    const bool configOk = ProvisioningForm::buildConfig(fields, submitted, validationError);
    if (paramsSubmitted && configOk) {
      if (!impl_->store.save(submitted)) {
        portalError = "配置写入失败，请重试";
        forcePortal = true;
        continue;
      }
      config = std::move(submitted);
    } else if (!configOk) {
      portalError = validationError;
      forcePortal = true;
      continue;
    }

    if (!connected || WiFi.status() != WL_CONNECTED) return false;
    writeForcePortal(false);
    return true;
  }
}

void ProvisioningService::beginWebPortal(AppConfig& config) {
  impl_->config = &config;
  if (impl_->webStarted) return;

  impl_->server.on("/", HTTP_GET, [this]() {
    impl_->server.send_P(200, PSTR("text/html; charset=utf-8"), SETTINGS_HTML);
  });
  impl_->server.on("/api/status", HTTP_GET, [this]() {
    std::string encoded;
    const bool haveConfig = impl_->config && AppConfigCodec::encode(*impl_->config, encoded);
    String body = "{\"ip\":\"";
    body += WiFi.localIP().toString();
    body += "\",\"rssi\":" + String(WiFi.RSSI());
    body += ",\"uptime_ms\":" + String(millis());
    body += ",\"config\":";
    body += haveConfig ? encoded.c_str() : "null";
    body += "}";
    impl_->server.send(200, "application/json; charset=utf-8", body);
  });
  impl_->server.on("/api/config", HTTP_POST, [this]() {
    if (!impl_->config) {
      impl_->server.send(503, "application/json", "{\"message\":\"配置服务未就绪\"}");
      return;
    }
    AppConfig submitted;
    std::string error;
    if (!ProvisioningForm::buildConfig(impl_->readWebFields(), submitted, error)) {
      const String body = String("{\"message\":\"") + error.c_str() + "\"}";
      impl_->server.send(400, "application/json; charset=utf-8", body);
      return;
    }
    if (!impl_->store.save(submitted)) {
      impl_->server.send(500, "application/json", "{\"message\":\"保存失败\"}");
      return;
    }
    // V1 applies configuration atomically on reboot. Do not partially mutate
    // the running controller/UI configuration before all modules restart.
    impl_->server.send(200, "application/json; charset=utf-8", "{\"message\":\"已保存，设备将重启\"}");
    impl_->restartScheduled = true;
    impl_->restartAtMs = millis() + 350;
  });
  impl_->server.on("/api/wifi/reconfigure", HTTP_POST, [this]() {
    writeForcePortal(true);
    impl_->restartScheduled = true;
    impl_->restartAtMs = millis() + 350;
    impl_->server.send(202, "application/json", "{\"message\":\"设备将重启进入配网模式\"}");
  });
  impl_->server.onNotFound([this]() {
    impl_->server.send(404, "application/json", "{\"message\":\"not found\"}");
  });
  impl_->server.begin();
  impl_->webStarted = true;
}

void ProvisioningService::process() {
  if (impl_->webStarted) impl_->server.handleClient();
  if (impl_->restartScheduled && static_cast<int32_t>(millis() - impl_->restartAtMs) >= 0) {
    ESP.restart();
  }
}
