from __future__ import annotations

from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
NEON_RF_H = REPO_ROOT / "src" / "neon_rf.h"
NEON_RF_CPP = REPO_ROOT / "src" / "neon_rf.cpp"


def test_neon_rf_header_exists() -> None:
    assert NEON_RF_H.exists(), f"Missing {NEON_RF_H}"


def test_neon_rf_cpp_exists() -> None:
    assert NEON_RF_CPP.exists(), f"Missing {NEON_RF_CPP}"


def test_capability_struct_fields_present() -> None:
    text = NEON_RF_H.read_text(encoding="utf-8", errors="replace")
    for field in [
        "supports_station_scan",
        "supports_channel_control",
        "supports_promiscuous_rx",
        "supports_raw_frame_tx",
        "requires_remote_coprocessor",
        "requires_custom_c6_firmware",
    ]:
        assert field in text, f"Missing field in NeonRfCaps: {field}"


def test_tab5_caps_and_backend_present() -> None:
    text = NEON_RF_CPP.read_text(encoding="utf-8", errors="replace")
    assert "NEONDRIVE_TARGET_M5TAB5" in text
    assert "tab5-none" in text or "tab5-c6-scan" in text
    assert "requires_remote_coprocessor" in text


def test_unsupported_logging_and_return_path_present() -> None:
    text = NEON_RF_CPP.read_text(encoding="utf-8", errors="replace")
    assert "NOT SUPPORTED on backend" in text
    assert "ESP_ERR_NOT_SUPPORTED" in text


def test_c6test_entrypoint_declared_and_implemented() -> None:
    h_text = NEON_RF_H.read_text(encoding="utf-8", errors="replace")
    cpp_text = NEON_RF_CPP.read_text(encoding="utf-8", errors="replace")
    assert "neon_rf_c6test_sdio_init" in h_text
    assert "neon_rf_c6test_sdio_init" in cpp_text
    assert "[c6test] step=sdio_init start" in cpp_text

