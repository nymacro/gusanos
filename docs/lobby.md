# Gusanos Lobby Protocol Specification

## Overview

The lobby covers three phases: **server discovery** (HTTP), **connection** (reliable UDP handshake), and **player registration** (in-game data exchange). All game-level lobby data is sent as tagged events over the ZoidCom reliable ordered channel (`eZCom_ReliableOrdered`).

---

## Phase 1: Server Discovery (HTTP Master Server)

**Endpoint:** `comser.liero.org.pl` / `gusserv.php`

### Client → Master Server: List Request

`POST gusserv.php` (`application/x-www-form-urlencoded`) via `Network::fetchServerList()`:

| Parameter | Value | Notes |
|---|---|---|
| `action` | `"list"` | |
| `protocol` | `"2"` | `Network::protocolVersion` |

### Master Server → Client: Response Format

Pipe-delimited (`|`), fields within each entry separated by `^`:

```
ip^title^desc^mod^map|ip^title^desc^mod^map|...
```

Each entry has 5 fields parsed by `serverListCallb`:

| Index | Field | Meaning |
|---|---|---|
| 1 | `ip` | Server address |
| 2 | `title` | `NET_SERVER_NAME` |
| 3 | `desc` | `NET_SERVER_DESC` |
| 4 | `mod` | Mod name |
| 5 | `map` | Current map name |

### Lua Binding

```lua
fetch_server_list(function(list)
    -- list is nil on failure, otherwise array of {ip, title, desc, mod, map}
    for _, s in ipairs(list) do
        -- s.ip, s.title, s.mod, s.map
    end
end)
```

---

## Phase 1b: Server Registration (Host → Master Server)

A host announces itself to the master server over HTTP so clients can discover
it via the `list` request (Phase 1). Registration is gated by the `NET_REGISTER`
console variable (default `1`). When `0`, the host never contacts the master and
is only reachable by direct IP.

### Trigger

`Network::host()` (or the queued `Host` message in `Network::update()`) calls
`registerToMasterServer()` immediately after the server control object is
created and ZoidCom classes are registered. It is **not** sent per-connection;
it is sent once when hosting starts.

### Host → Master Server: Add Request

`POST gusserv.php` with the following form fields:

| Field | Source | Notes |
|---|---|---|
| `action` | `"add"` | |
| `title` | `NET_SERVER_NAME` (`serverName`) | Shown in list field 2 |
| `desc` | `NET_SERVER_DESC` (`serverDesc`) | Shown in list field 3 |
| `port` | `NET_SERVER_PORT` (`m_serverPort`) | Host's listen port |
| `protocol` | `Network::protocolVersion` (`"2"`) | Must match client list filter |
| `mod` | `game.getModName()` | Shown in list field 4 |
| `map` | `game.level.getName()` | Shown in list field 5 |

> **Note on IP:** The request sends only `port`. The master server derives the
> server IP from the HTTP connection's source address and combines it with `port`
> to build the `ip` field (field 1) returned in a `list` response. This is why
> `registerToMasterServer()` has no `ip`/`address` field.

### Master Server → Host: Add Response

Handled by `onServerAdded()`:

| Outcome | `serverAdded` | Log |
|---|---|---|
| HTTP success | `true` | `"Registered to master server"` |
| HTTP failure (timeout) | `false` | `"Registration to master server timed out"` |
| HTTP failure (other) | `false` | `"Failed to register to master server"` |
| Null request (dropped) | `false` | — |

If registration fails, the host silently retries on the next heartbeat (see below).

### Heartbeat / Update

While hosting, `Network::update()` increments `updateTimer` each tick. Every
`6000 * 3 = 18000` ticks (≈ five minutes at 60 Hz), it sends:

- **If not yet registered** (`!serverAdded`): a fresh `add` request (re-register).
- **If already registered**: a lightweight `update` request.

| Field | Value | Notes |
|---|---|---|
| `action` | `"update"` | |
| `port` | `NET_SERVER_PORT` | Only the port is sent; the master keeps the IP from the original `add`. |

Handled by `onServerUpdate()`. On failure it resets `serverAdded = false`, so
the next heartbeat re-sends `add`.

### Host → Master Server: Remove Request

On `Network::disconnect()`, if `serverAdded` is true, the host sends a `remove`
request:

| Field | Value | Notes |
|---|---|---|
| `action` | `"remove"` | |
| `port` | `NET_SERVER_PORT` | Master de-lists by source-IP + port. |

Handled by `onServerRemoved()`, which unconditionally sets `serverAdded = false`.

### Console Variables

