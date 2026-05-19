# Deskflow Internet Relay

Enables Deskflow to share keyboard and mouse **across the internet** (not just your local network) and supports **bidirectional peer control**.

## How it works

```
Device A ──► Relay Server ──► Device B
Device B ──► Relay Server ──► Device A   (peer mode)
```

The relay server is a lightweight Node.js TCP proxy.  It pairs two peers that share the same **room code** and then transparently forwards the raw Deskflow protocol bytes between them.  Neither device needs to open any firewall ports or have a public IP address.

---

## Quick start

### 1. Host the relay server

You need one publicly reachable server (VPS, cloud VM, etc.).  The relay only needs **Node.js ≥ 16**.

```bash
# On your VPS
cd relay/
node server.js                 # listens on port 24801
# Or specify a different port:
RELAY_PORT=12345 node server.js
```

With Docker:
```bash
docker compose up -d           # uses docker-compose.yml in this directory
```

### 2. Configure Deskflow on each device

Open **Deskflow → Preferences → Internet** tab:

| Field | Value |
|-------|-------|
| Relay server | `your-server.example.com` |
| Port | `24801` |
| Room code | A shared secret (click **Generate** or type your own) |
| This device role | `server` on the controlling device, `client` on the controlled device |

Click **Save** and restart Deskflow.

> All devices that should be connected must use the **same relay server + room code**.

---

## Bidirectional control (peer mode)

In **peer** mode every device can both control and be controlled by every other device simultaneously.  This requires **two room codes** per device pair — one for each direction.

| Device A settings | Device B settings |
|-------------------|-------------------|
| Role: `peer` | Role: `peer` |
| Room code: `room-forward` | Room code: `room-reverse` |
| Reverse room code: `room-reverse` | Reverse room code: `room-forward` |

Under the hood deskflow runs as both server (in room `room-forward`) and client (in room `room-reverse`) simultaneously.

---

## Local relay proxy (no C++ changes needed)

If you prefer not to recompile Deskflow, use the included `proxy.js` script to bridge the relay to a stock Deskflow installation:

**Device A (server/controller):**
```bash
# Start standard Deskflow server on port 24800 (default)
# Then run the proxy:
node relay/proxy.js server relay.example.com 24801 my-room-code
```

**Device B (client/controlled):**
```bash
# Run the proxy (it will listen locally on port 24800)
node relay/proxy.js client relay.example.com 24801 my-room-code
# Then configure Deskflow client to connect to 127.0.0.1:24800
```

**Bidirectional with proxy:**
```bash
# Device A — controls B (server role in room "A-to-B")
node relay/proxy.js server relay.example.com 24801 A-to-B

# Device A — is controlled by B (client role in room "B-to-A", local port 24801)
node relay/proxy.js client relay.example.com 24801 B-to-A 24801

# Device B — is controlled by A (client role in room "A-to-B")
node relay/proxy.js client relay.example.com 24801 A-to-B

# Device B — controls A (server role in room "B-to-A")
node relay/proxy.js server relay.example.com 24801 B-to-A 24800
```

---

## Self-hosted relay — Docker Compose

```yaml
# docker-compose.yml already included in relay/
services:
  deskflow-relay:
    build: .
    ports:
      - "24801:24801"
    restart: unless-stopped
    environment:
      RELAY_PORT: 24801
      RELAY_ADMIN_PORT: 8080   # optional health endpoint
    ports:
      - "24801:24801"
      - "8080:8080"
```

```bash
docker compose up -d
# Health check:
curl http://localhost:8080/health
# {"status":"ok","rooms":0}
```

---

## Security notes

- The relay forwards **raw bytes** — it cannot read the Deskflow protocol (which is encrypted with TLS when you enable the Security settings).
- The **room code** acts as a shared secret.  Use a long random value (click **Generate** in the UI).
- For maximum security, enable TLS in Deskflow settings AND use a strong room code.
- You can run a **private relay** that only accepts connections from trusted IPs via a firewall rule on your VPS.

---

## Relay server options

| Environment variable | Default | Description |
|---------------------|---------|-------------|
| `RELAY_PORT` | `24801` | TCP port to listen on |
| `RELAY_ADMIN_PORT` | (unset) | If set, enables an HTTP health/room endpoint |

Admin endpoints (when `RELAY_ADMIN_PORT` is set):
- `GET /health` → `{"status":"ok","rooms":<count>}`
- `GET /rooms` → list of active rooms and their state
