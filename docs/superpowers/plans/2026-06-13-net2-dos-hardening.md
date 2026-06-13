# NET-2: Server DoS Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harden the dedicated/listen server against connectionless-packet floods, reflection/amplification (DRDoS), and rcon brute-force by porting ET:Legacy's leaky-bucket rate limiter + DRDoS receipt tracking, gated by a new `sv_protect` cvar — the "required before public exposure" gate for the server browser increment that follows.

**Architecture:** RM's server has NO out-of-band (OOB) rate limiting today (verified: no `leakyBucket`/`SVC_RateLimit`/`sv_protect`/DRDoS anywhere in `src/`). The amplifier commands `getstatus`/`getinfo` reply with large payloads to any spoofable source; `getchallenge`/`connect` and `rcon` have only weak/global throttles. We port ET:Legacy v2.84.0's production-grade hardening (same id Tech 3 lineage, GPL — reference clone at `C:\repo\et-deps\etlegacy\src\server\`) almost verbatim, adapting two things: **IPv4-only** (RM's `netadr_t` has no IPv6 yet — drop the `NA_IP6` branches) and **swap ET:Legacy's attack-log subsystem (`SV_WriteAttackLogD`) for `Com_DPrintf`** (don't port the whole attack-log file machinery). Server-only: touches `src/server/*` + cvar registration; NO sim/qagame/cgame/renderer; the `pmove_feel` golden hash is unaffected.

**Tech Stack:** C, MSVC x64, CMake/Ninja. Reference: `C:\repo\et-deps\etlegacy` (v2.84.0) `src/server/sv_main.c` + `src/server/server.h`.

**Reference map (ET:Legacy, the port source):**
- `server.h`: `leakyBucket_t` (struct, ~450-477), `MAX_BUCKETS 16384`, `MAX_HASHES 1024`; `receipt_t`/`MAX_INFO_RECEIPTS 48` (~354-361), `serverStatic_t.infoReceipts[]` (~399); `SVP_IOQ3 0x1` / `SVP_OWOLF 0x2` (~56-57).
- `sv_main.c`: `SVC_HashForAddress` (~497), `SVC_BucketForAddress` (~542), `SVC_RateLimit` (~649), `SVC_RateLimitAddress` (~692), `SV_CheckDRDoS` (~915); integration in `SVC_Status` (~719), `SVC_Info` (~789), `SVC_RemoteCommand` (~1045), and the DRDoS calls in `SV_ConnectionlessPacket` (~1154/1169/1177).

**RM integration points (verified on `C:\repo\et-rm`):**
- `src/server/sv_main.c`: `SVC_Status` ~424, `SVC_Info` ~552, `SVC_RemoteCommand` ~653, `SV_ConnectionlessPacket` ~732 (Huff_Decompress on "connect" ~740; dispatch getstatus ~750 / getinfo ~752 / getchallenge ~754 / rcon ~763).
- `src/server/sv_client.c`: `SV_GetChallenge` ~53.
- `src/server/server.h`: `serverStatic_t` (svs) struct + `challenge_t`; add structs/defines here.
- `src/server/sv_init.c`: cvar registration (~772 region, near `sv_floodProtect`).
- `src/server/server.h` cvar externs block: add `extern cvar_t *sv_protect;`.

**CRITICAL adaptation — loopback/local exemption:** the rate limiter must NOT throttle `NA_LOOPBACK` (and ideally LAN) sources, or a listen server's own local client and single-machine testing will break (the local client's getchallenge/connect comes from loopback). Every rate-limit/DRDoS call site must early-out for loopback. Verify how RM detects this (`NET_IsLocalAddress` / `Sys_IsLANAddress` / `from.type == NA_LOOPBACK`) and guard accordingly.

**Build/run/git:** branch `rm/12-dos-hardening` off `main` (already created). Build (PowerShell tool):
```
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 && cmake -S C:\repo\et-rm -B C:\repo\et-rm\build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build C:\repo\et-rm\build && ctest --test-dir C:\repo\et-rm\build --output-on-failure'
```
Dedicated server + flood testing recipe in Task 5. NEVER write into `C:\repo\enemy-territory-RM`. Commit trailer (exact last line):
```
Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
```

---

## Task 1: Data structures + `sv_protect` cvar

**Files:** `src/server/server.h` (structs/defines/extern), `src/server/sv_init.c` (cvar registration), `src/server/sv_main.c` (cvar definition)

- [ ] **Step 1:** In `src/server/server.h`, add (porting verbatim from ET:Legacy `server.h`, IPv4-only — keep only the `_4`/`ip` fields, drop `ipv._6`/`NA_IP6`): the `leakyBucket_t` struct (fields: `netadrtype_t type; union { byte _4[4]; } ipv; int lastTime; signed char burst; long hash; leakyBucket_t *prev, *next;`), `#define MAX_BUCKETS 16384`, `#define MAX_HASHES 1024`; the `receipt_t` struct (`netadr_t adr; int time;`) + `#define MAX_INFO_RECEIPTS 48`; and the protection flag defines `#define SVP_IOQ3 0x0001` and `#define SVP_OWOLF 0x0002`. Add `receipt_t infoReceipts[MAX_INFO_RECEIPTS];` to the `serverStatic_t` struct (the `svs` global). Add `extern cvar_t *sv_protect;` to the cvar extern list. Add prototypes `qboolean SVC_RateLimit(leakyBucket_t *bucket, int burst, int period);` and `qboolean SVC_RateLimitAddress(const netadr_t *from, int burst, int period);` + `extern leakyBucket_t outboundLeakyBucket;`.
- [ ] **Step 2:** In `src/server/sv_main.c`, near the other `cvar_t *sv_*` definitions (top of file), add `cvar_t *sv_protect;`.
- [ ] **Step 3:** In `src/server/sv_init.c`, near the `sv_floodProtect` registration (~772), register: `sv_protect = Cvar_Get( "sv_protect", "1", CVAR_ARCHIVE );` with a comment documenting the bitflags (1=SVP_IOQ3 leaky-bucket OOB rate limiting, 2=SVP_OWOLF DRDoS reflection protection; default 1 enables the rate limiter). DECISION: default `"1"` (IOQ3 rate-limiting on) — the limits are generous (10 req/sec/IP) so normal clients and LAN are unaffected, and loopback is exempt (Task 2). DRDoS (bit 2) defaults off to avoid surprising existing setups; document that `sv_protect 3` enables both, recommended for public servers.
- [ ] **Step 4:** Build (compiles with the new struct fields + cvar; functions come next). `ctest` 3/3. Commit (`net-2: DoS-hardening data structures + sv_protect cvar`).

## Task 2: Leaky-bucket core (rate limiter), IPv4-only, loopback-exempt

**Files:** `src/server/sv_main.c` (new static functions in the CONNECTIONLESS COMMANDS section, before `SVC_Status`)

- [ ] **Step 1:** Port `SVC_HashForAddress` from ET:Legacy `sv_main.c:497` — IPv4-only: hash only `address->ip` (4 bytes); drop the `NA_IP6` case; keep `hash &= (MAX_HASHES - 1)`. Use RM's millisecond clock (`Sys_Milliseconds()` — verify the exact name RM uses in sv_main.c; ET:Legacy uses `Sys_Milliseconds`).
- [ ] **Step 2:** Port the bucket statics + `SVC_BucketForAddress` (ET:Legacy `sv_main.c:488-638`): `static leakyBucket_t buckets[MAX_BUCKETS]; static leakyBucket_t *bucketHashes[MAX_HASHES]; leakyBucket_t outboundLeakyBucket;` then the find-or-allocate-with-LRU-reclaim logic, IPv4-only (`NA_IP` branch only). Replace `SV_WriteAttackLogD(...)` with `Com_DPrintf(...)`.
- [ ] **Step 3:** Port `SVC_RateLimit` (ET:Legacy `sv_main.c:649`) verbatim (it's address-type-agnostic) — replace `SV_WriteAttackLogD` with `Com_DPrintf`. Port `SVC_RateLimitAddress` (`sv_main.c:692`) — but add at the TOP: `if ( from->type == NA_LOOPBACK ) { return qfalse; }` (never rate-limit the local client — `qfalse` = "not limited, allow"). Verify RM's loopback enum name (`NA_LOOPBACK`).
- [ ] **Step 4:** Build (functions compile, not yet called). `ctest` 3/3. Commit (`net-2: leaky-bucket rate limiter (IPv4, loopback-exempt)`).

## Task 3: Rate-limit the amplifiers + DRDoS receipt tracking

**Files:** `src/server/sv_main.c` (`SVC_Status`, `SVC_Info`, `SV_ConnectionlessPacket`), `src/server/sv_client.c` (`SV_GetChallenge`)

- [ ] **Step 1:** In `SVC_Status` (sv_main.c ~424), at the top (after the function's existing locals, before building the response): add, guarded by the cvar —
```c
// DoS: rate-limit inbound per-IP, and cap total outbound reflection bandwidth
if ( sv_protect->integer & SVP_IOQ3 ) {
    if ( SVC_RateLimitAddress( &from, 10, 1000 ) ) {
        Com_DPrintf( "SVC_Status: rate limit from %s exceeded, dropping request\n", NET_AdrToString( from ) );
        return;
    }
    if ( SVC_RateLimit( &outboundLeakyBucket, 10, 100 ) ) {
        Com_DPrintf( "SVC_Status: outbound rate limit exceeded, dropping request\n" );
        return;
    }
}
```
(Match `from`'s actual type — `SVC_Status` takes `netadr_t from`; pass `&from`.)
- [ ] **Step 2:** In `SVC_Info` (sv_main.c ~552), add the identical guarded block (messages say "SVC_Info"). Both are the classic amplifiers; both get inbound per-IP (10/1000ms) + shared outbound (10/100ms) limits.
- [ ] **Step 3:** In `SV_GetChallenge` (sv_client.c ~53), add the same inbound per-IP guard at the top (after locals): `if ( ( sv_protect->integer & SVP_IOQ3 ) && SVC_RateLimitAddress( &from, 10, 1000 ) ) { Com_DPrintf(...); return; }`. (getchallenge is solicited by floods to fill the challenge table — limit it. No outbound bucket needed; the response is tiny.) Confirm `SV_GetChallenge`'s param name for the address.
- [ ] **Step 4:** Port `SV_CheckDRDoS` (ET:Legacy `sv_main.c:915`) into RM `sv_main.c` (IPv4-only; uses `svs.infoReceipts[]`, `MAX_INFO_RECEIPTS`; replace attack-log with `Com_DPrintf`). It tracks recent getstatus/getinfo responses and refuses to reply if a specific source has ≥3 responses in the last 2s (it's being used as a reflector) or the global receipt count is saturated. Then in `SV_ConnectionlessPacket` (sv_main.c ~732), guard the `getstatus`/`getinfo`/`getchallenge` dispatch with: `if ( ( sv_protect->integer & SVP_OWOLF ) && SV_CheckDRDoS( from ) ) { return; }` BEFORE calling the handler (mirror ET:Legacy lines 1154/1169/1177). Loopback-exempt `SV_CheckDRDoS` at its top.
- [ ] **Step 5:** Build; `ctest` 3/3. Commit (`net-2: rate-limit getstatus/getinfo/getchallenge + DRDoS receipts`).

## Task 4: rcon per-IP throttle + connect-decompression sanity

**Files:** `src/server/sv_main.c` (`SVC_RemoteCommand` ~653, `SV_ConnectionlessPacket` ~740)

- [ ] **Step 1:** In `SVC_RemoteCommand` (sv_main.c ~653), the existing protection is only a GLOBAL 500ms throttle (`static unsigned int lasttime`). Add per-IP hardening (ET:Legacy `sv_main.c:1045`): at the top, guarded by `sv_protect->integer & SVP_IOQ3`, `if ( SVC_RateLimitAddress( &from, 10, 1000 ) ) { Com_DPrintf("SVC_RemoteCommand: rate limit from %s exceeded\n", NET_AdrToString(from)); return; }`. Then, ON A BAD PASSWORD only, charge a per-request bucket to throttle brute force: keep ET:Legacy's pattern (`static leakyBucket_t bucket;` charged `SVC_RateLimit(&bucket, 10, 1000)` when the password is wrong) so valid rcon is unthrottled but guessing is slowed. Keep the existing global 500ms throttle too (belt-and-suspenders) or replace it — match ET:Legacy (they keep the per-IP + bad-password buckets as the primary). Document the choice.
- [ ] **Step 2:** Connect-decompression sanity: in `SV_ConnectionlessPacket` (~740), the `"connect"` path calls `Huff_Decompress( msg, 12 )` which allocates a 64KB stack buffer and decompresses attacker-controlled data on EVERY connect packet. Verify RM's `Huff_Decompress` (huffman.c:343) already clamps the output to `msg->maxsize` (it does — `cch` clamped to `mbuf->maxsize - offset`, and `MAX_MSGLEN 32768` bounds the buffer). Since it's already bounded, do NOT add redundant clamping; instead add a one-line comment at the call site noting the decompression is output-bounded by MAX_MSGLEN (documents that this was reviewed, not missed). If the review finds it is NOT adequately bounded, add a guard — but the research indicates it is. (No code change expected here beyond the comment unless the bound is found insufficient.)
- [ ] **Step 3:** Build; `ctest` 3/3. Commit (`net-2: rcon per-IP + bad-password throttle; document connect decompression bound`).

## Task 5: Finalize — verify, docs, hand off

**Files:** `docs/HOSTING.md` (add a "DoS protection / sv_protect" section), possibly `README.md` (one line under the hosting bullet)

- [ ] **Step 1: Loopback/listen regression (must not break normal play).** Listen server: `devmap oasis` (sv_protect at default 1) — the local client connects and plays normally (loopback is exempt; getchallenge/connect not throttled). Dedicated + matched client (the NET-1 two-instance harness, docs/HOSTING.md): a normal client connects and plays with `sv_protect 1` and `sv_protect 3`. Evidence: clean connect, no spurious rate-limit drops in the server log for the legitimate client.
- [ ] **Step 2: Flood is actually throttled.** From a second process / script, send a burst of OOB `getstatus`/`getinfo` packets to the dedicated server faster than 10/sec from one source and confirm the server starts dropping (Com_DPrintf "rate limit ... exceeded" under `developer 1`) instead of replying to every one. A simple way: a small client-side loop issuing `getstatus` (the client has CL code that sends getstatus for the server browser; or a tiny UDP script). Document the method and show the drop evidence. Also confirm with `sv_protect 0` the limiter is fully disabled (all replies sent) — proving the cvar gates it.
- [ ] **Step 3: Docs.** `docs/HOSTING.md`: add a section documenting `sv_protect` (bitflags 1=rate-limit OOB, 2=DRDoS reflection protection; default 1; recommend 3 for public servers), the generous limits (10 req/sec/IP), loopback exemption, and that rcon is now per-IP brute-force-throttled. Sober tone. README hosting bullet: one line that public servers should set `sv_protect 3`.
- [ ] **Step 4: Engine-touch audit + finalize.** `git diff main...HEAD --stat` — expected: `src/server/sv_main.c`, `src/server/sv_client.c`, `src/server/server.h`, `src/server/sv_init.c`, `docs/HOSTING.md`, maybe README. Confirm NO sim/qagame/cgame/bg_*/pmove/renderer touched. Clean from-scratch build + `ctest` 3/3. Hand off for USER acceptance (host a game, confirm normal play unaffected; optionally observe the flood-drop logs). Do NOT merge/tag — after acceptance: merge `--no-ff` to `main`, tag `rm-dos-hardening`.

---

## Self-Review
Spec coverage: leaky-bucket OOB rate limiting ✓(T2 core + T3 getstatus/getinfo/getchallenge), DRDoS receipt tracking ✓(T3), rcon per-IP throttle ✓(T4), `sv_protect` cvar gate ✓(T1, gates every call site), server-only/no-sim ✓(audit T5.4, feel harness green every task). Adaptations stated up front (IPv4-only, Com_DPrintf for attack-log) and applied consistently. The one correctness landmine — loopback/local exemption so listen servers and single-machine testing don't break — is called out as CRITICAL and enforced at every rate-limit/DRDoS entry (T2.3, T3, T4) and verified first (T5.1). The connect-decompression item is framed as verify-then-document (research shows it's already bounded) rather than speculative code. Function/struct names (SVC_RateLimit, SVC_RateLimitAddress, SVC_BucketForAddress, SVC_HashForAddress, SV_CheckDRDoS, leakyBucket_t, receipt_t, outboundLeakyBucket, SVP_IOQ3/SVP_OWOLF, sv_protect) are consistent across tasks and match the ET:Legacy reference. Security-sensitive: T2/T3/T4 each warrant the dedicated security review in the execution flow.
