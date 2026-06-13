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
client modules, so a client with a different `rm_bin.pk3` (a different RM build)
will be rejected. The friendly version-mismatch message and explicit
build-version enforcement are a later increment; today a mismatch surfaces as
the stock pure-server rejection.
