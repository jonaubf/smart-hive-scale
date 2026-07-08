"""Patch SSLClient for slow GSM: pump modem during TLS I/O and read early."""

from pathlib import Path

Import("env")

HOOK_DECL = 'extern "C" void beekprModemPumpForTls(void);'
HOOK_INCLUDE = '#include "ssl__client.h"'

# Replaces the whole client_net_recv_timeout body with a NON-blocking receive,
# mirroring WiFiClientSecure semantics. Blocking here is wrong: the handshake
# loop retries on WANT_READ with its own timeout, and PubSubClient polls
# available() with its own socket timeout. A blocking wait (up to the 120s
# mbedTLS read timeout) stalled PubSubClient::connected() before publish and
# let the broker's MQTT keepalive expire.
RECV_TIMEOUT_FUNC_START = (
    "int client_net_recv_timeout(void *ctx, unsigned char *buf, size_t len, uint32_t timeout) {"
)

RECV_TIMEOUT_NEW_BODY = """int client_net_recv_timeout(void *ctx, unsigned char *buf, size_t len, uint32_t timeout) {
  Client *client = (Client*)ctx;

  if (!client) {
    log_e("Uninitialised!");
    return -1;
  }

  if (len == 0) {
    log_e("Zero length specified!");
    return 0;
  }

  (void)timeout;  // non-blocking: callers retry on MBEDTLS_ERR_SSL_WANT_READ

  beekprModemPumpForTls();

  int pending = client->available();
  if (pending <= 0) {
    if (!client->connected()) {
      return 0;  // clean EOF so mbedTLS reports the closed connection
    }
    return MBEDTLS_ERR_SSL_WANT_READ;
  }

  int result = client->read(buf, len);
  if (result <= 0) {
    return MBEDTLS_ERR_SSL_WANT_READ;
  }

  Serial.printf("[TLS RX] %d bytes (asked %u)\\n", result, (unsigned)len);
  return result;
}"""

SEND_PUMP = """  if (!client->connected()) {
    log_e("Not connected!");
    return -2;
  }

  beekprModemPumpForTls();
  
  // esp_log_buffer_hexdump_internal("SSL.WR", buf, (uint16_t)len, ESP_LOG_VERBOSE);MBEDTLS_ERR_NET_SEND_FAILED;"""

SEND_ORIG = """  if (!client->connected()) {
    log_e("Not connected!");
    return -2;
  }
  
  // esp_log_buffer_hexdump_internal("SSL.WR", buf, (uint16_t)len, ESP_LOG_VERBOSE);MBEDTLS_ERR_NET_SEND_FAILED;"""

HANDSHAKE_PUMP_FROM = "    vTaskDelay(10 / portTICK_PERIOD_MS);"
HANDSHAKE_PUMP_TO = """    beekprModemPumpForTls();
    vTaskDelay(10 / portTICK_PERIOD_MS);"""

SEND_BEFORE_LOOP = """  // esp_log_buffer_hexdump_internal("SSL.WR", buf, (uint16_t)len, ESP_LOG_VERBOSE);MBEDTLS_ERR_NET_SEND_FAILED;

  beekprModemPumpForTls();

  // int result = client->write(buf, len);"""

SEND_BEFORE_LOOP_ORIG = """  // esp_log_buffer_hexdump_internal("SSL.WR", buf, (uint16_t)len, ESP_LOG_VERBOSE);MBEDTLS_ERR_NET_SEND_FAILED;

  // int result = client->write(buf, len);"""

SEND_AFTER_WRITE = """    result += client->write(buffer, bytesToWrite);
    beekprModemPumpForTls();"""

SEND_AFTER_WRITE_ORIG = "    result += client->write(buffer, bytesToWrite);"

# NOTE: an earlier revision configured mbedtls_ssl_conf_max_frag_len(512) here.
# Removed: OpenSSL-based brokers ignore the max_fragment_length extension and
# still send full-size records, which then overflow the shrunken input buffer
# (mbedTLS error -0x7080 FEATURE_UNAVAILABLE after the first large record).
MFL_PATCH = """  if (ret == 0) {
    ret = mbedtls_ssl_conf_max_frag_len(&ssl_client->ssl_conf, MBEDTLS_SSL_MAX_FRAG_LEN_512);
  }
  return ret;"""

MFL_REVERT = """  return ret;"""

