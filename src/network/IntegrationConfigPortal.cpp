#include "IntegrationConfigPortal.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

#include "BambuCloudProtocol.h"
#include "BambuPortalModel.h"
#include "HomeAssistantConfigStore.h"

namespace {
const char INTEGRATIONS_HTML[] PROGMEM = R"html(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>T-Display Integrations</title><style>body{font-family:system-ui;background:#111;color:#eee;margin:0;padding:18px}main{max-width:760px;margin:auto}.card{background:#1d1d1d;border-radius:14px;padding:16px;margin-bottom:14px}h1{font-size:21px;margin:0 0 10px}h2{font-size:18px;margin:0 0 8px}h3{font-size:15px;margin:16px 0 4px}p{line-height:1.5;color:#bbb}label{display:block;color:#aaa;font-size:13px;margin-top:10px}input,textarea,select,button{box-sizing:border-box;width:100%;padding:10px;font-size:15px;border:1px solid #444;border-radius:8px;background:#151515;color:#fff}.row{display:grid;grid-template-columns:1fr 1.4fr;gap:8px}.check{display:flex;gap:8px;align-items:center;color:#eee}.check input{width:auto}textarea{min-height:140px;font-family:monospace}.actions{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:14px}button{background:#eee;color:#111;font-weight:700}button.secondary{background:#333;color:#eee}button.danger{background:#6b2525;color:#fff;margin-top:8px}.status{font-family:monospace;font-size:13px;white-space:pre-wrap;margin-top:10px}.hint{font-size:13px;color:#b9a775}.printer{border-top:1px solid #333;margin-top:12px;padding-top:8px}@media(max-width:520px){.row,.actions{grid-template-columns:1fr}}</style></head><body><main><div class="card"><h1>Integrations</h1><p>配置页运行在设备局域网 HTTP 8081 端口，只应在可信 LAN 使用。Bambu Access Token 属于登录凭据，粘贴时会以明文经过当前局域网；设备到 Bambu Cloud 的 HTTPS/MQTT 始终使用 CA 校验 TLS。</p></div><form id="ha"><div class="card"><h2>Home Assistant</h2><label class="check"><input type="checkbox" name="enabled" value="1">启用 Home Assistant</label><label>Base URL</label><input name="base_url" placeholder="http://homeassistant.local:8123"><label>刷新秒数（30-300）</label><input name="refresh_sec" type="number" min="30" max="300" value="30"><h3>实体（1-4）</h3><div id="haRows"></div><label>Long-Lived Access Token（留空=保留当前值）</label><input name="token" type="password" autocomplete="new-password"><label>CA PEM（仅 HTTPS 必需；留空=保留当前值）</label><textarea name="ca_cert" spellcheck="false" placeholder="-----BEGIN CERTIFICATE-----"></textarea><label class="check"><input type="checkbox" name="clear_secrets" value="1">清空已保存 Token/CA</label><button type="submit">保存 HA 并重启</button><div id="haMsg"></div></div></form><form id="bambu"><div class="card"><h2>Bambu Lab Cloud</h2><label class="check"><input type="checkbox" name="enabled" value="1">启用 Bambu Lab</label><label>区域</label><select name="region"><option value="us_eu">US / EU / Global</option><option value="china">China</option></select><label>Access Token（留空=保留已保存 Token；不会从设备回显）</label><input name="access_token" type="password" autocomplete="new-password" spellcheck="false"><p class="hint">先在浏览器登录对应区域的 Bambu 网站，从开发者工具 Cookies 中复制 token。Token 不会显示在状态接口或串口。</p><div id="printerRows"></div><label>当前打印机</label><select name="active_printer_serial" id="activePrinter"><option value="">请选择打印机</option></select><div class="actions"><button type="button" class="secondary" id="bDiscover">用 Token 获取我的打印机</button><button type="submit">保存并切换</button></div><button type="button" class="danger" id="bLogout">清除 Bambu 凭据</button><div id="bStatus" class="status"></div><div id="bMsg"></div></div></form></main><script>const haRows=document.getElementById('haRows');for(let i=1;i<=4;i++)haRows.insertAdjacentHTML('beforeend',`<label>实体 ${i}</label><div class="row"><input name="entity${i}" placeholder="sensor.temperature"><input name="label${i}" placeholder="显示名称（可选）"></div>`);const printerRows=document.getElementById('printerRows');for(let i=1;i<=4;i++)printerRows.insertAdjacentHTML('beforeend',`<div class="printer"><h3>打印机 ${i}</h3><div class="row"><div><label>名称</label><input name="printer${i}_name" placeholder="例如 P1S"></div><div><label>Serial</label><input name="printer${i}_serial" autocomplete="off" spellcheck="false"></div></div></div>`);async function loadHa(){const s=await(await fetch('/api/ha/status')).json();ha.enabled.checked=!!s.enabled;ha.base_url.value=s.base_url||'';ha.refresh_sec.value=String(s.refresh_sec||30);(s.entities||[]).forEach((x,i)=>{if(i<4){ha[`entity${i+1}`].value=x.entity_id;ha[`label${i+1}`].value=x.label||''}});haMsg.textContent=`Token ${s.ha_token_set?'已设置':'未设置'} / CA ${s.ha_ca_set?'已设置':'未设置'}`};ha.onsubmit=async e=>{e.preventDefault();haMsg.textContent='保存中...';const r=await fetch('/api/ha/config',{method:'POST',body:new URLSearchParams(new FormData(ha))});const j=await r.json();haMsg.textContent=j.message||'完成'};function printerRowsFromForm(){const rows=[];for(let i=1;i<=4;i++){const serial=bambu[`printer${i}_serial`].value.trim();const name=bambu[`printer${i}_name`].value.trim();if(serial)rows.push({serial,name})}return rows}function syncActiveChoices(selected=''){const sel=document.getElementById('activePrinter');const old=selected||sel.value;sel.innerHTML='<option value="">请选择打印机</option>';for(const p of printerRowsFromForm()){const o=document.createElement('option');o.value=p.serial;o.textContent=p.name?`${p.name} (${p.serial})`:p.serial;if(p.serial===old)o.selected=true;sel.appendChild(o)}if(!sel.value&&sel.options.length>1)sel.selectedIndex=1}for(let i=1;i<=4;i++){bambu[`printer${i}_serial`].addEventListener('input',()=>syncActiveChoices());bambu[`printer${i}_name`].addEventListener('input',()=>syncActiveChoices())}function renderBambuStatus(s){bStatus.textContent=`状态: ${s.session}\nMQTT: ${s.mqtt_connected?'online':'offline'}\nToken: ${s.token_set?'已保存':'未保存'}\n本地打印机: ${s.printer_count||0}\n当前: ${s.active_printer_name||s.active_printer_serial||'--'}${s.last_mqtt_rc!==undefined&&s.last_mqtt_rc!==-1?`\nMQTT rc: ${s.last_mqtt_rc}`:''}`};async function refreshBambuStatus(){try{const s=await(await fetch('/api/bambu/status')).json();renderBambuStatus(s)}catch(_){}}async function loadBambu(){const s=await(await fetch('/api/bambu/status')).json();bambu.enabled.checked=!!s.enabled;bambu.region.value=s.region||'us_eu';renderBambuStatus(s);const p=await(await fetch('/api/bambu/printers')).json();for(let i=1;i<=4;i++){bambu[`printer${i}_serial`].value='';bambu[`printer${i}_name`].value=''};(p.printers||[]).slice(0,4).forEach((x,i)=>{bambu[`printer${i+1}_serial`].value=x.serial||'';bambu[`printer${i+1}_name`].value=x.name||''});syncActiveChoices(s.active_printer_serial||'')};bDiscover.onclick=async()=>{bMsg.textContent='正在获取打印机...';const body=new URLSearchParams();body.set('region',bambu.region.value);body.set('access_token',bambu.access_token.value);const r=await fetch('/api/bambu/discover',{method:'POST',body});const j=await r.json();if(!r.ok){bMsg.textContent=j.message||'获取失败';return}for(let i=1;i<=4;i++){bambu[`printer${i}_serial`].value='';bambu[`printer${i}_name`].value=''};(j.printers||[]).slice(0,4).forEach((x,i)=>{bambu[`printer${i+1}_serial`].value=x.serial||'';bambu[`printer${i+1}_name`].value=x.name||''});syncActiveChoices();bMsg.textContent=`获取到 ${j.printers?.length||0} 台打印机；请确认当前打印机后点击“保存并切换”`};bambu.onsubmit=async e=>{e.preventDefault();bMsg.textContent='保存中...';const r=await fetch('/api/bambu/config',{method:'POST',body:new URLSearchParams(new FormData(bambu))});const j=await r.json();bMsg.textContent=j.message||'完成';if(r.ok){bambu.access_token.value='';await loadBambu()}};bLogout.onclick=async()=>{if(!confirm('清除 Bambu Token、Cloud User ID 和本地打印机列表？'))return;const r=await fetch('/api/bambu/logout',{method:'POST'});const j=await r.json();bMsg.textContent=j.message||'完成';if(r.ok){bambu.access_token.value='';await loadBambu()}};loadHa();loadBambu();setInterval(refreshBambuStatus,2000);</script></body></html>)html";

std::string trimTrailingSlash(std::string value) {
  while (value.size() > 8 && value.back() == '/') value.pop_back();
  return value;
}

void secureErase(std::string& value) {
  if (!value.empty()) {
    volatile char* data = &value[0];
    for (size_t i = 0; i < value.size(); ++i) data[i] = '\0';
  }
  value.clear();
}

std::string trimmedArg(WebServer& server, const char* name) {
  String value = server.arg(name);
  value.trim();
  return std::string(value.c_str(), value.length());
}

bool parseRefresh(const String& input, uint32_t& out) {
  char* end = nullptr;
  const unsigned long value = std::strtoul(input.c_str(), &end, 10);
  if (input.isEmpty() || end == input.c_str() || *end != '\0' || value < 30 || value > 300) return false;
  out = static_cast<uint32_t>(value);
  return true;
}

BambuRegion parseRegion(const String& value) {
  return value == "china" ? BambuRegion::CHINA : BambuRegion::US_EU;
}

const char* regionName(BambuRegion region) {
  return region == BambuRegion::CHINA ? "china" : "us_eu";
}

const char* sessionName(BambuSessionState state) {
  switch (state) {
    case BambuSessionState::INTEGRATION_DISABLED: return "disabled";
    case BambuSessionState::UNCONFIGURED: return "unconfigured";
    case BambuSessionState::PRINTER_SELECTION_REQUIRED: return "printer_selection_required";
    case BambuSessionState::MQTT_CONNECTING: return "mqtt_connecting";
    case BambuSessionState::ONLINE: return "online";
    case BambuSessionState::TOKEN_INVALID: return "token_invalid";
    case BambuSessionState::RELOGIN_PENDING: return "relogin_pending";
    case BambuSessionState::RELOGIN_IN_PROGRESS: return "relogin_in_progress";
    case BambuSessionState::VERIFICATION_REQUIRED: return "verification_required";
    case BambuSessionState::LOGIN_FAILED: return "login_failed";
    case BambuSessionState::NETWORK_ERROR: return "network_error";
    case BambuSessionState::BUFFER_ERROR: return "buffer_error";
  }
  return "unknown";
}

const char* cloudErrorMessage(BambuCloudError error) {
  switch (error) {
    case BambuCloudError::NONE: return "ok";
    case BambuCloudError::NETWORK: return "网络连接失败";
    case BambuCloudError::TLS: return "TLS 校验失败";
    case BambuCloudError::HTTP_STATUS: return "Bambu Cloud 返回错误状态";
    case BambuCloudError::BODY_TOO_LARGE: return "Bambu Cloud 响应过大";
    case BambuCloudError::TRUNCATED_BODY: return "Bambu Cloud 响应不完整";
    case BambuCloudError::MALFORMED: return "Bambu Cloud 响应格式异常";
    case BambuCloudError::INVALID_CREDENTIALS: return "Access Token 无效";
    case BambuCloudError::USER_ID_UNAVAILABLE: return "无法从 Token 获取 Cloud User ID";
  }
  return "Bambu Cloud 错误";
}

const char* configErrorMessage(BambuConfigError error) {
  switch (error) {
    case BambuConfigError::NONE: return "ok";
    case BambuConfigError::TOKEN_REQUIRED: return "启用 Bambu 时请粘贴 Access Token";
    case BambuConfigError::USER_ID_REQUIRED: return "无法确定 Cloud User ID，请检查 Token";
    case BambuConfigError::PRINTER_REQUIRED: return "至少填写一台打印机 Serial";
    case BambuConfigError::TOKEN_TOO_LONG: return "Access Token 过长";
    case BambuConfigError::USER_ID_TOO_LONG: return "Cloud User ID 异常";
    case BambuConfigError::TOO_MANY_PRINTERS: return "最多保存 4 台打印机";
    case BambuConfigError::SERIAL_REQUIRED: return "打印机 Serial 不能为空";
    case BambuConfigError::SERIAL_INVALID: return "打印机 Serial 格式无效";
    case BambuConfigError::SERIAL_TOO_LONG: return "打印机 Serial 过长";
    case BambuConfigError::NAME_TOO_LONG: return "打印机名称过长";
    case BambuConfigError::DUPLICATE_SERIAL: return "打印机 Serial 不能重复";
    case BambuConfigError::ACTIVE_PRINTER_INVALID: return "当前打印机选择无效";
    case BambuConfigError::ENCODED_TOO_LONG: return "Bambu 配置过大";
    case BambuConfigError::MALFORMED: return "Bambu 配置格式错误";
  }
  return "Bambu 配置无效";
}
}  // namespace

