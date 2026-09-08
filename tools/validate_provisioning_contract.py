from pathlib import Path

source = Path("src/network/ProvisioningService.cpp").read_text(encoding="utf-8")
required = {
    "WiFiManager debug output": "wm.setDebugOutput(true, WM_DEBUG_NOTIFY);",
    "save-attempt break": "wm.setBreakAfterConfig(true);",
    "Wi-Fi save callback": "wm.setSaveConfigCallback(",
    "parameter save callback": "wm.setSaveParamsCallback(",
    "explicit portal cleanup": "wm.stopConfigPortal();",
    "controlled provisioning reboot": "[prov] provisioning saved successfully; rebooting",
    "location field": '"location_name"',
    "latitude field": '"latitude"',
    "longitude field": '"longitude"',
    "weather enabled field": '"weather_enabled"',
    "weather refresh field": '"weather_refresh"',
}
missing = [name for name, needle in required.items() if needle not in source]
if missing:
    raise SystemExit("Provisioning contract missing: " + ", ".join(missing))

print("Provisioning exit/logging + weather configuration contract: OK")
