# NEONdrive Release Process

This process produces user-friendly precompiled binaries for each hardware target.

## Artifact Policy

Do not publish a single generic firmware name for multiple boards.

Each release must include:

- `neondrive_<version>_cyd_2_4_app.bin`
- `neondrive_<version>_cyd_2_4_fullflash.bin`
- `neondrive_<version>_t_display_s3_app.bin`
- `neondrive_<version>_t_display_s3_fullflash.bin`
- `release_manifest.json`
- `release_sha256.txt`

## Build Release Binaries

From repository root:

```bash
python scripts/build_release_bins.py --version v0.1.0
```

Optional:

```bash
python scripts/build_release_bins.py --targets t_display_s3 --version v0.1.0-rc1
python scripts/build_release_bins.py --no-clean --version v0.1.0
```

Output directory:

```text
dist/releases/<version>/
```

## Verify Artifacts

1. Confirm all expected target files exist.
2. Confirm `release_sha256.txt` is present.
3. Confirm `release_manifest.json` matches the target list and names.
4. Smoke flash at least one board per release (recommended both CYD + T-Display-S3).

## GitHub Release Checklist

1. Create/push tag:

```bash
git tag v0.1.0
git push origin v0.1.0
```

2. Create GitHub Release for that tag.
3. Upload all files from `dist/releases/<version>/`.
4. Include install links:
   - `docs/INSTALL.md`
   - `docs/HARDWARE_TARGETS.md`
5. In release notes, clearly label:
   - `fullflash.bin` = first install / clean install
   - `app.bin` = upgrade-only at `0x10000`

## Mapping Used by Current Targets

| Target | Bootloader | Partitions | boot_app0 | App |
| --- | --- | --- | --- | --- |
| `cyd_2_4` | `0x1000` | `0x8000` | `0xE000` | `0x10000` |
| `t_display_s3` | `0x0000` | `0x8000` | `0xE000` | `0x10000` |