struct IntegrationConfigPortal::Impl {
  WebServer server{8081};
  HomeAssistantConfigStore haStore;
  HomeAssistantConfig* haConfig = nullptr;
  BambuCloudClient* bambuCloud = nullptr;
  BambuMqttService* bambuService = nullptr;
  bool started = false;
  bool restartScheduled = false;
  uint32_t restartAtMs = 0U;

  void scheduleRestart() {
    restartScheduled = true;
    restartAtMs = millis() + 450U;
  }

  void sendHaStatus() {
    DynamicJsonDocument doc(2048);
    const HomeAssistantConfig current = haConfig ? *haConfig : HomeAssistantConfig{};
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

  void saveHa() {
    if (!haConfig) {
      server.send(503, "application/json", "{\"message\":\"HA 配置服务未就绪\"}");
      return;
    }
    HomeAssistantConfig submitted = *haConfig;
    submitted.enabled = server.hasArg("enabled");
    submitted.baseUrl = trimTrailingSlash(server.arg("base_url").c_str());
    if (!parseRefresh(server.arg("refresh_sec"), submitted.refreshSeconds)) {
      server.send(400, "application/json", "{\"message\":\"刷新间隔必须是 30-300 秒\"}");
      return;
    }
    submitted.entityCount = 0;
    for (size_t i = 0; i < submitted.entities.size(); ++i) {
      String rawEntity = server.arg(("entity" + std::to_string(i + 1)).c_str());
      String rawLabel = server.arg(("label" + std::to_string(i + 1)).c_str());
      rawEntity.trim();
      rawLabel.trim();
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
      server.send(400, "application/json; charset=utf-8", body);
      return;
    }
    if (!haStore.save(submitted)) {
      server.send(500, "application/json", "{\"message\":\"HA 配置保存失败\"}");
      return;
    }
    *haConfig = submitted;
    server.send(200, "application/json; charset=utf-8", "{\"message\":\"HA 已保存，设备将重启\"}");
    scheduleRestart();
  }

  void sendBambuStatus() {
    const BambuConfig current = bambuService ? bambuService->configSnapshot() : BambuConfig{};
    const BambuPortalStatus publicStatus = buildBambuPortalStatus(current);
    const BambuMqttStatus serviceStatus = bambuService ? bambuService->status() : BambuMqttStatus{};
    DynamicJsonDocument doc(1536);
    doc["enabled"] = publicStatus.enabled;
    doc["region"] = regionName(publicStatus.region);
    doc["token_set"] = publicStatus.tokenSet;
    doc["printer_count"] = publicStatus.printerCount;
    doc["active_printer_serial"] = publicStatus.activePrinterSerial;
    doc["active_printer_name"] = publicStatus.activePrinterName;
    doc["mqtt_connected"] = serviceStatus.mqttConnected;
    doc["session"] = sessionName(serviceStatus.session);
    doc["last_mqtt_rc"] = serviceStatus.lastMqttRc;
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json; charset=utf-8", body);
  }

  void sendSavedBambuPrinters() {
    const BambuConfig current = bambuService ? bambuService->configSnapshot() : BambuConfig{};
    DynamicJsonDocument doc(3072);
    const BambuPrinterConfig* active = activeBambuPrinter(current);
    doc["active_printer_serial"] = active ? active->serial : "";
    JsonArray list = doc.createNestedArray("printers");
    for (size_t i = 0; i < current.printerCount; ++i) {
      JsonObject item = list.createNestedObject();
      item["serial"] = current.printers[i].serial;
      item["name"] = current.printers[i].name;
    }
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json; charset=utf-8", body);
  }

  void discoverBambuPrinters() {
    if (!bambuService || !bambuCloud) {
      server.send(503, "application/json", "{\"message\":\"Bambu 配置服务未就绪\"}");
      return;
    }
    const BambuConfig existing = bambuService->configSnapshot();
    const BambuRegion requestedRegion = parseRegion(server.arg("region"));
    std::string token = trimmedArg(server, "access_token");
    if (token.empty()) {
      if (requestedRegion != existing.region || existing.accessToken.empty()) {
        server.send(400, "application/json; charset=utf-8", "{\"message\":\"请先粘贴该区域的 Access Token\"}");
        return;
      }
      token = existing.accessToken;
    }

    const BambuCloudPrintersResult found = bambuCloud->fetchPrinters(token, requestedRegion);
    secureErase(token);
    if (!found.ok()) {
      DynamicJsonDocument doc(512);
      doc["message"] = cloudErrorMessage(found.error);
      String body;
      serializeJson(doc, body);
      server.send(found.error == BambuCloudError::INVALID_CREDENTIALS ? 401 : 502,
                  "application/json; charset=utf-8", body);
      return;
    }

    DynamicJsonDocument doc(4096);
    JsonArray list = doc.createNestedArray("printers");
    const size_t count = std::min(found.printers.size(), BambuConfigLimits::PRINTER_COUNT);
    for (size_t i = 0; i < count; ++i) {
      JsonObject item = list.createNestedObject();
      item["serial"] = found.printers[i].serial;
      item["name"] = found.printers[i].name;
    }
    doc["truncated"] = found.printers.size() > BambuConfigLimits::PRINTER_COUNT;
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json; charset=utf-8", body);
  }

  void saveBambu() {
    if (!bambuService || !bambuCloud) {
      server.send(503, "application/json", "{\"message\":\"Bambu 配置服务未就绪\"}");
      return;
    }
    const BambuConfig existing = bambuService->configSnapshot();
    BambuPortalConfigInput input;
    input.enabled = server.hasArg("enabled");
    input.region = parseRegion(server.arg("region"));
    input.accessToken = trimmedArg(server, "access_token");
    input.activePrinterSerial = trimmedArg(server, "active_printer_serial");

    for (size_t slot = 0; slot < BambuConfigLimits::PRINTER_COUNT; ++slot) {
      const std::string prefix = "printer" + std::to_string(slot + 1);
      std::string serial = trimmedArg(server, (prefix + "_serial").c_str());
      std::string name = trimmedArg(server, (prefix + "_name").c_str());
      if (serial.empty() && !name.empty()) {
        secureErase(input.accessToken);
        server.send(400, "application/json; charset=utf-8", "{\"message\":\"填写打印机名称时必须同时填写 Serial\"}");
        return;
      }
      if (serial.empty()) continue;
      BambuPrinterConfig& printer = input.printers[input.printerCount++];
      printer.serial = std::move(serial);
      printer.name = std::move(name);
    }

    BambuConfig updated = mergeBambuPortalConfig(existing, input);
    secureErase(input.accessToken);

    if (!updated.accessToken.empty() && updated.cloudUserId.empty()) {
      std::string userId;
      if (!extractBambuUserIdFromJwt(updated.accessToken, userId)) {
        const BambuCloudUserIdResult cloudUser = bambuCloud->fetchUserId(updated.accessToken, updated.region);
        if (!cloudUser.ok()) {
          DynamicJsonDocument doc(512);
          doc["message"] = cloudErrorMessage(cloudUser.error);
          String body;
          serializeJson(doc, body);
          server.send(cloudUser.error == BambuCloudError::INVALID_CREDENTIALS ? 401 : 502,
                      "application/json; charset=utf-8", body);
          return;
        }
        userId = cloudUser.userId;
      }
      updated.cloudUserId = std::move(userId);
    }

    const BambuConfigValidationResult validation = validateBambuConfig(updated);
    if (!validation.ok()) {
      DynamicJsonDocument doc(512);
      doc["message"] = configErrorMessage(validation.error);
      String body;
      serializeJson(doc, body);
      server.send(400, "application/json; charset=utf-8", body);
      return;
    }
    if (!bambuService->replaceConfig(updated)) {
      server.send(500, "application/json", "{\"message\":\"Bambu 配置保存失败\"}");
      return;
    }

    const BambuPrinterConfig* active = activeBambuPrinter(updated);
    DynamicJsonDocument doc(512);
    const std::string label = active ? (active->name.empty() ? active->serial : active->name) : std::string("--");
    doc["message"] = std::string("已保存，正在切换到 ") + label;
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json; charset=utf-8", body);
  }

  void logoutBambu() {
    if (!bambuService) {
      server.send(503, "application/json", "{\"message\":\"Bambu 配置服务未就绪\"}");
      return;
    }
    const BambuConfig cleared = clearBambuPortalCredentials(bambuService->configSnapshot());
    if (!bambuService->replaceConfig(cleared)) {
      server.send(500, "application/json", "{\"message\":\"清除 Bambu 凭据失败\"}");
      return;
    }
    server.send(200, "application/json; charset=utf-8", "{\"message\":\"Bambu Token 和本地打印机列表已清除\"}");
  }
};

IntegrationConfigPortal::IntegrationConfigPortal() : impl_(new Impl()) {}
IntegrationConfigPortal::~IntegrationConfigPortal() = default;

void IntegrationConfigPortal::begin(HomeAssistantConfig& homeAssistantConfig,
                                    BambuCloudClient& bambuCloud,
                                    BambuMqttService& bambuService) {
  impl_->haConfig = &homeAssistantConfig;
  impl_->bambuCloud = &bambuCloud;
  impl_->bambuService = &bambuService;
  if (impl_->started) return;

  impl_->server.on("/", HTTP_GET, [this]() {
    impl_->server.send_P(200, PSTR("text/html; charset=utf-8"), INTEGRATIONS_HTML);
  });
  impl_->server.on("/api/ha/status", HTTP_GET, [this]() { impl_->sendHaStatus(); });
  impl_->server.on("/api/ha/config", HTTP_POST, [this]() { impl_->saveHa(); });
  impl_->server.on("/api/bambu/status", HTTP_GET, [this]() { impl_->sendBambuStatus(); });
  impl_->server.on("/api/bambu/printers", HTTP_GET, [this]() { impl_->sendSavedBambuPrinters(); });
  impl_->server.on("/api/bambu/discover", HTTP_POST, [this]() { impl_->discoverBambuPrinters(); });
  impl_->server.on("/api/bambu/config", HTTP_POST, [this]() { impl_->saveBambu(); });
  impl_->server.on("/api/bambu/logout", HTTP_POST, [this]() { impl_->logoutBambu(); });
  impl_->server.onNotFound([this]() {
    impl_->server.send(404, "application/json", "{\"message\":\"not found\"}");
  });
  impl_->server.begin();
  impl_->started = true;
  const BambuConfig current = bambuService.configSnapshot();
  Serial.printf("[integrations] portal=http://%s:8081/ ha_token_set=%s bambu_token_set=%s printers=%u\n",
                WiFi.localIP().toString().c_str(),
                homeAssistantConfig.token.empty() ? "no" : "yes",
                current.accessToken.empty() ? "no" : "yes",
                static_cast<unsigned>(current.printerCount));
}

void IntegrationConfigPortal::process() {
  if (impl_->started) impl_->server.handleClient();
  if (impl_->restartScheduled && static_cast<int32_t>(millis() - impl_->restartAtMs) >= 0) ESP.restart();
}
