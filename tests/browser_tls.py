"""Capture local TLS ClientHello + H2 settings; reject bad trust/hostnames. No Internet."""
import json
from pathlib import Path
import socket
import socketserver
import ssl
import struct
import subprocess
import sys
import threading
import time

errors = []
hellos = []
settings = []
windows = []
requests = 0


def exact(sock, count):
    data = b""
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise EOFError()
        data += chunk
    return data


def grease(value):
    return value & 0x0F0F == 0x0A0A and value >> 8 == value & 255


def client_hello(sock):
    # MSG_PEEK leaves the record available to Python/OpenSSL's real TLS handshake.
    head = sock.recv(5, socket.MSG_PEEK)
    deadline = time.monotonic() + 4
    while len(head) < 5 and time.monotonic() < deadline:
        time.sleep(0.001)
        head = sock.recv(5, socket.MSG_PEEK)
    if len(head) != 5 or head[0] != 22:
        raise AssertionError("Expected a TLS handshake record")
    length = int.from_bytes(head[3:5], "big") + 5
    record = sock.recv(length, socket.MSG_PEEK)
    while len(record) < length and time.monotonic() < deadline:
        time.sleep(0.001)
        record = sock.recv(length, socket.MSG_PEEK)
    assert len(record) == length, "Incomplete ClientHello"
    body = record[5:]
    assert body[0] == 1 and int.from_bytes(body[1:4], "big") <= len(body) - 4
    pos = 4 + 2 + 32
    pos += 1 + body[pos]  # legacy session ID
    count = int.from_bytes(body[pos:pos + 2], "big")
    pos += 2
    ciphers = list(struct.unpack("!" + "H" * (count // 2), body[pos:pos + count]))
    pos += count
    pos += 1 + body[pos]  # compression methods
    end = pos + 2 + int.from_bytes(body[pos:pos + 2], "big")
    pos += 2
    extensions = {}
    while pos < end:
        kind, count = struct.unpack("!HH", body[pos:pos + 4])
        pos += 4
        extensions[kind] = body[pos:pos + count]
        pos += count
    assert any(grease(c) for c in ciphers), "Chrome GREASE cipher missing"
    assert any(grease(e) for e in extensions), "Chrome GREASE extension missing"
    assert [c for c in ciphers if not grease(c)] == [0x1301, 0x1302, 0x1303, 0xC02B, 0xC02F, 0xC02C, 0xC030, 0xCCA9, 0xCCA8, 0xC013, 0xC014, 0x009C, 0x009D, 0x002F, 0x0035], "Chrome cipher order changed"
    assert 27 in extensions, "Browser certificate compression missing"
    assert b"h2" in extensions[16], "REST must offer HTTP/2 via ALPN"
    hellos.append(extensions)


def frame(kind, flags=0, stream=0, payload=b""):
    return len(payload).to_bytes(3, "big") + bytes([kind, flags]) + struct.pack("!I", stream) + payload


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        global requests
        raw = self.request
        raw.settimeout(4)
        try:
            client_hello(raw)
            connection = self.server.tls.wrap_socket(raw, server_side=True)
            with connection:
                assert connection.selected_alpn_protocol() == "h2"
                assert exact(connection, 24) == b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
                connection.sendall(frame(4))  # empty server SETTINGS
                while True:
                    header = exact(connection, 9)
                    size = int.from_bytes(header[:3], "big")
                    assert size < 1024 * 1024
                    kind, flags = header[3:5]
                    stream = int.from_bytes(header[5:9], "big") & 0x7FFFFFFF
                    payload = exact(connection, size)
                    if kind == 4 and not flags & 1:
                        values = [struct.unpack("!HI", payload[i:i + 6]) for i in range(0, size, 6)]
                        settings.append(values)
                        connection.sendall(frame(4, flags=1))
                    elif kind == 8 and stream == 0:
                        windows.append(int.from_bytes(payload, "big"))
                    elif kind == 1:
                        assert flags & 4, "Fixture expects a single HEADERS frame"
                        requests += 1
                        data = json.dumps({"id": "100", "username": "fixture"}).encode()
                        # HPACK static index 8 (:status 200), literal indexed name 31 (content-type).
                        connection.sendall(frame(1, flags=4, stream=stream, payload=b"\x88\x5f\x10application/json") + frame(0, flags=1, stream=stream, payload=data))
        except (EOFError, ConnectionError, ssl.SSLError):
            pass  # Negative cases deliberately abort the untrusted handshake.
        except Exception as exc:
            errors.append(str(exc))


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True


root = Path(__file__).resolve().parent / "fixtures"
server = Server(("127.0.0.1", 0), Handler)
server.tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
server.tls.load_cert_chain(root / "localhost.pem", root / "localhost-key.pem")
server.tls.set_alpn_protocols(["h2"])
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    port = server.server_address[1]
    subprocess.run([sys.argv[1], f"https://localhost:{port}/api", str(root / "localhost.pem"), "accept"], check=True, timeout=15)
    subprocess.run([sys.argv[1], f"https://localhost:{port}/api", str(root / "untrusted.pem"), "reject"], check=True, timeout=10)
    subprocess.run([sys.argv[1], f"https://127.0.0.1:{port}/api", str(root / "localhost.pem"), "reject"], check=True, timeout=10)
    assert requests == 2, "Rejected TLS connections must not send authenticated HTTP requests"
    assert len(hellos) >= 3 and not errors, errors
    assert settings == [[(1, 65536), (2, 0), (4, 6291456), (6, 262144)]], settings
    assert 15663105 in windows, windows
    print("Chrome 152 ClientHello, HTTP/2 SETTINGS/flow control, session reuse and TLS rejection passed")
finally:
    server.shutdown()
