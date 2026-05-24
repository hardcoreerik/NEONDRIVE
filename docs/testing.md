# Testing

Host-side regression tests can run on a developer PC without M5Tab5 hardware.

## Install

```bash
python -m pip install -r tests/requirements.txt
```

## Run

```bash
python -m pytest tests -v
```

## Notes

- These are **host-side** static/regression tests to prevent Tab5 RF architectural backsliding.
- They do not flash firmware and do not require hardware by default.
- Optional PlatformIO compile checks are available with:

```bash
NEONDRIVE_PIO_BUILD_CHECK=1 python -m pytest tests/test_rf_diagnostics.py -v
```

