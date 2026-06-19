# Hosting a server

RM ships its client-side game logic (`cgame` and `ui`) as native 64-bit DLLs.
A pure server (`sv_pure 1`, which is the default for *Host Game*) refuses to
run unless the `cgame` module can be located inside a `.pk3`, so the build packs
both client modules into `etmain/rm_bin.pk3`. With that pak present, pure
servers start and matched clients load `cgame`/`ui` straight from the pak. This
applies to both listen servers (*Host Game* / `devmap`) and dedicated servers
(`etrmded`).

## Listen server (Host Game)

No setup beyond a normal RM install. *Host Game* in the menu, or from the
console:

```
sv_pure 1
devmap oasis
```

The local client loads `cgame`/`ui` from `etmain/rm_bin.pk3` automatically.

## Dedicated server

A dedicated server runs `etrmded.exe` and loads only the server-side game
module (`qagame`), which stays a loose DLL. It never loads `cgame`/`ui`, but it
still performs the pure-server check for `cgame`, so `rm_bin.pk3` must be
present.

A dedicated install needs, under `fs_basepath` / `fs_homepath`:

- `etrmded.exe`
- `qagame_mp_x86_64.dll` — loose, next to the executable (loaded from
  `<fs_basepath>/etmain/` or the working directory, not from a pak)
- `etmain/rm_bin.pk3` — supplies `cgame`/`ui` so the pure check passes and so
  connecting clients can pull the matching modules
- the retail data paks in `etmain/`: `pak0.pk3`, `pak1.pk3`, `pak2.pk3`,
  `mp_bin.pk3`

Minimal launch:

```
etrmded.exe +set dedicated 1 +set sv_pure 1 +set net_port 27960 +map oasis
```

(Add `+set fs_basepath <path>` / `+set fs_homepath <path>` if the data and the
executable are not co-located.) The server spawns the map and accepts matched
clients; `sv_pure 1` works.

## Client requirements

Clients must run the **same RM build** as the server. Pure validation compares
the client's loaded paks against the server's, and `rm_bin.pk3` carries the
client modules, so a client with a different (or missing) `rm_bin.pk3` — i.e. a
different RM build — is rejected. The client sees a plain-language dialog ("This
server is running a different ET-RM build. Update your client...") and returns
cleanly to the menu; no mismatched code is ever loaded.

## Security model

A client only ever loads client game modules (`cgame`/`ui`) that are present in
its **own install** at clean startup. The engine freezes the set of
content-checksums of all paks loaded at startup (`FS_CaptureTrustedModulePaks`),
and module bytes are only served from a pak in that frozen set
(`FS_CL_ExtractFromPakFile`). A pak that appears later — for example one pulled
in by a server-triggered download or `FS_Restart` after connecting — is refused
as a module source, and the load fails closed. This means a server can never
push executable client code; the worst a build mismatch can do is the
"update your client" rejection above.

Consequences and scope:

- Pure servers are for **matched clients** (same build): LAN, same-build groups,
  and dedicated servers whose operator ships the matching `rm_bin.pk3`.
- This is **not** a public auto-download server. A client cannot fetch and run a
  server's modules; it must already have the matching build installed.
- Custom-map distribution over HTTP (downloading map *content*, not code) is a
  separate future feature and is unrelated to this module-load guard.

## DoS protection (`sv_protect`)

The original engine answers a small set of *connectionless* (out-of-band) UDP
queries — `getstatus`, `getinfo`, `getchallenge` — without any handshake. Each
elicits a reply that is larger than the request, which lets an attacker spoof a
victim's source address and use the server as a traffic amplifier (a reflected /
distributed-reflected denial-of-service, "DRDoS"). RM ports the established
ET:Legacy / ioquake3 mitigations and gates them behind the `sv_protect` cvar.

`sv_protect` is a bitflag cvar (archived); default `1`:

| Bit | Name        | Effect |
|-----|-------------|--------|
| 1   | `SVP_IOQ3`  | Leaky-bucket rate limiting on the `getstatus`/`getinfo`/`getchallenge` out-of-band handlers. Each source address is limited to roughly 10 requests/second, and a single global cap limits the total rate at which the server emits `getstatus`/`getinfo` reflection replies. Over-limit requests are dropped silently. |
| 2   | `SVP_OWOLF` | DRDoS reflection protection. The server keeps a short ring of recent reply receipts (time + source); if it has already sent 3 or more replies to the same source within a ~2-second window, or if the whole ring is saturated within that window, further `getstatus`/`getinfo`/`getchallenge` replies to that source are suppressed. |

