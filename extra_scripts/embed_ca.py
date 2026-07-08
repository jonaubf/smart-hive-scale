"""Embed certs/ca.pem into include/ca_pem_embed.h at build time."""

from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
ca_path = project_dir / "certs" / "ca.pem"
out_path = project_dir / "include" / "ca_pem_embed.h"


def embed_ca():
    if ca_path.is_file():
        pem = ca_path.read_text(encoding="utf-8").strip()
        if "BEGIN CERTIFICATE" not in pem:
            print(f"WARNING: {ca_path} does not look like a PEM certificate")
        body = "\n".join(
            [
                "#pragma once",
                "",
                "#define CA_PEM_AVAILABLE 1",
                "",
                'static const char CA_PEM[] PROGMEM = R"CAPEM(',
                pem,
                ')CAPEM";',
                "static const size_t CA_PEM_LEN = sizeof(CA_PEM) - 1;",
                "",
            ]
        )
        print(f"Embedded CA certificate from {ca_path} ({len(pem)} bytes)")
    else:
        body = "\n".join(
            [
                "#pragma once",
                "",
                "#define CA_PEM_AVAILABLE 0",
                "",
                "static const char CA_PEM[] PROGMEM = \"\";",
                "static const size_t CA_PEM_LEN = 0;",
                "",
            ]
        )
        print(
            f"WARNING: {ca_path} not found — MQTT TLS will fail until you copy your CA:"
        )
        print("  cp ~/beekpr-certs/ca.crt certs/ca.pem")

    out_path.write_text(body, encoding="utf-8")


embed_ca()
