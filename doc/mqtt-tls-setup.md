# MQTT over TLS — Home setup (Step 9)

Field devices connect to your home Mosquitto broker over the **public static IP** using **MQTT over TLS** (port **8883**). No Cloudflare, no extra domain.

Home Assistant and all **local LAN clients** keep using **plain MQTT on port 1883** — no certificates on the client side. Only **field hives over the internet** use **TLS on port 8883**.

## Architecture

```
  LOCAL LAN (no client certs)                    INTERNET (field hives)
  ─────────────────────────                      ──────────────────────

  Home Assistant ──┐                             ESP32 (GPRS) ──► static.ip:8883
  Node-RED ────────┼──► Mosquitto :1883          (router port forward)
  WiFi sensors ────┤    username + password              │
  gate-master ─────┘    no TLS                          ▼
                                              Mosquitto :8883 (TLS)
                                              server cert only
                                              username + password
                                              (require_certificate: false)
```

**Enabling TLS does not remove or change port 1883.** The HA Mosquitto add-on runs **both** listeners when `server.crt` + `server.key` are in `/ssl/`:

| Port | Encryption | Who uses it | Client certificate required? |
|------|------------|-------------|------------------------------|
| **1883** | None | HA, Node-RED, LAN devices, WiFi-mode hives at home | **No** — username/password only |
| **8883** | TLS (server cert) | ESP32 in the field (cellular) | **No** — username/password only |

`require_certificate: false` means clients never present their own certificate — neither local nor remote. The **server** presents `server.crt` on 8883; the ESP32 trusts it because your CA is embedded in firmware. Local clients on 1883 skip TLS entirely.

**Do not forward port 1883 on the router.** LAN clients reach 1883 only inside your network; field devices use the public IP on 8883.

## Security model

| Layer | Measure |
|-------|---------|
| Transport (field) | **TLS 1.2+** on **8883** — server certificate only |
| Transport (LAN) | **Plain MQTT** on **1883** — no change for existing clients |
| Application | **MQTT username + password** per device/user |
| Exposure | Only **8883/TCP** forwarded; **1883 stays on LAN** |
| Broker | `allow_anonymous false` on both listeners |
| Firmware (field) | CA certificate **embedded** to verify server on 8883 |
| Local clients | **No certificates** — keep current `mqtt` user / port 1883 |

Optional later: **mutual TLS** (client certificates per hive) — not used in v1; would complicate SIM800L and local clients.

---

## 1. Generate certificates (on your PC or HA host)

Create a private CA and server cert. Replace `YOUR_STATIC_IP` with your public IP (used as CN/SAN).

```bash
mkdir -p ~/beekpr-certs && cd ~/beekpr-certs

# Certificate authority (keep ca.key secret, offline backup)
openssl genrsa -out ca.key 4096
openssl req -x509 -new -nodes -key ca.key -sha256 -days 3650 \
  -out ca.crt -subj "/CN=Beekpr Home CA"

# Server key + CSR
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr \
  -subj "/CN=YOUR_STATIC_IP"

# SAN for IP address (required for TLS IP connections)
cat > server.ext <<EOF
subjectAltName = IP:YOUR_STATIC_IP
extendedKeyUsage = serverAuth
EOF

openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out server.crt -days 825 -sha256 -extfile server.ext
```

Files you need:

| File | Use |
|------|-----|
| `ca.crt` | Trust anchor — embed in ESP32 firmware |
| `server.crt` | Mosquitto TLS certificate |
| `server.key` | Mosquitto TLS private key (**secret**) |
| `ca.key` | CA private key (**secret**, do not deploy**) |

Copy `ca.crt` into this project as `certs/ca.pem` (gitignored) for firmware builds.

---

## 2. Mosquitto (Home Assistant add-on)

The HA Mosquitto add-on does **not** use raw `listener` blocks in its YAML. TLS on **8883** is enabled automatically when `certfile` and `keyfile` are set **and** those files exist in Home Assistant’s `/ssl/` folder.

### 2a. Upload certificate files

The Mosquitto add-on init script checks **only** these paths inside the container:

```
/ssl/server.crt
/ssl/server.key
```

Your YAML `certfile: server.crt` is correct — the add-on prepends `/ssl/` automatically. The files must physically exist there on the **Home Assistant host**, not inside the add-on config folder.

**Wrong locations (will not work):**

| Wrong | Why |
|-------|-----|
| `/config/ssl/server.crt` | File Editor default; Mosquitto does not look here |
| `config/ssl/` via Samba | Same problem |
| Only pasted into add-on YAML | Config names files; it does not upload bytes |

**Correct location on Home Assistant OS:**