Under `SVP_IOQ3` the `rcon` handler is additionally hardened: incoming `rcon`
requests are per-IP rate-limited (roughly 10/second per source, loopback exempt),
and a separate bucket throttles attempts that supply a wrong or empty password —
slowing brute-force / dictionary guessing without ever throttling legitimate rcon
(a correct password is not charged against that bucket). This is in addition to
the pre-existing global 500 ms throttle, which is retained.

Recommended settings:

- **Default (`1`)** is fine for LAN and private/same-build groups. The limits
  are deliberately generous, so normal browser refreshes and LAN play are never
  affected.
- **Public / internet-facing servers:** set `sv_protect 3` to enable both the
  leaky-bucket limiter and DRDoS reflection protection.

The local client of a listen server is always exempt: loopback traffic
(`NA_LOOPBACK`) bypasses every limiter and the DRDoS check, so hosting *Host
Game* and playing on the same machine is never throttled regardless of
`sv_protect`. Dropped requests are noted only in developer output
(`developer 1`); they are not fatal and require no operator action.

There are no separate tuning cvars: the rate is governed by the built-in
~10 requests/second-per-source limits, which are intentionally generous and
suit normal hosting without adjustment. Operators choose behaviour by setting
`sv_protect` (`0` off, `1` rate limiting, `3` rate limiting + DRDoS).

## Server browser / master server

ET advertises dedicated servers to players through *master servers*: a server
sends periodic **heartbeats** to each configured master, and clients query a
master for the server list shown on the Internet tab of the in-game browser.

The original id master `etmaster.idsoftware.com` is **dead** and has been
removed as a baked-in default — pointing at it was the bug NET-3 fixes. There
is no hardcoded master in a stock build; you configure one of three ways.

### Server side — `sv_master1` .. `sv_master5`

A dedicated server (`dedicated 2`, i.e. public internet) heartbeats up to five
master slots:

- **`sv_master1`** — defaults to the build-time master (see `RM_MASTER_HOST`
  below). In a normal build with no baked-in host this is **empty**, which is
  harmless: empty slots are skipped, so the server starts cleanly with no DNS
  hit and no "couldn't resolve" hang. Set it at runtime to your RM master, e.g.
  `set sv_master1 master.example.com` in your `server.cfg`.
- **`sv_master2`** — defaults to **`master.etlegacy.com`**, a live community
  master (ET:Legacy). This keeps RM dedicated servers visible to the existing ET
  scene out of the box. It is archived, so to opt out just set it empty:
  `set sv_master2 ""`.
- **`sv_master3` .. `sv_master5`** — empty by default; archived. Add extra
  masters here if you want.

Heartbeats are only sent when `dedicated` is `2`. A `LAN`-only server
(`dedicated 1`) never contacts a master and is found via LAN discovery instead.
If a master fails to resolve, that slot is cleared automatically so the server
does not take repeated DNS hits — it never blocks startup or gameplay.

### Client side — `cl_master`

The Internet tab of the browser queries the host in **`cl_master`** (archived;
defaults to the build-time master, empty in a stock build). Point it at your RM
or a community master to populate the Internet tab, e.g.
`set cl_master master.example.com`. With an empty `cl_master` the Internet tab
simply returns no servers instead of hanging the UI; the LAN tab and direct
`connect <ip>` are unaffected.

### LAN discovery — the Local tab / `localservers`

The **Local** tab of the in-game browser (or the `localservers` console command)
finds servers on the local network *without* any master server. The client sends
a connectionless `getinfo` query as an **IPv4 broadcast** (`255.255.255.255`) to
each server port in the `PORT_SERVER` range, and any reachable RM/ET server
replies with an `infoResponse` (hostname, map, player count, gametype, ping) that
populates the list. This is the same reply path a direct ping to a known LAN IP
exercises, so a server can also be found by `connect <lan-ip>` even if broadcast
is filtered.

A `dedicated 1` (LAN-only) server never contacts a master and relies entirely on
this broadcast discovery. `dedicated 2` additionally heartbeats the configured
masters for the Internet tab.

> **RM/NET-3:** LAN discovery now emits **IPv4 broadcasts only**. The legacy IPX
> broadcast (`NA_BROADCAST_IPX`) has been removed from the client browse path —
> IPX is dead on modern platforms and only added a wasted send per port. The
> port range and the `getinfo` payload are unchanged, so discovery of existing
> ET servers is unaffected.

### Build-time default — `RM_MASTER_HOST`

To bake a default master host into the binaries (used at go-live), configure
with:

