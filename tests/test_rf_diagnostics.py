from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[1]
MAIN_CPP = REPO_ROOT / "src" / "main.cpp"
YOINK_CPP = REPO_ROOT / "src" / "yoink_engine.cpp"
NEON_RF_CPP = REPO_ROOT / "src" / "neon_rf.cpp"
STABILIZATION_DOC = REPO_ROOT / "docs" / "tab5-rf-stabilization-plan.md"
INSPECTION_DOC = REPO_ROOT / "docs" / "tab5-rf-inspection-report.md"
TESTING_DOC = REPO_ROOT / "docs" / "testing.md"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def test_last_action_and_backend_logging_exist() -> None:
    text = _read(NEON_RF_CPP)
    assert "last_rf_action" in text
    assert "backend=%s" in text
    assert "caps: scan=" in text
    assert "reset=" in text


def test_expected_rf_action_labels_present() -> None:
    merged = _read(MAIN_CPP) + "\n" + _read(YOINK_CPP)
    expected = [
        "startSniff",
        "ai_inject_raw",
        "ai_inject_deauth",
        "startReconDeauthHunter",
        "startJammitWifi",
        "yoink_start",
    ]
    missing = [label for label in expected if label not in merged]
    assert not missing, f"Missing RF action labels: {missing}"


def test_tab5_docs_exist_and_have_architecture_terms() -> None:
    assert STABILIZATION_DOC.exists(), f"Missing doc: {STABILIZATION_DOC}"
    assert INSPECTION_DOC.exists(), f"Missing doc: {INSPECTION_DOC}"
    text = _read(STABILIZATION_DOC) + "\n" + _read(INSPECTION_DOC)
    for token in [
        "ESP32-P4",
        "ESP32-C6",
        "neon_rf",
        "unsupported",
    ]:
        assert token.lower() in text.lower(), f"Expected token not found in docs: {token}"


def test_testing_doc_exists_with_host_side_commands() -> None:
    assert TESTING_DOC.exists(), f"Missing doc: {TESTING_DOC}"
    text = _read(TESTING_DOC)
    assert "python -m pip install -r tests/requirements.txt" in text
    assert "python -m pytest tests -v" in text
    assert "host-side" in text.lower()


@pytest.mark.skipif(os.getenv("NEONDRIVE_PIO_BUILD_CHECK", "0") != "1", reason="Set NEONDRIVE_PIO_BUILD_CHECK=1 to enable compile checks")
def test_optional_platformio_compile() -> None:
    pio = shutil.which("pio") or shutil.which("pio.exe")
    if not pio:
        pytest.skip("PlatformIO (pio) not found in PATH")

    cmd = [pio, "run", "-e", "firmware_m5tab5"]
    subprocess.run(cmd, cwd=str(REPO_ROOT), check=True)

