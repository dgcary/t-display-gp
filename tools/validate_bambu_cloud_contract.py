from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "network" / "BambuCloudProtocol.h"
SOURCE = ROOT / "src" / "network" / "BambuCloudProtocol.cpp"

errors = []

for path in (HEADER, SOURCE):
    if not path.exists():
        errors.append(f"missing {path.relative_to(ROOT)}")

if not errors:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")

    required_header_markers = [
        "enum class BambuLoginDisposition",
        "TOKEN",
        "NEED_EMAIL_CODE",
        "NEED_TFA",
        "ERROR",
        "parseBambuLoginReply",
        "extractBambuUserIdFromJwt",
        "parseBambuDeviceList",
        "bambuReportTopic",
    ]
    for marker in required_header_markers:
        if marker not in header:
            errors.append(f"BambuCloudProtocol.h missing marker: {marker}")

    forbidden = ["HTTPClient", "WiFiClientSecure", "setInsecure", "NetworkArbiter"]
    joined = header + "\n" + source
    for marker in forbidden:
        if marker in joined:
            errors.append(f"pure protocol layer must not depend on transport: {marker}")

if errors:
    for error in errors:
        print(f"ERROR: {error}")
    sys.exit(1)

print("Bambu Cloud pure protocol boundary contract: OK")
