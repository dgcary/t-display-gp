#include "IntegrationConfigPortal.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "BambuPortalModel.h"
#include "HomeAssistantConfigStore.h"

namespace {
const char INTEGRATIONS_HTML[] PROGMEM = R"html(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>T-Display Integrations</title><style>body{font-family:system-ui;background:#111;color:#eee;margin:0;padding:18px}main{max-width:720px;margin:auto}.card{background:#1d1d1d;border-radius:14px;padding:16px;margin-bottom:14px}h1{font-size:21px;margin:0 0 10px}h2{font-size:18px;margin:0 0 8px}p{line-height:1.5;color:#bbb}label{display:block;color:#aaa;font-size:13px;margin-top:10px}input,textarea,select,button{box-sizing:border-box;width:100%;padding:10px;font-size:15px;border:1px solid #444;border-radius:8px;background:#151515;color:#fff}.row{display:grid;grid-template-columns:1.2fr 1fr;gap:8px}.check{display:flex;gap:8px;align-items:center;color:#eee}.check input{width:auto}textarea{min-height:140px;font-family:monospace}.ok{color:#86d386}.warn{color:#ffcc66}.actions{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:14px}button{background:#eee;color:#111;font-weight:700}button.secondary{background:#333;color:#eee}button.danger{background:#6b2525;color:#fff}.status{font-family:monospace;font-size:13px;white-space:pre-wrap}</style></head><body><main><div class="card"><h1>Integrations</h1><p>配置页运行在设备局域网 HTTP 8081 端口，只应在可信 LAN 使用。Home Assistant 的 HTTP 模式同样只适合可信 LAN；Bambu Cloud 的设备到云端登录和 MQTT 连接始终使用 CA 校验 TLS。</p></div><form id="ha"><div class="card"><h2>Home Assistant</h2><label class="check"><input type="checkbox" name="enabled" value="1">启用 Home Assistant</label><label>Base URL</label><input name="base_url" placeholder="http://homeassistant.local:8123"><label>刷新秒数（30-300）</label><input name="refresh_sec" type="number" min="30" max="300" value="30"><h2 style="margin-top:16px">实体（1-4）</h2><div id="haRows"></div><label>Long-Lived Access Token（留空=保留当前值）</label><input name="token" type="password" autocomplete="new-password"><label>CA PEM（仅 HTTPS 必需；留空=保留当前值）</label><textarea name="ca_cert" spellcheck="false" placeholder="-----BEGIN CERTIFICATE-----"></textarea><label class="check"><input type="checkbox" name="clear_secrets" value="1">清空已保存 Token/CA</label><button type="submit">保存 HA 并重启</button><div id="haMsg"></div></div></form><form id="bambu"><div class="card"><h2>Bambu Lab Cloud</h2><label class="check"><input type="checkbox" name="enabled" value="1">启用 Bambu Lab</label><label>区域</label><select name="region"><option value="us_eu">US / EU / Global</option><option value="china">China</option></select><label>账号邮箱</label><input name="email" type="email" autocomplete="username"><label>账号密码（留空=保留已保存密码；不会从设备回显）</label><input name="password" type="password" autocomplete="current-password"><label class="check"><input type="checkbox" name="remember_password" value="1" checked>保存密码，用于 Token 失效后自动续期</label><label>打印机</label><select name="printer_serial"><option value="">尚未获取</option></select><div class="actions"><button type="button" id="bLogin">登录并获取打印机</button><button type="submit">保存并重启</button></div><button type="button" class="danger" id="bLogout">退出登录并清除凭据</button><div id="bStatus" class="status"></div><div id="bMsg"></div></div></form></main><script>const haRows=document.getElementById('haRows');for(let i=1;i<=4;i++)haRows.insertAdjacentHTML('beforeend',`<label>实体 ${i}</label><div class="row"><input name="entity${i}" placeholder="sensor.temperature"><input name="label${i}" placeholder="显示名称（可选）"></div>`);async function loadHa(){const s=await (await fetch('/api/ha/status')).json();ha.enabled.checked=!!s.enabled;ha.base_url.value=s.base_url||'';ha.refresh_sec.value=String(s.refresh_sec||30);(s.entities||[]).forEach((x,i)=>{ha[`entity${i+1}`].value=x.entity_id;ha[`label${i+1}`].value=x.label||''});haMsg.textContent=`Token ${s.ha_token_set?'已设置':'未设置'} / CA ${s.ha_ca_set?'已设置':'未设置'}`};ha.onsubmit=async e=>{e.preventDefault();haMsg.textContent='保存中...';const r=await fetch('/api/ha/config',{method:'POST',body:new URLSearchParams(new FormData(ha))});const j=await r.json();haMsg.textContent=j.message||'完成'};async function loadPrinters(selected=''){const r=await fetch('/api/bambu/printers');const j=await r.json();const sel=bambu.printer_serial;sel.innerHTML='<option value="">请选择打印机</option>';for(const p of (j.printers||[])){const o=document.createElement('option');o.value=p.serial;o.textContent=`${p.name||p.serial} (${p.serial})`;if(p.serial===selected)o.selected=true;sel.appendChild(o)}}async function loadBambu(){const s=await (await fetch('/api/bambu/status')).json();bambu.enabled.checked=!!s.enabled;bambu.region.value=s.region||'us_eu';bambu.email.value=s.email||'';bStatus.textContent=`状态: ${s.session}\nMQTT: ${s.mqtt_connected?'online':'offline'}\n密码: ${s.password_set?'已保存':'未保存'} / Token: ${s.token_set?'已保存':'未保存'}\n打印机: ${s.printer_name||'--'}`;await loadPrinters(s.printer_serial||'')};bLogin.onclick=async()=>{bMsg.textContent='登录中...';const body=new URLSearchParams();body.set('region',bambu.region.value);body.set('email',bambu.email.value);body.set('password',bambu.password.value);if(bambu.remember_password.checked)body.set('remember_password','1');const r=await fetch('/api/bambu/login',{method:'POST',body});const j=await r.json();bMsg.textContent=j.message||'完成';if(r.ok){bambu.password.value='';await loadBambu()}};bambu.onsubmit=async e=>{e.preventDefault();bMsg.textContent='保存中...';const r=await fetch('/api/bambu/config',{method:'POST',body:new URLSearchParams(new FormData(bambu))});const j=await r.json();bMsg.textContent=j.message||'完成'};bLogout.onclick=async()=>{if(!confirm('清除 Bambu 密码、Token、用户 ID 和打印机选择？'))return;const r=await fetch('/api/bambu/logout',{method:'POST'});const j=await r.json();bMsg.textContent=j.message||'完成'};loadHa();loadBambu();</script></body></html>)html";

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
    case BambuSessionState::TWO_FACTOR_REQUIRED: return "2fa_required";
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
    case BambuCloudError::INVALID_CREDENTIALS: return "账号或密码错误";
    case BambuCloudError::TWO_FACTOR_REQUIRED: return "账号需要 2FA，V1 无法无人值守续期";
    case BambuCloudError::USER_ID_UNAVAILABLE: return "无法获取 Cloud User ID";
  }
  return "Bambu Cloud 错误";
}
}