```
cmake -S . -B build -DRM_MASTER_HOST=master.example.com
```

`RM_MASTER_HOST` **defaults to `31.97.133.47`** (the project VPS master below), so
a plain `cmake` build auto-points `sv_master1` / `cl_master` (via
`MASTER_SERVER_NAME` / `MOTD_SERVER_NAME`) at it — the browser works out of the
box. Override with `-DRM_MASTER_HOST=<host>` (e.g. a DNS name), or
`-DRM_MASTER_HOST=""` for a master-less build configured purely at runtime. The
`sv_master2` community fallback (`master.etlegacy.com`) applies regardless.

**Summary:** an empty/unreachable master is always harmless — it is skipped on
the server and yields an empty Internet list on the client; nothing hangs.

### Live RM master server

A master is deployed and running on the project VPS:

```
31.97.133.47:27950   (UDP)
```

It runs **dpmaster** (the standard Quake3/ET master daemon, which has built-in
Wolfenstein: ET support — gamename `et`, protocol 84) as a hardened, unprivileged
`systemd` service (`dpmaster.service`, enabled at boot). It validates each server
with a `getinfo` round-trip before listing it, so it can't be used to list
arbitrary spoofed addresses. UDP 27950 is open at both the OS firewall (`ufw`) and
the Hostinger cloud firewall.

To use it without rebuilding:
- Server: `set sv_master1 31.97.133.47` (with `dedicated 2` so it heartbeats).
- Client: `set cl_master 31.97.133.47`, then open the Internet tab.

To bake it in as the default for a release build: `-DRM_MASTER_HOST=31.97.133.47`
(or point a DNS name at the VPS and use that). Verified live end-to-end: a server
heartbeats in, dpmaster validates it, and a client `getservers 84` query returns
it — over the public internet.

### Testing the Internet-browse flow against a local master

You can prove the whole heartbeat → getservers → ping → connect path on one
machine, with no VPS and no third-party software, using the test harness
`docs/superpowers/evidence-scripts/mock_master.py`. It is a ~150-line Python
stand-in for a q3/ET master: it binds UDP `27950`, records a server's IP:port
when it receives a `heartbeat`, and answers `getservers <proto>` with a
`getserversResponse` framed exactly as the client parser
(`CL_ServersResponsePacket`, `src/client/cl_main.c`) expects. It logs every
packet, so the log is the evidence the flow worked.

1. **Mock master** (background):

   ```
   python docs/superpowers/evidence-scripts/mock_master.py --bind 127.0.0.1 --port 27950
   ```

2. **Dedicated server** — `dedicated 2` (public) so it heartbeats; point its
   one master slot at the mock and clear the community fallback. Use a separate
   `fs_homepath` holding the loose `qagame_mp_x86_64.dll` + `rm_bin.pk3`, with
   `fs_basepath` pointing at the install that has the retail paks:

   ```
   etrmded.exe +set dedicated 2 +set sv_master1 127.0.0.1 +set sv_master2 "" \
       +set net_port 27960 +set sv_hostname "RM-MASTER-TEST" \
       +set fs_basepath <install> +set fs_homepath <ded-home> +map oasis
   ```

   The mock log shows `heartbeat EnemyTerritory-1` arriving from
   `127.0.0.1:27960`.

3. **Client** — point `cl_master` at the mock and drive the query headlessly.
   `+set logfile 2` writes `etmain/etconsole.log` under `fs_homepath`:

   ```
   etrm.exe +set cl_master 127.0.0.1 +set developer 1 +set logfile 2 \
       +set fs_basepath <install> +set fs_homepath <cl-home> \
       +globalservers 0 84 +wait 100 +serverstatus 127.0.0.1:27960 \
       +wait 50 +ping 127.0.0.1:27960 +wait 100 +quit
   ```

   The client log shows `Requesting servers from master 127.0.0.1...`, then
   `CL_ServersResponsePacket` / `1 servers parsed`, then the `getinfo` ping and
   `infoResponse` filling `sv_hostname RM-MASTER-TEST` / `mapname oasis`. A
   plain `+connect 127.0.0.1:27960` then completes the
   `challengeResponse`/`connectResponse` handshake (`Connected to a pure
   server.`) and loads the map.

   > Note: the parser's developer-only line `server: 0 ip: 127.0.0.1:14445`
   > prints the port byte-swapped (`14445` = `0x386D`, the network-order form of
   > `27960` = `0x6D38`). This is a cosmetic quirk of the stock `Com_DPrintf`
   > after `BigShort`; the *stored* address is correct, as the successful ping
   > and connect to `:27960` confirm.
