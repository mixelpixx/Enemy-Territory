# NET-3: Server Browser Revival Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make RM servers discoverable again — LAN broadcast discovery + a self-hosted master server (the dead `etmaster.idsoftware.com` is repointed to a master we run on the user's VPS), with a community master as a fallback source so the existing ET scene stays browsable. The in-game server list populates and a client can find + join an RM server over LAN and over the internet.

**Architecture:** The whole browser stack is intact stock ET, just pointed at dead hosts (research 2026-06-13). The engine code change is small: repoint the master host, confirm LAN broadcast (drop the dead IPX path), and let a fallback master slot be configured. The new component is a standard **dpmaster** daemon (the q3/ET master protocol server used by ioquake3/ET:Legacy) deployed on the VPS — NOT new engine code. Wire protocol stays 84 (no bump) so RM clients can still browse — and join where the pure/native barrier allows — original servers. ALL code is verified against a **locally-run dpmaster** first; the VPS deploy is the final go-live step. Server-only + client/UI + a tiny qcommon define; NO sim/qagame/cgame-gameplay/pmove/renderer; pmove_feel golden green.

**Key facts (verified, C:\repo\et-rm):**
- Master host: `MASTER_SERVER_NAME "etmaster.idsoftware.com"` + `MOTD_SERVER_NAME` (src/qcommon/qcommon.h:284); `PROTOCOL_VERSION 84` (qcommon.h:276); `PORT_MASTER` (27950).
- Server heartbeat: `SV_MasterHeartbeat`/`SV_MasterShutdown` (src/server/sv_main.c:229-367), name `HEARTBEAT_GAME "EnemyTerritory-1"` / `HEARTBEAT_DEAD "ETFlatline-1"`, gated by `com_dedicated->integer == 2`; `sv_master1..5` cvars (sv_init.c:797, slot 0 default = MASTER_SERVER_NAME, 1-4 CVAR_ARCHIVE empty).
- Client: `CL_GlobalServers_f` (cl_main.c:4074) sends `getservers <proto>` to PORT_MASTER; `CL_ServersResponsePacket` (cl_main.c:2193) parses `\<ip4><port2>...\EOT`; `CL_LocalServers_f` (cl_main.c:4029) broadcasts `getinfo xxx` on NA_BROADCAST + NA_BROADCAST_IPX; `CL_ServerInfoPacket`/`cl_pinglist` fill rows.
- UI: server list exists — `FEEDER_SERVERS 0x02` (etmain/ui/menudef.h), `ui_netSource` (AS_LOCAL 0 / AS_GLOBAL 1 / AS_FAVORITES 2), trap_LAN_* syscalls abstract it. playonline.menu drives it.
- AUTHORIZE_SUPPORT is `#ifdef`-gated and NOT enabled (good — no auth-server dependency).

**Tech Stack:** C (MSVC x64, CMake/Ninja) for the engine; dpmaster (C, builds on Linux) for the VPS master. VPS = orchis.ai (Ubuntu; ssh-connect MCP available). Reference: C:\repo\et-deps\etlegacy for how ETe configures its master/heartbeat.

**Build/run/git:** branch `rm/13-server-browser` off `main`. Build (PowerShell tool): the canonical vcvars+cmake+ctest one-liner. Two-instance + local-master testing in the tasks. NEVER write into C:\repo\enemy-territory-RM. Commit trailer (exact last line):
```
Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
```
Per project preference: update docs + commit at the end of EACH task.

---

## Task 1: Master host configuration — repoint + fallback slot

**Files:** `src/qcommon/qcommon.h` (MASTER_SERVER_NAME / MOTD), `src/server/sv_init.c` (sv_master defaults), `src/client/cl_main.c` (cl_master default if any), `docs/HOSTING.md`