# mbedTLS 2.x (ESP32 Arduino core) cannot match IP-address SANs, so the name
# check always fails when connecting by IP even with a valid certificate.
# Skip the hostname check for IP literals; the CA chain is still verified.
HOSTNAME_ORIG = """  // Hostname set here should match CN in server certificate
  ret = mbedtls_ssl_set_hostname(&ssl_client->ssl_ctx, host);"""

HOSTNAME_PATCH = """  // Hostname set here should match CN in server certificate
  bool beekprIsIpLiteral = (host != NULL && *host != '\\0');
  for (const char *beekprP = host; beekprIsIpLiteral && *beekprP != '\\0'; ++beekprP) {
    if ((*beekprP < '0' || *beekprP > '9') && *beekprP != '.') {
      beekprIsIpLiteral = false;
    }
  }
  if (beekprIsIpLiteral) {
    // mbedTLS 2.x cannot match IP SANs; skip name check (CA chain still verified)
    ret = mbedtls_ssl_set_hostname(&ssl_client->ssl_ctx, NULL);
  } else {
    ret = mbedtls_ssl_set_hostname(&ssl_client->ssl_ctx, host);
  }"""


def _ssl_client_path() -> Path:
    return (
        Path(env.subst("$PROJECT_LIBDEPS_DIR"))
        / env.subst("$PIOENV")
        / "SSLClient"
        / "src"
        / "ssl__client.cpp"
    )


def _replace_function_body(content: str, func_signature: str, new_function: str) -> str:
    """Replace an entire function (matched by brace counting) with new_function."""
    start = content.find(func_signature)
    if start < 0:
        return content

    depth = 0
    end = -1
    for i in range(start + len(func_signature) - 1, len(content)):
        ch = content[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    if end < 0:
        return content

    return content[:start] + new_function + content[end:]


def _apply_text_patches(content: str) -> str:
    updated = content

    if HOOK_DECL not in updated:
        updated = updated.replace(HOOK_INCLUDE, HOOK_INCLUDE + "\n" + HOOK_DECL, 1)

    if RECV_TIMEOUT_NEW_BODY not in updated:
        updated = _replace_function_body(
            updated, RECV_TIMEOUT_FUNC_START, RECV_TIMEOUT_NEW_BODY
        )

    if SEND_ORIG in updated and SEND_PUMP not in updated:
        updated = updated.replace(SEND_ORIG, SEND_PUMP, 1)

    if SEND_BEFORE_LOOP_ORIG in updated and SEND_BEFORE_LOOP not in updated:
        updated = updated.replace(SEND_BEFORE_LOOP_ORIG, SEND_BEFORE_LOOP, 1)

    if SEND_AFTER_WRITE_ORIG in updated and "write(buffer, bytesToWrite);\n    beekprModemPumpForTls()" not in updated:
        updated = updated.replace(SEND_AFTER_WRITE_ORIG, SEND_AFTER_WRITE, 1)

    if MFL_PATCH in updated:
        updated = updated.replace(MFL_PATCH, MFL_REVERT, 1)

    if HOSTNAME_ORIG in updated and HOSTNAME_PATCH not in updated:
        updated = updated.replace(HOSTNAME_ORIG, HOSTNAME_PATCH, 1)

    if HANDSHAKE_PUMP_FROM in updated and HANDSHAKE_PUMP_TO not in updated:
        updated = updated.replace(HANDSHAKE_PUMP_FROM, HANDSHAKE_PUMP_TO, 1)

    return updated


def apply_sslclient_patch(source=None, target=None, env_obj=None):
    path = _ssl_client_path()
    if not path.is_file():
        return

    content = path.read_text(encoding="utf-8")
    updated = _apply_text_patches(content)
    if updated == content:
        if RECV_TIMEOUT_NEW_BODY not in content or HOOK_DECL not in content:
            print(f"WARNING: SSLClient patch patterns not fully applied in {path}")
        return

    path.write_text(updated, encoding="utf-8")
    print("Patched SSLClient for slow GSM (modem pump + early recv)")

    build_dir_name = env.get("BUILD_DIR")
    pioenv = env.get("PIOENV")
    if build_dir_name and pioenv:
        build_dir = Path(str(build_dir_name)) / str(pioenv)
        for obj in build_dir.glob("lib*/SSLClient/ssl__client.cpp.o"):
            obj.unlink(missing_ok=True)
            print(f"Removed stale {obj.name} to force SSLClient rebuild")


def register_patch_hook():
    path = _ssl_client_path()
    if path.is_file():
        apply_sslclient_patch()
        return

    def _patch_before_build(source, target, env):
        apply_sslclient_patch()

    env.AddPreAction("buildprog", _patch_before_build)


register_patch_hook()
