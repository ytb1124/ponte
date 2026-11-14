#!/usr/bin/env python3
"""Minimal software PTPv2 grandmaster for bench tests without AES67 hardware.

Two-step, multicast E2E, domain 0 by default. Announces the worst possible
clock quality (priority1 250, class 248), so any real grandmaster on the
network wins the BMCA. Timestamps are software (time.time_ns) - good enough to
let aes67eleven lock for loopback tests, NOT a production clock.

usage: python3 tools/ptp_test_gm.py <interface-ip> [--domain N] [--seconds S]
"""
import argparse
import os
import socket
import struct
import threading
import time

PTP_GROUP = "224.0.1.129"
SYNC, DELAY_REQ, FOLLOW_UP, DELAY_RESP, ANNOUNCE = 0x0, 0x1, 0x8, 0x9, 0xB


def ts_bytes(ns):
    sec, nsec = divmod(ns, 1_000_000_000)
    return struct.pack(">HII", (sec >> 32) & 0xFFFF, sec & 0xFFFFFFFF, nsec)


def header(msg_type, length, domain, flags, identity, seq, control, log_interval):
    return struct.pack(">BBHBBHq4s8sHHBb",
                       msg_type, 2, length, domain, 0, flags, 0, b"\0" * 4,
                       identity, 1, seq, control, log_interval)


def open_socket(port, if_ip):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    s.bind(("", port))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                 socket.inet_aton(PTP_GROUP) + socket.inet_aton(if_ip))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(if_ip))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)  # same-host slaves
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("interface_ip")
    ap.add_argument("--domain", type=int, default=0)
    ap.add_argument("--seconds", type=float, default=0, help="stop after S seconds (0 = run until Ctrl-C)")
    args = ap.parse_args()

    # locally administered, random - must differ from the slave's own identity
    identity = bytes([0x02]) + os.urandom(2) + b"\xff\xfe" + os.urandom(3)
    ev = open_socket(319, args.interface_ip)
    gen = open_socket(320, args.interface_ip)
    dest_ev, dest_gen = (PTP_GROUP, 319), (PTP_GROUP, 320)
    stop = threading.Event()
    print("test grandmaster", "-".join(f"{b:02X}" for b in identity), "on", args.interface_ip,
          "domain", args.domain, flush=True)

    def delay_responder():
        ev.settimeout(0.5)
        while not stop.is_set():
            try:
                data, _ = ev.recvfrom(1500)
            except socket.timeout:
                continue
            t4 = time.time_ns()
            if len(data) < 44 or data[0] & 0x0F != DELAY_REQ or data[4] != args.domain:
                continue
            seq = struct.unpack(">H", data[30:32])[0]
            requester = data[20:30]  # clock identity + port
            msg = header(DELAY_RESP, 54, args.domain, 0, identity, seq, 3, 0) + ts_bytes(t4) + requester
            gen.sendto(msg, dest_gen)

    threading.Thread(target=delay_responder, daemon=True).start()

    seq_sync = seq_ann = 0
    next_ann = 0.0
    end = time.monotonic() + args.seconds if args.seconds else None
    try:
        while end is None or time.monotonic() < end:
            now = time.monotonic()
            if now >= next_ann:
                body = (ts_bytes(0) + struct.pack(">hB", 37, 0) +
                        struct.pack(">BBBHB", 250, 248, 0xFE, 0xFFFF, 250) +
                        identity + struct.pack(">HB", 0, 0xA0))  # steps removed, internal oscillator
                gen.sendto(header(ANNOUNCE, 64, args.domain, 0x0008, identity, seq_ann, 5, 0) + body, dest_gen)
                seq_ann = (seq_ann + 1) & 0xFFFF
                next_ann = now + 1.0
            # two-step sync, 8/s: precise origin goes into the follow up
            ev.sendto(header(SYNC, 44, args.domain, 0x0200, identity, seq_sync, 0, -3) + ts_bytes(0), dest_ev)
            t1 = time.time_ns()
            gen.sendto(header(FOLLOW_UP, 44, args.domain, 0, identity, seq_sync, 2, -3) + ts_bytes(t1), dest_gen)
            seq_sync = (seq_sync + 1) & 0xFFFF
            time.sleep(0.125)
    except KeyboardInterrupt:
        pass
    stop.set()
    print("grandmaster stopped", flush=True)


if __name__ == "__main__":
    main()
