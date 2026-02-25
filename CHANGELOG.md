# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Fixed
- `espresso_machine_brewos`: Added missing Arduino framework library dependencies (`WiFi`, `NetworkClientSecure`) to fix compilation with ESPHome's `lib_ldf_mode=off` — resolves `WiFi.h` / `WiFiClientSecure.h` not found errors
- `espresso_machine_brewos`: Changed library reference from PlatformIO ID `549` to owner/repo format `links2004/WebSockets` for better compatibility

### Added
- Initial project structure: README, PLAN, structure, CONTRIBUTING, NOTICE, CHANGELOG
- GitHub Copilot instructions (`.github/copilot-instructions.md`)
- Reference example YAML for Philips Barista Brew (`examples/philips_barista_brew.yaml`)
- `.gitignore` for ESPHome and Python build artifacts

---

<!-- Keep the link section at the bottom of the file -->
[Unreleased]: https://github.com/shaggitza/test_espresso_esphome/compare/HEAD...HEAD
