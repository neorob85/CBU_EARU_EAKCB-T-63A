Import("env")
from pathlib import Path

env_file = Path(env["PROJECT_DIR"]) / ".env"
if not env_file.exists():
    print("ERROR: .env file not found — copy .env.template to .env and fill in your credentials")
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
        env.Append(CPPDEFINES=[(key.strip(), as_define_value(value.strip()))])
