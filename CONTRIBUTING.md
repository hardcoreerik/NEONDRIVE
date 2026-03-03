# Contributing to NEONDRIVE

Thank you for your interest in contributing. This project is primarily a single-dev firmware,
but pull requests, bug reports, and hardware feedback are welcome.

---

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/install) (CLI or VS Code extension)
- Python 3.9+
- A supported CYD or LilyGO device (see [Hardware Targets](docs/HARDWARE_TARGETS.md))

### Build From Source

```bash
# Clone
git clone https://github.com/YOUR_USERNAME/neondrive-cyd.git
cd neondrive-cyd

# Build a specific target
pio run -e firmware_cyd_2_4
pio run -e firmware_cyd_3_5
pio run -e firmware_cyd_2_8
```

### Flash and Monitor

```bash
pio run -e firmware_cyd_2_4 -t upload --upload-port <PORT>
pio device monitor --port <PORT> --baud 115200
```

---

## Submitting a Pull Request

1. **Fork** the repo and create a branch from `main`: `git checkout -b fix/your-description`
2. **Build** the relevant target(s) before submitting — PRs that break CI will not be merged
3. **Test on hardware** if at all possible. Note the board and firmware env in the PR description
4. **Keep scope small** — one feature or fix per PR
5. **Write a clear commit message** (`fix:`, `feat:`, `chore:`, `docs:` prefixes preferred)

### What is in scope

- Bug fixes for any supported hardware target
- Performance/DRAM improvements
- New hardware targets (with a matching device profile, board variant, and User_Setup header)
- Documentation improvements
- Test coverage improvements

### What is out of scope / will not be merged

- Features that require cloud accounts or third-party SaaS without an opt-out
- Code that stores credentials in flash or NVS unencrypted in a way that increases risk
- Binaries or compiled artifacts committed to source tree
- Breaking changes to the config.json schema without a migration path

---

## Code Style

- C++: follow the existing file's style (K&R braces, `constexpr` over `#define` for constants)
- Kotlin (CYDCompanion): standard Android / Compose conventions
- Keep functions small and single-purpose where DRAM budget allows
- Prefer `Serial.printf` debug output guarded by `#if UI_DEBUG`

---

## Reporting Bugs

Open a GitHub Issue with:
- Board and firmware env (`firmware_cyd_2_4`, etc.)
- Steps to reproduce
- Serial monitor output (if available)
- What you expected vs. what happened

---

## Legal

By contributing you agree that your contribution will be licensed under the same
license as the rest of the project. See [LICENSE](LICENSE).

For security-related reports, see [SECURITY.md](SECURITY.md).
