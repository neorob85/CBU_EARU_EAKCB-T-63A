Import("env")
from pathlib import Path

# Only these keys are passed as compile-time defines.
# All other config (WiFi, MQTT, device name, etc.) is stored at runtime
# in PrefsManager and configured via the provisioning web portal.
COMPILE_TIME_KEYS = {
    "CFG_FIRMWARE_VERSION",
    "CFG_DEVICE_VERSION",
    "CFG_DEVICE_MANUFACTURER",
    "CFG_DEVICE_MODEL",
}

env_file = Path(env["PROJECT_DIR"]) / ".env"
if not env_file.exists():
    print("ERROR: .env file not found — copy .env.template to .env and fill in your values")
    env.Exit(1)

def as_define_value(value):
    try:
        int(value)
        return value
    except ValueError:
        pass
    try:
        float(value)
        return value
    except ValueError:
        pass
    return f'\\"{value}\\"'

with open(env_file) as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, _, value = line.partition("=")
        key = key.strip()
        if key in COMPILE_TIME_KEYS:
            env.Append(CPPDEFINES=[(key, as_define_value(value.strip()))])
