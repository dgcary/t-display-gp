from pathlib import Path

source = Path("src/network/ProvisioningService.cpp").read_text(encoding="utf-8")
required = {
    "WiFiManager debug output": "wm.setDebugOutput(true, WM_DEBUG_NOTIFY);",
    "save-attempt break": "wm.setBreakAfterConfig(true);",
    "Wi-Fi save callback": "wm.setSaveConfigCallback(",
    "parameter save callback": "wm.setSaveParamsCallback(",
    "explicit portal cleanup": "wm.stopConfigPortal();",
    "controlled provisioning reboot": "[prov] provisioning saved successfully; rebooting",
}
missing = [name for name, needle in required.items() if needle not in source]
if missing:
    raise SystemExit("Provisioning contract missing: " + ", ".join(missing))

print("Provisioning exit/logging contract: OK")
