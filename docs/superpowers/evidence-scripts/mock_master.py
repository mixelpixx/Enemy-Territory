#!/usr/bin/env python3
r"""mock_master.py -- a tiny q3/ET master-server stand-in for ET-RM NET-3.

This is a TEST HARNESS, not shipped engine code. It lets us prove the full
"Internet" server-browse flow end-to-end on one machine with no third-party
software and no dependency on a real VPS master:

    server (dedicated 2) --heartbeat--> mock_master <--getservers-- client
                                   \--getserversResponse-->/

The q3/ET master protocol is connectionless UDP on PORT_MASTER (27950). Every
message is "out of band": prefixed with four 0xFF bytes. We handle the two
message types the RM engine actually emits:

  * Server  -> master:  "\xff\xff\xff\xffheartbeat EnemyTerritory-1\n"
      RM's SV_MasterHeartbeat (src/server/sv_main.c) sends this when
      `dedicated 2`. We record the sender's IP:port as a known server so the
      next getservers reply lists it dynamically. (A real master, e.g.
      dpmaster, would first bounce a getinfo challenge back to confirm the
      server is alive; we optionally do that too with --confirm for fidelity,
      but recording on heartbeat is sufficient for the proof.)

  * Client  -> master:  "\xff\xff\xff\xffgetservers 84 [keywords]"
      RM's CL_GlobalServers_f (src/client/cl_main.c:4155) sends this. We reply
      with a getserversResponse framed EXACTLY as CL_ServersResponsePacket
      (src/client/cl_main.c:2229) parses it -- see _build_getservers_response
      below for the byte-for-byte justification.

Usage:
    python mock_master.py [--bind 127.0.0.1] [--port 27950]
                          [--server IP:PORT ...] [--confirm] [--protocol 84]

  --server  pre-register a server (repeatable). Heartbeats add to this set
            dynamically, so you usually don't need it.
  --confirm send a getinfo challenge back to a server on heartbeat (dpmaster
            fidelity). Off by default; recording-on-heartbeat is enough.

Every packet in and every reply out is logged with a timestamp to stdout, so
the log is the evidence that the heartbeat arrived and the getservers was
answered.
"""

import argparse
import socket
import struct
import sys
import time

OOB = b"\xff\xff\xff\xff"  # connectionless / out-of-band prefix


def ts() -> str:
    return time.strftime("%H:%M:%S", time.localtime())


def log(msg: str) -> None:
    print(f"[{ts()}] {msg}", flush=True)


def parse_server_arg(s: str):
    host, _, port = s.partition(":")
    return (host, int(port) if port else 27960)


def _build_getservers_response(servers):
    r"""Frame a getserversResponse exactly as CL_ServersResponsePacket parses it.

    The RM parser (src/client/cl_main.c:2229) walks the *whole* OOB packet
    (data still includes the 0xFF prefix and the "getserversResponse" token).
    For each server it:
        1. scans forward until it consumes a backslash  ('\\')
        2. reads 4 IP bytes, then a big-endian uint16 port (hi<<8 | lo)
        3. syntax-checks that the *next* byte is another '\\'
        4. breaks if that next run is the literal "EOT"  (buffptr[1..3])

    So the on-the-wire form is:

        getserversResponse \<ip0..3><porthi><portlo> \<...> \EOT

    i.e. one leading backslash per server record, and a final
    "\\EOT" terminator. The backslash that precedes "EOT" doubles as the
    syntax-check separator after the last server, which is why a real master
    emits "...<portlo>\\EOT" (no extra separator). We match that byte-for-byte.
    """
    out = bytearray(OOB)
    out += b"getserversResponse"
    for ip, port in servers:
        octets = bytes(int(x) for x in ip.split("."))
        if len(octets) != 4:
            raise ValueError(f"bad IPv4 {ip!r}")
        out += b"\\"
        out += octets
        out += struct.pack(">H", port)  # big-endian port, hi then lo
    out += b"\\EOT"
    return bytes(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description="ET-RM mock master server")
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=27950)
    ap.add_argument("--protocol", default="84")
    ap.add_argument("--server", action="append", default=[],
                    help="pre-register IP:PORT (repeatable)")
    ap.add_argument("--confirm", action="store_true",
                    help="send getinfo challenge back to a server on heartbeat")
    args = ap.parse_args(argv)

    # set of known servers, keyed by (ip, port)
    known = {}
    for s in args.server:
        host, port = parse_server_arg(s)
        known[(host, port)] = time.time()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, args.port))
    log(f"mock master listening on {args.bind}:{args.port} "
        f"(protocol {args.protocol}); pre-registered={list(known)}")

    while True:
        data, addr = sock.recvfrom(4096)
        if not data.startswith(OOB):
            log(f"<- {addr} non-OOB packet ({len(data)} bytes), ignoring")
            continue
        payload = data[4:]
        # log the command token for the evidence trail
        text = payload.split(b"\n", 1)[0].split(b" ", 1)[0]
        log(f"<- {addr[0]}:{addr[1]}  {len(data)}B  cmd={text!r}  "
            f"raw={payload[:48]!r}")

        if payload.startswith(b"heartbeat"):
            known[addr] = time.time()
            log(f"   registered server {addr[0]}:{addr[1]} "
                f"(heartbeat); known now={list(known)}")
            if args.confirm:
                # dpmaster-style liveness confirm
                sock.sendto(OOB + b"getinfo RM-MOCK-CHALLENGE", addr)
                log(f"-> {addr[0]}:{addr[1]}  getinfo challenge (confirm)")
            continue

        if payload.startswith(b"infoResponse"):
            # reply to our --confirm challenge; keep it registered
            log(f"   infoResponse from {addr[0]}:{addr[1]} (confirmed alive)")
            continue

        if payload.startswith(b"getservers"):
            # tolerate "getservers 84", "getservers 84 empty full", etc.
            servers = [(ip, port) for (ip, port) in known]
            reply = _build_getservers_response(servers)
            sock.sendto(reply, addr)
            log(f"-> {addr[0]}:{addr[1]}  getserversResponse with "
                f"{len(servers)} server(s) {servers}  ({len(reply)}B): "
                f"{reply!r}")
            continue

        log(f"   unhandled command {text!r}, ignoring")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        log("shutting down")
        sys.exit(0)
