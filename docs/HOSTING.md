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
