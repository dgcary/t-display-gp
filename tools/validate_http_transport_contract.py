#!/usr/bin/env python3
"""Validate HTTP timeout, reuse, shared TLS serialization, and network diagnostics invariants."""
from pathlib import Path
import sys

build_config = Path("include/build_config.h").read_text(encoding="utf-8")
transport = Path("src/network/HttpTransport.cpp").read_text(encoding="utf-8")
arbiter = Path("src/network/NetworkArbiter.cpp").read_text(encoding="utf-8")
main = Path("src/main.cpp").read_text(encoding="utf-8")

required_config = {
    "constexpr uint32_t HTTP_CONNECT_TIMEOUT_MS = 1500;",
    "constexpr uint32_t HTTP_READ_TIMEOUT_MS = 2500;",
    "constexpr uint32_t HTTP_TLS_HANDSHAKE_TIMEOUT_SEC = 5;",
}
required_transport = {
    "NetworkRequestGuard requestGuard(sharedNetworkArbiter());",
    "client.setHandshakeTimeout(BuildConfig::HTTP_TLS_HANDSHAKE_TIMEOUT_SEC);",
    "http.setConnectTimeout(BuildConfig::HTTP_CONNECT_TIMEOUT_MS);",
    "http.setTimeout(BuildConfig::HTTP_READ_TIMEOUT_MS);",
    "http.setReuse(false);",
    "arbiterWaitMs",
    "ioElapsedMs",
    "WiFi.BSSIDstr()",
    "WiFi.channel()",
    "[net] host=%s arb=%lums io=%lums",
}
required_arbiter = {
    "xSemaphoreCreateMutex()",
    "xSemaphoreTake(mutex_, portMAX_DELAY)",
    "xSemaphoreGive(mutex_)",
}
required_main = {
    "sharedNetworkArbiter().begin()",
}
forbidden_transport = {
    "client.setTimeout(BuildConfig::HTTP_READ_TIMEOUT_MS);",
}

missing_config = sorted(item for item in required_config if item not in build_config)
missing_transport = sorted(item for item in required_transport if item not in transport)
missing_arbiter = sorted(item for item in required_arbiter if item not in arbiter)
missing_main = sorted(item for item in required_main if item not in main)
present_forbidden = sorted(item for item in forbidden_transport if item in transport)

if missing_config or missing_transport or missing_arbiter or missing_main or present_forbidden:
    if missing_config:
        print("missing HTTP timeout config contract:")
        for item in missing_config:
            print(f"  {item}")
    if missing_transport:
        print("missing HTTP transport/diagnostics contract:")
        for item in missing_transport:
            print(f"  {item}")
    if missing_arbiter:
        print("missing shared TLS serialization contract:")
        for item in missing_arbiter:
            print(f"  {item}")
    if missing_main:
        print("missing network arbiter startup contract:")
        for item in missing_main:
            print(f"  {item}")
    if present_forbidden:
        print("forbidden HTTP transport calls:")
        for item in present_forbidden:
            print(f"  {item}")
    sys.exit(1)

print("HTTP transport timeout + serialized TLS + network diagnostics contract: OK")