| Variable | Default | Effect |
|---|---|---|
| `NET_REGISTER` | `1` | `0` disables all master-server contact (add/update/remove). |
| `NET_MASTER_SERVER` | `"comser.liero.org.pl"` | Hostname of the master server contacted for add/update/remove. |
| `NET_SERVER_NAME` | `"Unnamed server"` | `title` |
| `NET_SERVER_DESC` | `""` | `desc` |
| `NET_SERVER_PORT` | `9898` | `port` (also the ZoidCom listen port) |

### Lua-Relevant

There is no Lua callback for registration success/failure; it is logged to the
console only. `NET_SET_PROXY` can redirect master-server HTTP through a proxy.

---

## Phase 2: Connection Request/Reply (ZoidCom Reliable UDP)

### 2a. Client Connects

`ZCom_Connect(address, NULL)` — no custom request payload.

### 2b. Server Reviews Request (`Server::ZCom_cbConnectionRequest`)

Server checks, in order:

| Condition | Reply | Action |
|---|---|---|
| IP is banned | `Banned` (2, 8 bits) | Reject |
| Server is in retry state | `Retry` (1, 8 bits) | Reject; client auto-retries in 50 ticks |
| Server is pre-shutdown | `Refused` (0, 8 bits) | Reject |
| **Accepted** | mod + map strings | Accept |

**On accept**, the reply payload is written:

| Field | Type | Notes |
|---|---|---|
| Mod name | `string` | `game.getMod()` |
| Map name | `string` | `game.level.getName()` |

### 2c. Client Handles Result (`Client::ZCom_cbConnectResult`)

On **accepted**, the client:

1. Reads mod string and map string from reply.
2. Checks `game.hasMod(mod)` — if missing, shows **ErrorModNotFound**.
3. Checks `game.hasLevel(map)` — if missing and auto-downloads enabled, requests updater; otherwise **ErrorMapNotFound**.
4. If both available: sets mod, loads level with `game.changeLevel(map, false)`, runs init scripts, then sends consistency info and requests ZoidMode level 1.

On **rejected**, reads 8-bit reason:

| Reason Value | Client Response |
|---|---|
| `Retry` (1) | Schedules reconnect in 50 ticks |
| `Banned` (2) | Shows "YOU ARE BANNED" |
| `Refused` (0) | Shows "COULDN'T ESTABLISH CONNECTION" |

### 2d. Post-Connection: Lua Event Registry (Server → Client)

Sent once on connection spawn in `Server::ZCom_cbConnectionSpawned`:

| Field | Encoding | Notes |
|---|---|---|
| Client event type tag | `Encoding::encode(..., 2)` | 0 = LuaEvents |
| Per group (4 groups: Game, Player, Worm, Particle): | | |
| ├── Event count | `int(8)` | 0–255 |
| └── Per event: name | `string` | Lua callback name |

Client indexes these events by name for later invocation.

### 2e. Consistency Check (Client → Server)

Sent after loading the level:

| Field | Width | Notes |
|---|---|---|
| Event tag: `ConsistencyInfo` (12) | 8 bits | `Network::NetEvents` |
| Protocol version | 32 bits | Must equal 2 |
| CRC data | variable | `game.addCRCs()` |

Server checks:

| Check | Disconnect Reason |
|---|---|
| Protocol mismatch | `IncompatibleProtocol` |
| CRC mismatch (if `NET_CHECK_CRC`) | `IncompatibleData` |

---

## Phase 3: Player Registration

### 3a. Client Sends Player Request

`Client::requestPlayer` sends after ZoidMode level 1 is granted:

| Field | Width | Notes |
|---|---|---|
| Event tag: `PLAYER_REQUEST` (10) | 8 bits | `Network::NetEvents` |
| Player name | `string` | `playerOptions.name` |
| Colour | 24 bits | RGB |
| Team | 8 bits (signed) | Team number |
| Unique ID | 32 bits | Persistent player identifier for stats |

Up to 2 requests sent sequentially for split-screen (players 0 and 1).

### 3b. Server Accepts Player (`Server::ZCom_cbDataReceived`)

1. Creates `NetWorm` owned by connection `_id`.
2. Creates `BasePlayer` with `Game::PROXY`.
3. Looks up `uniqueID` in `savedScores` map — if found, restores previous stats; if not, generates a new unique ID.
4. Sets colour, team, name.
5. Stores name → `savedNames[uniqueID]`, stats → `savedScores[uniqueID]`.
6. Assigns network authority to server, sets connection owner.
7. **No explicit reply** — the player/worm nodes are replicated to the client automatically by ZoidCom.

---

## Disconnection Events (Lobby-Relevant)

Sent as 8-bit `DConnEvents` in the disconnect payload:

