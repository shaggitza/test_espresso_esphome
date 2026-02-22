# Contributing to ESPHome Espresso Machine Controller

Thank you for your interest in contributing! This document outlines the process
for reporting bugs, requesting features, and submitting code changes.

---

## Code of Conduct

Be respectful and constructive. Contributors of all experience levels are welcome.

---

## How to Contribute

### Reporting Bugs

1. Search [existing issues](../../issues) to avoid duplicates.
2. Open a new issue using the **Bug Report** template.
3. Include:
   - Your machine model
   - Your ESP32 board and ESPHome version
   - The relevant section of your YAML config (redact any secrets)
   - Full ESPHome log output (`esphome logs`)

### Requesting Features

1. Open a new issue using the **Feature Request** template.
2. Describe the use case, not just the solution.
3. Reference the relevant section of `PLAN.md` if applicable.

### Submitting a Pull Request

1. **Fork** the repository and create a branch from `main`:
   ```bash
   git checkout -b feat/my-feature
   ```

2. **Follow the project structure** described in `structure.md`. New subsystems go under
   `components/espresso_machine/<subsystem>/`.

3. **Python schema files** (`__init__.py`) must:
   - Use `esphome.config_validation` (`cv`) for all schema types.
   - Define `CONFIG_SCHEMA` and an async `to_code(config)` function.
   - Pass `esphome config examples/philips_barista_brew.yaml` without errors.

4. **C++ files** must:
   - Live in the `esphome::espresso_machine` namespace.
   - Follow ESPHome coding conventions (2-space indent, `snake_case` methods).
   - Not block the `loop()` — use state machines for multi-step sequences.

5. **Update `examples/philips_barista_brew.yaml`** if you add new YAML keys.

6. **Update `CHANGELOG.md`** under the `[Unreleased]` heading.

7. Open a pull request against `main` with a clear description of the change.

---

## Development Setup

```bash
# Install ESPHome in a virtual environment
python3 -m venv .venv
source .venv/bin/activate
pip install esphome

# Validate the example config (no hardware needed)
esphome config examples/philips_barista_brew.yaml

# Lint Python files
pip install flake8
flake8 components/ --max-line-length=120
```

---

## Commit Message Style

Use [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(brew): add pre-infusion pressure ramp
fix(heater): prevent PID windup on cold start
docs(wiring): add MAX31855 connection diagram
chore: update ESPHome min version to 2024.6
```

---

## License

By contributing, you agree that your contributions will be licensed under the
[Apache 2.0 License](LICENSE).