struct IntegrationConfigPortal::Impl {
  WebServer server{8081};
  HomeAssistantConfigStore haStore;
  HomeAssistantConfig* haConfig = nullptr;
  BambuConfig* bambuConfig = nullptr;
  BambuConfigStore* bambuStore = nullptr;
  BambuCloudClient* bambuCloud = nullptr;
  BambuMqttService* bambuService = nullptr;
  std::vector<BambuCloudDevice> printers;
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
    DynamicJsonDocument doc(1536);
    const BambuConfig current = bambuConfig ? *bambuConfig : BambuConfig{};
    const BambuPortalStatus publicStatus = buildBambuPortalStatus(current);
    const BambuMqttStatus serviceStatus = bambuService ? bambuService->status() : BambuMqttStatus{};
    doc["enabled"] = publicStatus.enabled;
    doc["region"] = regionName(publicStatus.region);
    doc["email"] = publicStatus.email;
    doc["printer_serial"] = publicStatus.printerSerial;
    doc["printer_name"] = publicStatus.printerName;
    doc["password_set"] = publicStatus.passwordSet;
    doc["token_set"] = publicStatus.tokenSet;
    doc["mqtt_connected"] = serviceStatus.mqttConnected;
    doc["session"] = sessionName(serviceStatus.session);
    doc["relogin_failures"] = serviceStatus.reloginFailureCount;
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json; charset=utf-8", body);
  }

  bool refreshPrinters(const BambuConfig& current, BambuCloudError* errorOut = nullptr) {
    if (!bambuCloud || current.accessToken.empty()) {
      if (errorOut) *errorOut = BambuCloudError::INVALID_CREDENTIALS;
      return false;
    }
    const BambuCloudPrintersResult found = bambuCloud->fetchPrinters(current.accessToken, current.region);
    if (!found.ok()) {
      if (errorOut) *errorOut = found.error;
      return false;
    }
    printers = found.printers;
    if (errorOut) *errorOut = BambuCloudError::NONE;
    return true;
  }

  void sendPrinters() {
    BambuConfig current = bambuConfig ? *bambuConfig : BambuConfig{};
    BambuCloudError error = BambuCloudError::NONE;
    if (printers.empty() && !current.accessToken.empty()) refreshPrinters(current, &error);
    DynamicJsonDocument doc(4096);
    doc["ok"] = error == BambuCloudError::NONE;
    if (error != BambuCloudError::NONE) doc["message"] = cloudErrorMessage(error);
    JsonArray list = doc.createNestedArray("printers");
    for (const auto& printer : printers) {
      JsonObject item = list.createNestedObject();
      item["serial"] = printer.serial;
      item["name"] = printer.name;
    }
    String body;
    serializeJson(doc, body);
    server.send(error == BambuCloudError::NONE ? 200 : 409,
                "application/json; charset=utf-8", body);
  }

  void loginBambu() {
    if (!bambuConfig || !bambuStore || !bambuCloud) {
      server.send(503, "application/json", "{\"message\":\"Bambu 配置服务未就绪\"}");
      return;
    }
    BambuPortalCredentials input;
    input.enabled = true;
    input.region = parseRegion(server.arg("region"));
    input.email = server.arg("email").c_str();
    input.password = server.arg("password").c_str();
    input.rememberPassword = server.hasArg("remember_password");

    const BambuConfig existing = *bambuConfig;
    const std::string effectivePassword = effectiveBambuPortalPassword(existing, input);
    if (input.email.empty() || effectivePassword.empty()) {
      server.send(400, "application/json; charset=utf-8", "{\"message\":\"请输入邮箱和密码；密码留空只会保留已经保存过的密码\"}");
      return;
    }

    const BambuCloudLoginResult login = bambuCloud->login(input.email, effectivePassword, input.region);
    if (!login.ok()) {
      DynamicJsonDocument doc(512);
      doc["message"] = cloudErrorMessage(login.error);
      String body;
      serializeJson(doc, body);
      server.send(login.error == BambuCloudError::TWO_FACTOR_REQUIRED ? 409 : 401,
                  "application/json; charset=utf-8", body);
      return;
    }
    const BambuCloudUserIdResult user = bambuCloud->fetchUserId(login.accessToken, input.region);
    if (!user.ok()) {
      DynamicJsonDocument doc(512);
      doc["message"] = cloudErrorMessage(user.error);
      String body;
      serializeJson(doc, body);
      server.send(502, "application/json; charset=utf-8", body);
      return;
    }
    const BambuCloudPrintersResult found = bambuCloud->fetchPrinters(login.accessToken, input.region);
    if (!found.ok()) {
      DynamicJsonDocument doc(512);
      doc["message"] = cloudErrorMessage(found.error);
      String body;
      serializeJson(doc, body);
      server.send(502, "application/json; charset=utf-8", body);
      return;
    }

    BambuConfig updated = mergeBambuPortalCredentials(existing, input);
    updated.enabled = true;
    updated.accessToken = login.accessToken;
    updated.cloudUserId = user.userId;
    printers = found.printers;

    const auto selected = std::find_if(printers.begin(), printers.end(), [&](const BambuCloudDevice& printer) {
      return printer.serial == updated.printerSerial;
    });
    if (selected == printers.end()) {
      updated.printerSerial.clear();
      updated.printerName.clear();
    }
    if (printers.size() == 1U) {
      updated.printerSerial = printers.front().serial;
      updated.printerName = printers.front().name;
    }

    if (!validateBambuConfig(updated).ok() || !bambuStore->save(updated)) {
      server.send(500, "application/json", "{\"message\":\"Bambu 登录成功但配置保存失败\"}");
      return;
    }
    *bambuConfig = updated;

    DynamicJsonDocument doc(512);
    doc["message"] = printers.size() == 1U ? "登录成功，已自动选择唯一打印机" : "登录成功，请选择打印机后保存";
    doc["printer_count"] = printers.size();
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json; charset=utf-8", body);
  }

  void saveBambu() {
    if (!bambuConfig || !bambuStore) {
      server.send(503, "application/json", "{\"message\":\"Bambu 配置服务未就绪\"}");
      return;
    }
    const BambuConfig existing = *bambuConfig;
    BambuPortalCredentials input;
    input.enabled = server.hasArg("enabled");
    input.region = parseRegion(server.arg("region"));
    input.email = server.arg("email").c_str();
    input.password = server.arg("password").c_str();
    input.rememberPassword = server.hasArg("remember_password");
    BambuConfig updated = mergeBambuPortalCredentials(existing, input);

    const std::string selectedSerial = server.arg("printer_serial").c_str();
    if (selectedSerial.empty()) {
      updated.printerSerial.clear();
      updated.printerName.clear();
    } else {
      updated.printerSerial = selectedSerial;
      const auto selected = std::find_if(printers.begin(), printers.end(), [&](const BambuCloudDevice& printer) {
        return printer.serial == selectedSerial;
      });
      if (selected != printers.end()) updated.printerName = selected->name;
      else if (existing.printerSerial != selectedSerial) updated.printerName = selectedSerial;
    }

    if (!validateBambuConfig(updated).ok()) {
      server.send(400, "application/json; charset=utf-8", "{\"message\":\"Bambu 配置无效：启用时需要邮箱以及密码或有效 Token\"}");
      return;
    }
    if (!bambuStore->save(updated)) {
      server.send(500, "application/json", "{\"message\":\"Bambu 配置保存失败\"}");
      return;
    }
    *bambuConfig = updated;
    server.send(200, "application/json; charset=utf-8", "{\"message\":\"Bambu 配置已保存，设备将重启\"}");
    scheduleRestart();
  }

  void logoutBambu() {
    if (!bambuConfig || !bambuStore) {
      server.send(503, "application/json", "{\"message\":\"Bambu 配置服务未就绪\"}");
      return;
    }
    BambuConfig cleared = clearBambuPortalCredentials(*bambuConfig);
    if (!bambuStore->save(cleared)) {
      server.send(500, "application/json", "{\"message\":\"清除 Bambu 凭据失败\"}");
      return;
    }
    *bambuConfig = cleared;
    printers.clear();
    server.send(200, "application/json; charset=utf-8", "{\"message\":\"Bambu 凭据已清除，设备将重启\"}");
    scheduleRestart();
  }
};

