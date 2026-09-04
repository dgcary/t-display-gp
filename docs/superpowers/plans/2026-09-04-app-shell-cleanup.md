# App Shell Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove Crypto and Nixie Clock completely, restore Stock as startup, and leave a clean four-app intermediate shell ready for the Bambu Cloud app.

**Architecture:** Delete the two obsolete app stacks instead of hiding them. Collapse the shared `AppDataWorker` back to Weather + Home Assistant only, keep the existing no-idle-switch `AppManager`, and make Stock the default startup at both API and `main.cpp` wiring levels.

**Tech Stack:** Arduino/C++17, PlatformIO `lilygo-t-display-s3`, Unity native tests, Python static contract validators, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-09-04-bambu-cloud-integration-design.md`

## Global Constraints

- Target remains LILYGO T-Display-S3, logical 320×170 landscape rotation 3.
- GPIO/button semantics and 40 ms debounce / 700 ms long-press behavior remain unchanged.
- There is no automatic idle app switch.
- Preserve Stock Tencent/EastMoney behavior, Weather + 168×126 Bad Apple behavior, Home Assistant behavior, Device Info, NVS and normal application-only flashing.
- Keep exactly one shared `AppDataWorker`; after cleanup it serves Weather and Home Assistant only.
- Keep the common system clock/NTP facilities even though Nixie is removed.
- Current docs (`README.md`, `AGENTS.md`, `docs/deployment.md`, `docs/api-contract.md`, `docs/hardware-acceptance.md`) must match the intermediate four-app behavior in the same change.

---

### Task 1: RED app-shell contract

**Files:**
- Modify: `test/test_app_shell/test_main.cpp`
- Create: `tools/validate_app_shell_contract.py`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `AppId`, `AppManager::begin(AppId)`, `MenuApp`.
- Produces: a contract requiring default startup `AppId::STOCK`, no `NIXIE_CLOCK`/`CRYPTO` app IDs in current source wiring, and no global idle switch.

- [ ] **Step 1: Change the app-shell native test to the new default.**

Replace the Nixie fixture with Stock/Weather/HomeAssistant/DeviceInfo fakes and make the first test assert:

```cpp
void test_defaults_to_stock_and_enters_once() {
  ShellFixture f;
  TEST_ASSERT_TRUE(f.manager.begin());
  TEST_ASSERT_EQUAL(AppId::STOCK, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(1, f.stock.enters);
}
```

Long-inactivity tests assert only that Weather/Menu/DeviceInfo remain active; they must not depend on a Nixie fake.

- [ ] **Step 2: Add a static shell validator that intentionally fails on the old source.**

`tools/validate_app_shell_contract.py` must require:

```text
AppManager default = AppId::STOCK
main.cpp startup = AppId::STOCK
main menu has 股票 / 天气 / 智能家居 / 设备信息
AppShell.h and main.cpp contain neither NIXIE_CLOCK nor CRYPTO
main.cpp contains neither NixieClockApp nor CryptoApp
```

It must also reject obvious global idle-switch symbols such as `IDLE_TO_NIXIE`, `idleDeadline`, and `switchTo(AppId::NIXIE_CLOCK)`.

- [ ] **Step 3: Wire the new validator into CI before implementation.**

Add:

```yaml
- run: python tools/validate_app_shell_contract.py
```

Keep existing validators temporarily so this RED commit fails for both the expected new shell contract and, after source removal begins, any stale legacy validator that still requires removed apps.

- [ ] **Step 4: Verify RED in GitHub Actions.**

Expected: native shell test and/or `validate_app_shell_contract.py` fails because the old branch still starts Nixie and contains Crypto/Nixie wiring. Record the exact run and failure reason before GREEN.

- [ ] **Step 5: Commit RED.**

```bash
git add test/test_app_shell/test_main.cpp tools/validate_app_shell_contract.py .github/workflows/ci.yml
git commit -m "test: require Stock-only startup shell"
```

---

### Task 2: GREEN remove Crypto/Nixie from runtime wiring

**Files:**
- Modify: `src/app/AppShell.h`
- Modify: `src/main.cpp`
- Modify: `src/network/AppDataTypes.h`
- Modify: `src/network/AppDataWorker.cpp`
- Modify: `platformio.ini`

**Interfaces:**
- Produces final intermediate `AppId` values: `MENU`, `STOCK`, `WEATHER`, `HOME_ASSISTANT`, `DEVICE_INFO`.
- `AppDataRequestType` becomes `WEATHER`, `HOME_ASSISTANT`.

- [ ] **Step 1: Set `AppManager` default startup to Stock and delete obsolete App IDs.**

`AppShell.h` must contain:

```cpp
enum class AppId {
  MENU,
  STOCK,
  WEATHER,
  HOME_ASSISTANT,
  DEVICE_INFO,
};
...
bool begin(AppId startupApp = AppId::STOCK);
```

- [ ] **Step 2: Remove Nixie/Crypto includes, instances, menu entries, diagnostics and begin calls from `main.cpp`.**

Intermediate menu order:

```cpp
{{AppId::STOCK, "股票"},
 {AppId::WEATHER, "天气"},
 {AppId::HOME_ASSISTANT, "智能家居"},
 {AppId::DEVICE_INFO, "设备信息"}}
```

Call `appManager.begin(AppId::STOCK)`.

- [ ] **Step 3: Remove Crypto from shared data types.**

`AppDataTypes.h` must no longer include `CryptoProvider.h`, no longer expose `CRYPTO`, and no longer carry Crypto error/snapshot/diagnostics fields.

- [ ] **Step 4: Remove Crypto provider/result queue execution from `AppDataWorker.cpp`.**

After change the worker owns exactly:

```text
OpenMeteoProvider
SecureHomeAssistantTransport / HomeAssistantProvider
weatherResultQueue
homeAssistantResultQueue
```

- [ ] **Step 5: Remove Crypto native sources from `platformio.ini`.**

Delete `app/CryptoController.cpp` and `network/CryptoProvider.cpp` from `build_src_filter`.

- [ ] **Step 6: Push GREEN source and verify the new shell tests.**

Expected: new shell test passes. Legacy Nixie/dashboard validator may still fail until Task 3 removes obsolete contracts.

- [ ] **Step 7: Commit.**

```bash
git commit -am "refactor: remove Crypto and Nixie runtime wiring"
```

---

### Task 3: Delete obsolete feature files/tests and repair CI contracts

**Files:**
- Delete: `src/app/CryptoApp.*`, `src/app/CryptoController.*`, `src/ui/CryptoScreen.*`, `src/network/CryptoProvider.*`
- Delete: `src/app/NixieClockApp.*`, `src/app/NixieClockModel.h`, `src/ui/NixieClockScreen.*`
- Delete: `test/test_crypto_controller/test_main.cpp`
- Delete: `test/test_crypto_provider/test_main.cpp`
- Delete: `test/test_nixie_clock_model/test_main.cpp`
- Delete: `tools/validate_nixie_clock_contract.py`
- Modify: `tools/validate_dashboard_apps_contract.py`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- `validate_dashboard_apps_contract.py` becomes an HA-only shared-worker/security validator while retaining its filename for CI continuity.

- [ ] **Step 1: Delete obsolete implementation and native test files.**

No current production source may reference `CryptoApp`, `CryptoController`, `CryptoProvider`, `NixieClockApp`, `NixieClockModel`, or `NixieClockScreen`.

- [ ] **Step 2: Convert dashboard validator to Home Assistant-only.**

It must still require all HA app/controller/screen/provider/config/store/portal files, strict HA HTTPS CA behavior, one shared `AppDataWorker`, and typed `HOME_ASSISTANT` result handling. It must contain no Crypto requirement.

- [ ] **Step 3: Remove the Nixie validator CI step.**

CI shell validators become:

```yaml
- run: python tools/validate_tdisplay_setup.py
- run: python tools/validate_provisioning_contract.py
- run: python tools/validate_http_transport_contract.py
- run: python tools/validate_app_shell_contract.py
- run: python tools/validate_dashboard_apps_contract.py
- run: python tools/validate_bad_apple_contract.py
```

- [ ] **Step 4: Run exact-head CI.**

Expected: all Python validators PASS; Ubuntu native and Windows native PASS; Bad Apple generation and ESP32-S3 build PASS.

- [ ] **Step 5: Commit.**

```bash
git commit -m "refactor: delete Crypto and Nixie feature stacks"
```

---

### Task 4: Align current documentation

**Files:**
- Modify: `README.md`
- Modify: `AGENTS.md`
- Modify: `docs/deployment.md`
- Modify: `docs/api-contract.md`
- Modify: `docs/hardware-acceptance.md`

**Interfaces:**
- Documents the temporary four-app shell before Bambu is introduced by Plan B.

- [ ] **Step 1: Replace six-app/Nixie/Crypto current-state claims.**

Document:

```text
Stock / Weather+Bad Apple / Home Assistant / Device Info
Startup = Stock
No automatic idle switching
AppDataWorker = Weather + HA only
```

- [ ] **Step 2: Preserve historical design documents.**

Do not rewrite old dated specs/plans merely because they describe earlier milestones.

- [ ] **Step 3: Update physical acceptance to remove Nixie/Crypto tests and require Stock startup.**

Keep all existing Weather/Bad Apple and HA acceptance criteria unchanged.

- [ ] **Step 4: Commit.**

```bash
git add README.md AGENTS.md docs/deployment.md docs/api-contract.md docs/hardware-acceptance.md
git commit -m "docs: align four-app cleanup baseline"
```

---

### Task 5: Freeze the cleanup baseline

**Files:** none unless verification finds a defect.

- [ ] **Step 1: Run final exact-head CI.**

Required:

```text
all static validators PASS
Ubuntu native PASS
Windows native PASS
Bad Apple 2190-frame generation/round-trip PASS
PlatformIO lilygo-t-display-s3 PASS
```

- [ ] **Step 2: Inspect artifact manifest and memory usage.**

The application image must still include Bad Apple and preserve normal `firmware.bin @ 0x10000` deployment semantics.

- [ ] **Step 3: Use this SHA only as the internal base for Plan B.**

Do not hand this temporary four-app build to hardware unless needed for fault isolation; Plan B immediately layers Bambu on top.
