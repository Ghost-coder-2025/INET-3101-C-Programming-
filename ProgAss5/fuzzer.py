#!/usr/bin/env python3
# fuzzer.py - sends malicious TCP payloads to server.c
# usage: python3 fuzzer.py [host] [port]
import os
import socket
import struct
import sys
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9090


def connect():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(3)
    s.connect((HOST, PORT))
    return s


def read_all(s):
    """Read until the server closes. Returns bytes, or None if the server hung."""
    data = b""
    try:
        while True:
            chunk = s.recv(1024)
            if not chunk:
                return data
            data += chunk
    except socket.timeout:
        return None
    except ConnectionResetError:
        return data


def alive():
    """Is the server still answering? (needs test.txt to exist)"""
    try:
        s = connect()
        s.sendall(b"test.txt\n")
        data = read_all(s)
        s.close()
        return bool(data)
    except OSError:
        return False


def send_payload(payload):
    s = connect()
    try:
        s.sendall(payload)
    except OSError:
        pass
    data = read_all(s)
    s.close()
    return data


def judge(data):
    """PASS if the server refused or dropped us. FAIL if it hung or returned file data."""
    if data is None:
        return "FAIL (server hung: connection left open with no reply)"
    if data == b"" or data.startswith(b"ERROR"):
        return "PASS"
    return "FAIL (server returned file data: %r)" % data[:40]


# ---------- teardown tests (abrupt disconnects) ----------
def connect_and_close():
    connect().close()
    return "PASS"


def partial_then_close():
    s = connect()
    s.sendall(b"tes")          # half a filename, no newline
    s.close()
    return "PASS"


def reset_mid_transfer():
    s = connect()
    s.sendall(b"test.txt\n")
    # linger 0 = close with a RST, like a crashed client
    s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    s.close()
    return "PASS"


def idle_connection():
    s = connect()              # connect and say nothing
    time.sleep(7)
    ok = alive()               # can the server still serve someone else?
    s.close()
    return "PASS" if ok else "FAIL (server stuck waiting on an idle client)"


# ---------- all tests ----------
TESTS = [
    # path traversal
    ("traversal: ../../etc/passwd", lambda: judge(send_payload(b"../../etc/passwd\n"))),
    ("traversal: absolute /etc/passwd", lambda: judge(send_payload(b"/etc/passwd\n"))),
    ("traversal: ....// trick", lambda: judge(send_payload(b"....//....//etc/passwd\n"))),
    ("traversal: subdir/../test.txt", lambda: judge(send_payload(b"subdir/../test.txt\n"))),
    ("traversal: bare ..", lambda: judge(send_payload(b"..\n"))),
    # payload flooding
    ("flood: 10,000 chars, no newline", lambda: judge(send_payload(b"A" * 10000))),
    ("flood: 10,000 chars + newline", lambda: judge(send_payload(b"A" * 10000 + b"\n"))),
    ("garbage: 1000 random bytes", lambda: judge(send_payload(os.urandom(1000)))),
    # null byte injection
    ("null: test.txt\\0../../etc/passwd", lambda: judge(send_payload(b"test.txt\x00../../etc/passwd\n"))),
    ("null: test.txt\\0", lambda: judge(send_payload(b"test.txt\x00\n"))),
    ("null: only a null byte", lambda: judge(send_payload(b"\x00\n"))),
    ("null: 300 null bytes", lambda: judge(send_payload(b"\x00" * 300))),
    # abrupt disconnects
    ("teardown: connect then close", connect_and_close),
    ("teardown: half a filename then close", partial_then_close),
    ("teardown: reset mid-transfer", reset_mid_transfer),
    ("teardown: idle client (takes ~7s)", idle_connection),
]


def main():
    if not alive():
        print("Server not answering on %s:%d. Start it and make sure test.txt exists." % (HOST, PORT))
        sys.exit(2)

    failures = 0
    for name, test in TESTS:
        try:
            verdict = test()
        except Exception as e:
            verdict = "FAIL (fuzzer error: %s)" % e
        if not alive():
            verdict = "FAIL (server dead or stuck afterwards)"
        if verdict.startswith("FAIL"):
            failures += 1
        print("[%s] %s" % (verdict, name))

    print("\n%d of %d tests failed" % (failures, len(TESTS)))
    sys.exit(1 if failures else 0)


main()