| Value | Enum | Client Shows | Game Reset |
|---|---|---|---|
| 0 | `Kick` | "YOU WERE KICKED" | `Kicked` |
| 1 | `ServerMapChange` | "SERVER CHANGED MAP" | `ServerChangeMap` (+ auto-reconnect in 150 ticks) |
| 2 | `Quit` | "CONNECTION CLOSED BY SERVER" | `ServerQuit` |
| 3 | `IncompatibleData` | "YOU HAVE INCOMPATIBLE DATA" | `IncompatibleData` |
| 4 | `IncompatibleProtocol` | "THE HOST RUNS AN INCOMPATIBLE VERSION" | `IncompatibleProtocol` |

Timeout shows "CONNECTION TIMEDOUT" → `ServerQuit`.

---

## Wire Format Summary

All master-server requests use HTTP **POST** (`application/x-www-form-urlencoded`)
to `gusserv.php`; the master derives each server's IP from the TCP source address
and never receives an `ip` field.

### Part A — Discovery & registration (Host ↔ Master ↔ Client)

```
  Host                       Master                 Client
    │                           │                       │
    │  POST action=add          │                       │
    │    title/desc/port/       │                       │
    │    protocol/mod/map       │                       │
    │──────────────────────────→│                       │
    │←────────── OK ────────────│                       │
    │  (serverAdded = true)     │                       │
    │                           │                       │
    │  ... heartbeat every 18000 ticks (~5 min @ 60 Hz) ...
    │  POST action=update       │                       │
    │    (port)                 │                       │
    │──────────────────────────→│                       │
    │                           │                       │
    │                           │   POST action=list     │
    │                           │     protocol=2         │
    │                           │←───────────────────────│
    │                           │   pipe-delimited list  │
    │                           │   ip^title^desc^       │
    │                           │   mod^map per entry    │
    │                           │──────────────────────→│
    │                           │                       │
    │   Client dials Host ip:port (taken from a list entry)
    │                           │                       │
    │  ... on shutdown ...      │                       │
    │  POST action=remove (port)│                       │
    │──────────────────────────→│                       │
```

### Part B — Connection handshake & player registration (Host ↔ Client)

Once the client knows the Host's `ip:port`, all further lobby traffic is
reliable-ordered UDP over ENet (`eZCom_ReliableOrdered`); the master server is
no longer involved.

```
  Host                                                    Client
    │                                                         │
    │←──────────── ZCom_Connect(addr) ────────────────────────│
    │   (no custom request payload)                           │
    │                                                         │
    │  ZCom_cbConnectionRequest:                              │
    │    banned  → reply Banned(2, 8 bits), reject            │
    │    retry   → reply Retry(1, 8 bits),  reject            │
    │    pre-shutdown → reply Refused(0, 8 bits), reject      │
    │    else    → reply mod + map strings, accept            │
    │────────────────────────────────────────────────────────→│
    │                                                         │
    │  if rejected: client reads 8-bit reason                │
    │    Retry(1)    → reconnect in 50 ticks                 │
    │    Banned(2)   → "YOU ARE BANNED FROM THIS SERVER"      │
    │    Refused(0)  → "COULDNT ESTABLISH CONNECTION"         │
    │  if accepted:                                          │
    │    hasMod?  no → ErrorModNotFound                       │
    │    hasLevel? no → request updater (ZoidMode 2) or       │
    │                  ErrorMapNotFound                       │
    │    else: set mod, changeLevel, runInitScripts, then:    │
    │                                                         │
    │←──────── ConsistencyInfo (tag 12, 8 bits) ─────────────│
    │           protocol(32) + CRC data                      │
    │  server checks protocol==2 and (if NET_CHECK_CRC) CRCs │
    │    mismatch → disconnect IncompatibleProtocol/Data     │
    │                                                         │
    │←──────── ZCom_requestZoidMode(1) ──────────────────────│
    │                                                         │
    │  ZCom_cbConnectionSpawned: send Lua event registry      │
    │    tag LuaEvents(0, encode width 2), per group         │
    │    (Game/Player/Worm/Particle): count(8) + name(string) │
    │────────────────────────────────────────────────────────→│
    │                                                         │
    │←──────── PLAYER_REQUEST (tag 10, 8 bits) ──────────────│
    │           name(string)+colour(24)+team(s8)+uniqueID(32)│
    │  server: addWorm+addPlayer, restore/generate uniqueID, │
    │          set colour/team/name, setOwnerId, assignWorm  │
    │  (no explicit reply; nodes auto-replicate via ZoidCom)  │
    │──────── auto-replication (worm/player nodes) ──────────→│
    │                                                         │
    │                    ... gameplay ...                      │
    │                                                         │
    │←──────── ZCom_Disconnect(8-bit DConnEvents) ───────────│
    │  Kick(0) / ServerMapChange(1) / Quit(2) /               │
    │  IncompatibleData(3) / IncompatibleProtocol(4)         │
    │  timeout → "CONNECTION TIMEDOUT" → ServerQuit          │
```