- [ ] **Step 1:** Replace the dead `MASTER_SERVER_NAME`/`MOTD_SERVER_NAME` default in qcommon.h with a compile-time-overridable RM master host. Use a placeholder that resolves to the VPS master DNS (decide the exact hostname at deploy — e.g. `master.<user-domain>`; until then default to `""`/localhost-friendly so a missing master fails gracefully, not a 30s DNS hang on a dead host). Make it a CMake-overridable define (`-DRM_MASTER_HOST=...`) so the VPS hostname isn't hardcoded. Document the choice.
- [ ] **Step 2:** Server side (sv_init.c): keep `sv_master1` = RM master (the new default), and set a sensible default for `sv_master2` = a live community master (ET:Legacy's — verify the current live hostname, e.g. `master.etlegacy.com`, at implementation; if uncertain leave empty + document how to set it) so RM servers ALSO advertise to the community master for visibility. Heartbeat already loops all 5 slots.
- [ ] **Step 3:** Client side: the global-servers query uses MASTER_SERVER_NAME for slot 0; ensure the client can query multiple masters (it already supports `cls.masterNum`) — confirm the browser's "Internet" source aggregates results from our master + the fallback. Minimal change: make sure a query to an unreachable master times out gracefully and the other still populates.
- [ ] **Step 4:** Build + ctest 3/3. Doc: add a "Server browser / master" subsection to docs/HOSTING.md (how sv_master1..5 work, the RM master, fallback). Commit (`net-3: repoint master host + community fallback slot`).

## Task 2: LAN discovery cleanup + verify

**Files:** `src/client/cl_main.c` (CL_LocalServers_f), `docs/HOSTING.md`

- [ ] **Step 1:** In `CL_LocalServers_f` (cl_main.c:4029), the broadcast loop sends to NA_BROADCAST **and NA_BROADCAST_IPX**. IPX is dead (no IPX stack on Win11) — the IPX send either errors harmlessly or wastes time. Remove/guard the NA_BROADCAST_IPX branch (IPv4 broadcast only). Confirm NA_BROADCAST sends still go out on PORT_SERVER..+NUM_SERVER_PORTS.
- [ ] **Step 2:** Verify LAN browse end to end on ONE machine: start an RM dedicated server (or listen server) bound to the LAN; from a second client instance, in the UI set the source to Local (or console `localservers`) and confirm the server appears with correct hostname/map/players/ping via the `getinfo` path. (Loopback/localhost broadcast behavior: confirm NA_BROADCAST reaches a same-host server; if Windows loopback broadcast is unreliable, test against the machine's LAN IP. Document the method.)
- [ ] **Step 3:** Build + ctest 3/3. Doc the LAN-browse behavior. Commit (`net-3: LAN discovery (IPv4 broadcast), drop dead IPX path`).

## Task 3: Local dpmaster bring-up + full internet-browse flow (no VPS yet)

**Files:** none in-repo (test harness); `docs/HOSTING.md` (master setup notes); a vendored/reference note in `docs/THIRDPARTY.md` if we ship/point at dpmaster

- [ ] **Step 1:** Obtain dpmaster (the canonical q3/ET master; ETe/ioq3 use it). Build/run it LOCALLY on the dev box (Windows build, or WSL/Linux — whichever runs; document). Configure it for ET: it must accept `heartbeat EnemyTerritory-1`, track servers by protocol 84, and answer `getservers 84` with the `\<ip><port>...\EOT` list. dpmaster's gamename/protocol config — set for ET. (dpmaster is GPL; if we end up shipping a build or config, log it in THIRDPARTY.md. We are NOT vendoring it into the engine — it's a separate daemon.)
- [ ] **Step 2:** Point an RM dedicated server's `sv_master1` at the local dpmaster (`+set sv_master1 127.0.0.1` or the LAN IP, `+set dedicated 2` so it heartbeats). Confirm dpmaster logs the heartbeat and registers the server (dpmaster verbose/-v output, or query it).
- [ ] **Step 3:** Point an RM client's master query at the local dpmaster and run `globalservers 0 84` (or use the UI Internet tab). Confirm `getservers` → `getserversResponse` returns the registered server, the client pings it (getinfo), and it shows in the browser with correct details. Then JOIN it and confirm play. This proves the entire internet-browse path against a real master, locally.
- [ ] **Step 4:** ctest 3/3 (no code change expected in this task — it's the integration proof; if a bug surfaces, fix in client/server net code, re-verify). Doc the master setup recipe in HOSTING.md. Commit (`net-3: verified internet-browse flow against local dpmaster`).

## Task 4: VPS master deployment (go-live) — CONFIRM BEFORE EXPOSING

**Files:** `docs/HOSTING.md` (the live master address + ops notes). Infra: orchis.ai via ssh-connect MCP.

> **GATE:** this task makes an outward-facing change on the user's live VPS (a public UDP service + firewall rule). Confirm with the user before opening the port / exposing the service. The VPS (orchis.ai) currently runs nginx + phpBB3 + MySQL — the master coexists as a separate UDP daemon, must not disturb the web stack.

- [ ] **Step 1:** Deploy dpmaster on orchis.ai: build/install it, create a systemd unit to run it (unprivileged user, restart-on-failure), bind UDP PORT_MASTER (27950) — verify nothing else uses it. Do NOT open the firewall yet.
- [ ] **Step 2:** Firewall (ufw): after user confirmation, allow UDP 27950 in. Confirm the web stack's ports (80/443/etc.) and the firewall posture are unchanged otherwise.
- [ ] **Step 3:** DNS/hostname: set the RM master hostname (Task 1's `RM_MASTER_HOST`) to resolve to the VPS (a subdomain A record). Rebuild RM with `-DRM_MASTER_HOST=<that host>` (or rely on the sv_master cvar). 
- [ ] **Step 4:** End-to-end live test: an RM dedicated server (could be the user's, or a temporary one) with `dedicated 2` heartbeats to the live master; an RM client on a different network runs the Internet browse and sees + joins it. Confirm the community-master fallback slot also returns the existing scene. Document the live master address in HOSTING.md.
- [ ] **Step 5:** Commit the docs (`net-3: live VPS master deployed + documented`). (Infra config lives on the VPS / in ops notes, not the repo, except the documented address + the systemd unit captured in docs or a deploy/ dir if the user wants it version-controlled.)

## Task 5: Finalize — verify, docs, hand off

- [ ] **Step 1:** Full matrix: LAN browse (find+join), internet browse via RM master (find+join), community-master fallback (original servers visible), graceful behavior when a master is unreachable (no hang, other sources still populate). ctest 3/3.
- [ ] **Step 2:** Engine-touch audit: `git diff main...HEAD --stat` — expected: qcommon.h (master host), sv_init.c (master defaults), cl_main.c (LAN IPv4 + master query), docs/HOSTING.md, README.md (one line: server browser revived, point at your own/community master), plan doc. Confirm NO sim/qagame/cgame-gameplay/pmove/renderer touched. pmove_feel green.
- [ ] **Step 3:** Clean from-scratch build + ctest 3/3 + a browse smoke. README hosting section: note the server browser works (LAN + master) and how to run your own master. Hand off for USER acceptance (browse LAN, browse via the live VPS master, join a server). Do NOT merge/tag — after acceptance: merge `--no-ff` to `main`, tag `rm-server-browser`.

---

## Self-Review
Scope covers the user's decision (LAN + self-hosted VPS master) plus the fallback-to-community-master they were curious about (so original servers stay visible — interop preserved by keeping protocol 84). The whole browser stack already exists, so the engine work is minimal (repoint + IPv4-broadcast cleanup + graceful multi-master) — the substance is the dpmaster deployment, structured so ALL code is verified against a LOCAL master (Task 3) before any VPS exposure (Task 4), and the outward-facing VPS step is explicitly gated on user confirmation. No sim/renderer contact; pmove_feel green every task; docs+commit per task per project preference. Open items to resolve at implementation: the exact RM master hostname/subdomain and the current live community-master hostname (verify, don't guess). The pure/native barrier (can't *play* on stock pure servers) is inherent from NET-1 and documented, not in scope to "fix" here.