| Method | Where to put files |
|--------|-------------------|
| **Terminal & SSH** add-on | `/ssl/server.crt`, `/ssl/server.key`, `/ssl/ca.crt` |
| **Samba** share | `ssl/` folder next to `configuration.yaml` (not inside `config/`) |
| **Studio Code Server** | Open `/ssl/` at filesystem root |

Create the folder if missing:

```bash
mkdir -p /ssl
ls -la /ssl/
```

You must see all three files with exact names (case-sensitive):

```
ca.crt
server.crt
server.key
```

Copy from your PC (example):

```bash
scp server.crt server.key ca.crt root@HOMEASSISTANT:/ssl/
```

Or from the machine where you generated certs:

```bash
scp ~/beekpr-certs/server.crt ~/beekpr-certs/server.key ~/beekpr-certs/ca.crt root@192.168.68.118:/ssl/
```

Replace `192.168.68.118` with your HA host LAN IP.

**After upload, verify before restarting Mosquitto:**

```bash
ls -la /ssl/server.crt /ssl/server.key
```

Both must exist. If `ls` fails, SSL will stay disabled.

| File in `/ssl/` | Source |
|-----------------|--------|
| `ca.crt` | Your Beekpr CA |
| `server.crt` | Server certificate (signed by CA, SAN = public IP) |
| `server.key` | Server private key |

Use **filenames only** in add-on config — not paths like `/ssl/server.crt`.

### 2b. Add-on configuration

**Settings → Add-ons → Mosquitto broker → Configuration:**

```yaml
logins:
  - username: hive01
    password: "<strong-random-password>"
  - username: mqtt
    password: "<home-assistant-mqtt-password>"

certfile: server.crt
keyfile: server.key
require_certificate: false
```

`cafile: ca.crt` is optional when `require_certificate: false`.

**Quote passwords that contain `$` or `&`** in YAML:

```yaml
  - username: hive01
    password: "your-password-with-$-and-&"
```

Do **not** put `listener 8883` lines here — the add-on creates port **8883** when certs are valid.

### 2b.1 Local clients — no changes needed

After you enable TLS, **nothing changes for LAN clients**:

- Same **port 1883**
- Same **username/password** (`mqtt`, `gate-master`, Node-RED, etc.)
- **No** client certificates, **no** `--cafile`, **no** config changes

Your existing connections in the Mosquitto log (`gate-master`, `nodered`, `AnernAN-SCI01-PRO`, HA `mqtt` user) continue on **1883** exactly as today. TLS on 8883 is an **additional** listener for field devices only.

Only reconfigure a client if you intentionally move it to 8883 (not required for anything on your LAN).

### 2c. Verify after restart

In the Mosquitto add-on log you must see:

```
Opening ipv4 listen socket on port 8883.
```

If you see **`SSL is not enabled`** instead, the add-on did not find `/ssl/server.crt` or `/ssl/server.key`. Run `ls -la /ssl/` in the Terminal add-on — see section 2a.

When SSL works, the log says **`Certificates found: SSL is available`** (not `SSL is not enabled`), then:

### 2d. Test from outside your LAN

```bash
# Plain MQTT on LAN (HA internal user)
mosquitto_pub -h 127.0.0.1 -p 1883 -u mqtt -P '...' -t test -m hello

# TLS from internet (simulates ESP32) — use phone hotspot or external host
mosquitto_pub -h YOUR_STATIC_IP -p 8883 --cafile ca.crt \
  -u hive01 -P '...' -t beekpr/hive-01/state -m '{"weight_kg":1.0}'
```

If the second command fails with *connection refused*, TLS is not listening on 8883 yet. If it fails with certificate errors, regenerate `server.crt` with `subjectAltName = IP:YOUR_STATIC_IP`.

### Common mistake: `SSL is not enabled` in HA logs

| Symptom | Cause | Fix |
|---------|-------|-----|
| HA log: `SSL is not enabled` | Certs missing or wrong path in `/ssl/` | Upload `server.crt` + `server.key`; set `certfile`/`keyfile` to **filenames only** |
| HA log: only ports 1883, 1884 | TLS listener not started | Same as above; restart add-on |
| ESP `gprs` OK, `mqtt` fails state=-2 | Step 5 is plain TCP; TLS needs ESP32 TLS (not SIM800 `+CIPSSL`) or broker on 8883 | Flash latest firmware; see SIM800 note below |
| Mosquitto `unsupported protocol` on 8883 | SIM800 modem TLS is too old for OpenSSL 3 | Fixed in firmware: TLS on ESP32, plain TCP via modem |
| Broker `exceeded timeout` ~30s after `New connection` on 8883 | Mosquitto default keepalive **20s** (×1.5 = **30s**) starts at TCP accept — TLS + MQTT CONNECT must finish in 30s | Flash latest firmware (modem pump + 50ms SIM800 RX poll). Smaller server cert (ECDSA) helps |
| ESP `SSL handshake timeout` at 120s, broker already disconnected | Broker gave up first (~31s) while ESP still waited | Same as above — handshake must finish before broker idle timeout |

