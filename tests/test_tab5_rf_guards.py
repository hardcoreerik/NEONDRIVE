from __future__ import annotations

import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]

HIGH_RISK_FILES = [
    REPO_ROOT / "src" / "main.cpp",
    REPO_ROOT / "src" / "yoink_engine.cpp",
    REPO_ROOT / "src" / "wifiscan.cpp",
    REPO_ROOT / "src" / "deauth_hunter.cpp",
]

# These files are intentionally local-backend implementations or scoped stubs.
ALLOWLIST_FILES = {
    "wsl_bypasser.cpp",
    "wsl_bypasser.h",
    "bruce_wifi.cpp",
    "bruce_wifi.h",
}

DANGEROUS_APIS = [
    "esp_wifi_set_promiscuous",
    "esp_wifi_set_promiscuous_rx_cb",
    "esp_wifi_80211_tx",
    "esp_wifi_set_channel",
    "esp_wifi_set_mac",
    "esp_wifi_scan_start",
    "esp_wifi_scan_get_ap_records",
    "WiFi.scanNetworks",
]

GUARD_HINTS = (
    "NEONDRIVE_TARGET_M5TAB5",
    "NEONDRIVE_TARGET_CYD",
    "neon_rf_log_unsupported",
    "rf_unsupported:tab5",
    "TAB5_TEST_C6_SCAN",
)

# Legacy call-sites that are intentionally retained for local-radio targets
# while Tab5 behavior is blocked/routed through stubs in surrounding logic.
# If any of these move, the test should be updated deliberately.
TOLERATED_LEGACY_SITES = {
    ("main.cpp", "esp_wifi_80211_tx", 1393),
    ("main.cpp", "esp_wifi_set_promiscuous", 1837),
    ("main.cpp", "esp_wifi_set_promiscuous", 1838),
    ("main.cpp", "esp_wifi_set_promiscuous", 1859),
    ("main.cpp", "esp_wifi_set_promiscuous", 1860),
    ("main.cpp", "esp_wifi_80211_tx", 6174),
    ("main.cpp", "esp_wifi_80211_tx", 6175),
    ("main.cpp", "esp_wifi_80211_tx", 8762),
    ("main.cpp", "esp_wifi_80211_tx", 11570),
    ("yoink_engine.cpp", "esp_wifi_set_channel", 881),
    ("yoink_engine.cpp", "esp_wifi_set_channel", 915),
    ("yoink_engine.cpp", "esp_wifi_80211_tx", 1068),
}


def _line_has_dangerous_api(line: str) -> str | None:
    stripped = line.strip()
    if stripped.startswith("//"):
        return None
    for api in DANGEROUS_APIS:
        if api in line:
            return api
    return None


def _has_guard_context(lines: list[str], index: int, window: int = 40) -> bool:
    start = max(0, index - window)
    context = "\n".join(lines[start : index + 1])
    return any(hint in context for hint in GUARD_HINTS)


def _is_tolerated_legacy_site(file_name: str, api: str, line_no: int) -> bool:
    for f, a, l in TOLERATED_LEGACY_SITES:
        if f == file_name and a == api and abs(l - line_no) <= 2:
            return True
    return False


def test_high_risk_files_exist() -> None:
    for path in HIGH_RISK_FILES:
        assert path.exists(), f"Missing expected source file: {path}"


def test_dangerous_rf_calls_are_guarded() -> None:
    failures: list[str] = []
    for path in HIGH_RISK_FILES:
        if path.name in ALLOWLIST_FILES:
            continue
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for idx, line in enumerate(lines):
            api = _line_has_dangerous_api(line)
            if not api:
                continue
            line_no = idx + 1
            if _is_tolerated_legacy_site(path.name, api, line_no):
                continue
            if not _has_guard_context(lines, idx):
                failures.append(f"{path}:{idx+1} uses {api} without obvious Tab5 guard/routing")

    assert not failures, "Unsafe RF calls detected:\n" + "\n".join(failures)


def test_neon_rf_header_included_in_high_risk_files() -> None:
    missing = []
    for path in HIGH_RISK_FILES:
        text = path.read_text(encoding="utf-8", errors="replace")
        if '#include "neon_rf.h"' not in text:
            missing.append(str(path))
    assert not missing, "Expected neon_rf.h include missing:\n" + "\n".join(missing)
