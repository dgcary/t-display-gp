#!/usr/bin/env python3
"""Validate market HTTP timeout/instrumentation invariants for Arduino-ESP32 2.0.14."""
from pathlib import Path
import sys

build_config = Path("include/build_config.h").read_text(encoding="utf-8")
transport = Path("src/network/HttpTransport.cpp").read_text(encoding="utf-8")

required_config = {
    "constexpr uint32_t HTTP_CONNECT_TIMEOUT_MS = 1500;",
    "constexpr uint32_t HTTP_READ_TIMEOUT_MS = 2500;",
    "constexpr uint32_t HTTP_TLS_HANDSHAKE_TIMEOUT_SEC = 5;",
}
required_transport = {
    "client.setHandshakeTimeout(BuildConfig::HTTP_TLS_HANDSHAKE_TIMEOUT_SEC);",
    "http.setConnectTimeout(BuildConfig::HTTP_CONNECT_TIMEOUT_MS);",
    "http.setTimeout(BuildConfig::HTTP_READ_TIMEOUT_MS);",
    "http.setReuse(false);",
}
forbidden_transport = {
    "client.setTimeout(BuildConfig::HTTP_READ_TIMEOUT_MS);",
}

missing_config = sorted(item for item in required_config if item not in build_config)
missing_transport = sorted(item for item in required_transport if item not in transport)
present_forbidden = sorted(item for item in forbidden_transport if item in transport)

if missing_config or missing_transport or present_forbidden:
    if missing_config:
        print("missing HTTP timeout config contract:")
        for item in missing_config:
            print(f"  {item}")
    if missing_transport:
        print("missing HTTP transport contract:")
        for item in missing_transport:
            print(f"  {item}")
    if present_forbidden:
        print("forbidden HTTP transport calls:")
        for item in present_forbidden:
            print(f"  {item}")
    sys.exit(1)

print("HTTP transport timeout contract: OK")