### SIM800 and TLS (field devices)

The SIM800L **`+CIPSSL`** stack only speaks legacy TLS (often TLS 1.0). Mosquitto 2.x with OpenSSL 3 rejects that (`SSL routines::unsupported protocol` in broker logs). **Firmware therefore uses ESP32 mbedTLS** (`SSLClient` over plain `TinyGsmClient`): the modem opens a normal TCP socket; the ESP32 performs TLS 1.2 to port 8883. The CA in `certs/ca.pem` is used on the ESP32 only — no modem `+SSLSETCERT` upload required.

On 2G, the modem serial buffer must be serviced (`modem.maintain()`) during every TLS read/write. TinyGSM normally polls the modem RX buffer only every **500ms** — too slow when Mosquitto allows only **30 seconds** from TCP accept to MQTT CONNECT.

### Why exactly 30 seconds?

Mosquitto assigns `keepalive = 20` to every new TCP connection **before** TLS or MQTT complete (`context__init` in Mosquitto source). The broker disconnects with `exceeded timeout` after **20 × 1.5 = 30 seconds** if no MQTT packet arrives. Your Mac completes TLS + publish in under a second; the hive on 2G must finish the full TLS handshake **and** send MQTT CONNECT within that window.

There is no HA add-on setting to extend this pre-connect window. The firmware must be fast enough.

### Mosquitto 7.x (HA add-on 7.1.0)

The Home Assistant **Mosquitto broker** add-on **7.1.0** ships Mosquitto **7.x** with **OpenSSL 3** — compatible with ESP32 TLS 1.2 on port 8883. LAN clients on **1883** are unchanged.

If the log shows `New connection from … on port 8883` followed by `disconnected: exceeded timeout` **without** a line like `New client connected … as hive-01 (p2, c1, k…)`, the broker never received an MQTT CONNECT packet. The TCP socket sat idle during a **slow TLS handshake** (typical on 2G). That is a **client transport** issue, not a Mosquitto 7.1.0 regression.

What to check on 7.1.0:

| Log pattern | Meaning |
|-------------|---------|
| `New connection` → `exceeded timeout` (~30s), no client id | TLS handshake too slow or stalled — flash firmware with modem pump fix |
| `OpenSSL Error` / `Protocol error` | Cert, cipher, or TLS version mismatch — check `ca.pem` and server SAN |
| `unsupported protocol` | Old modem TLS — must use ESP32 TLS (current firmware) |
| `New client connected … as hive-01` then later `exceeded timeout` | MQTT keepalive — different issue (PubSubClient must send pings) |

Optional: in the add-on **customize** block, avoid an unnecessarily low `max_keepalive` if you set one manually. Default HA config is fine for most setups.

---

## 3. Router port forwarding

| External | Internal | Target |
|----------|----------|--------|
| TCP **8883** | TCP **8883** | Home Assistant / Mosquitto host LAN IP |

Do **not** forward port **1883**.

Verify your ISP static IP matches `YOUR_STATIC_IP`. If the ISP changes IP, update `.env` `MQTT_BROKER_HOST` and regenerate server cert SAN.

---

## 4. Home Assistant MQTT integration

HA connects to the **local plain** broker only — **no TLS, no client certificate**:

- Broker: `core-mosquitto` or `127.0.0.1`
- Port: **1883**
- Username/password: `mqtt` login from Mosquitto `logins`

Do **not** point Home Assistant at port 8883. Field devices never use 1883 over the internet.

---

## 5. ESP32 firmware configuration

In `.env`:

```bash
MQTT_BROKER_HOST=YOUR_STATIC_IP
MQTT_BROKER_PORT=8883
MQTT_USE_TLS=1
MQTT_USERNAME=hive-01
MQTT_PASSWORD=<same as Mosquitto logins>
```

Copy CA cert:

```bash
cp ~/beekpr-certs/ca.crt certs/ca.pem
```

Firmware embeds `certs/ca.pem` at build time and validates the server certificate during TLS handshake (Steps 5–6).

---

## 6. Hardening checklist

- [ ] Strong unique password per hive device
- [ ] `allow_anonymous false` on both listeners
- [ ] Port 1883 bound to `127.0.0.1` only
- [ ] Only 8883 forwarded on router
- [ ] CA key (`ca.key`) stored offline, not on HA
- [ ] Server key permissions restricted
- [ ] Consider HA / router firewall logging on 8883
- [ ] Rotate MQTT passwords if a device is lost

---

## 7. What we are not doing

- No Cloudflare tunnel
- No public DNS name required
- No plain MQTT over the internet
- No `allow_anonymous true` on external listener