IntegrationConfigPortal::IntegrationConfigPortal() : impl_(new Impl()) {}
IntegrationConfigPortal::~IntegrationConfigPortal() = default;

void IntegrationConfigPortal::begin(HomeAssistantConfig& homeAssistantConfig,
                                    BambuConfig& bambuConfig,
                                    BambuConfigStore& bambuStore,
                                    BambuCloudClient& bambuCloud,
                                    BambuMqttService& bambuService) {
  impl_->haConfig = &homeAssistantConfig;
  impl_->bambuConfig = &bambuConfig;
  impl_->bambuStore = &bambuStore;
  impl_->bambuCloud = &bambuCloud;
  impl_->bambuService = &bambuService;
  if (impl_->started) return;

  impl_->server.on("/", HTTP_GET, [this]() {
    impl_->server.send_P(200, PSTR("text/html; charset=utf-8"), INTEGRATIONS_HTML);
  });
  impl_->server.on("/api/ha/status", HTTP_GET, [this]() { impl_->sendHaStatus(); });
  impl_->server.on("/api/ha/config", HTTP_POST, [this]() { impl_->saveHa(); });
  impl_->server.on("/api/bambu/status", HTTP_GET, [this]() { impl_->sendBambuStatus(); });
  impl_->server.on("/api/bambu/login", HTTP_POST, [this]() { impl_->loginBambu(); });
  impl_->server.on("/api/bambu/printers", HTTP_GET, [this]() { impl_->sendPrinters(); });
  impl_->server.on("/api/bambu/config", HTTP_POST, [this]() { impl_->saveBambu(); });
  impl_->server.on("/api/bambu/logout", HTTP_POST, [this]() { impl_->logoutBambu(); });
  impl_->server.onNotFound([this]() {
    impl_->server.send(404, "application/json", "{\"message\":\"not found\"}");
  });
  impl_->server.begin();
  impl_->started = true;
  Serial.printf("[integrations] portal=http://%s:8081/ ha_token_set=%s bambu_password_set=%s bambu_token_set=%s\n",
                WiFi.localIP().toString().c_str(),
                homeAssistantConfig.token.empty() ? "no" : "yes",
                bambuConfig.password.empty() ? "no" : "yes",
                bambuConfig.accessToken.empty() ? "no" : "yes");
}

void IntegrationConfigPortal::process() {
  if (impl_->started) impl_->server.handleClient();
  if (impl_->restartScheduled && static_cast<int32_t>(millis() - impl_->restartAtMs) >= 0) {
    ESP.restart();
  }
}
