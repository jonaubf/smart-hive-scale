"""Load key=value pairs from .env into include/build_env.h (C defines)."""

from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
env_file = project_dir / ".env"
out_path = project_dir / "include" / "build_env.h"

INTEGER_KEYS = {
    "MQTT_BROKER_PORT",
    "MQTT_USE_TLS",
    "SETUP_BUTTON_HOLD_MS",
    "WIFI_CONNECT_TIMEOUT_MS",
    "MODEM_NETWORK_TIMEOUT_MS",
    "MODEM_GPRS_CONNECT_TIMEOUT_MS",
    "MODEM_TCP_CONNECT_TIMEOUT_MS",
    "MODEM_TLS_HANDSHAKE_TIMEOUT_MS",
    "MODEM_MQTT_CONNECT_TIMEOUT_MS",
}


def _c_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def _format_define(key: str, raw_value: str) -> str:
    value = raw_value.strip().strip('"').strip("'")
    if key in INTEGER_KEYS or value.isdigit():
        return f"#define {key} {value}"
    return f'#define {key} "{_c_escape(value)}"'


def load_dotenv():
    lines = [
        "#pragma once",
        "",
        "// Auto-generated from .env by extra_scripts/load_env.py — do not edit.",
        "",
    ]

    if not env_file.is_file():
        print("WARNING: .env not found — copy .env.example to .env")
        lines.append("// No .env file; defaults from config.h are used.")
    else:
        for line in env_file.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            if not key:
                continue
            lines.append(_format_define(key, value))
            print(f"Loaded from .env: {key}")

    lines.append("")
    out_path.write_text("\n".join(lines), encoding="utf-8")


load_dotenv()
