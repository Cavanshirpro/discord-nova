"""Local HTTP and RFC6455 fixtures. No real account, token or Internet access."""
import base64
import hashlib
import http.server
import json
import queue
import socketserver
import struct
import subprocess
import sys
import threading
import time

events = queue.Queue()
errors = []
history_count = 0
gateway_ready = threading.Event()
events_sent = threading.Event()


def message(identifier, content):
    return {"id": identifier, "channel_id": "200", "author": {"id": "100", "username": "fixture"}, "content": content}


class HTTP(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        global history_count
        try:
            assert self.headers.get("Authorization") == "offline-token"
            status = 200
            if self.path == "/api/users/@me":
                data = {"id": "100", "username": "fixture", "global_name": "Fixture"}
            elif self.path == "/api/users/@me/guilds?with_counts=true":
                data = [{"id": "400", "name": "Offline server"}]
            elif self.path == "/api/guilds/400/channels":
                data = [{"id": "200", "name": "chat", "type": 0}, {"id": "201", "name": "voice", "type": 2}, {"id": "202", "name": "category", "type": 4}]
            elif self.path == "/api/users/@me/channels":
                data = [{"id": "300", "type": 1, "recipients": [{"id": "101", "username": "friend", "global_name": "Friend"}]}]
            elif self.path == "/api/channels/200/messages?limit=100":
                history_count += 1
                if history_count == 1:
                    assert gateway_ready.wait(3)
                    events.put(("MESSAGE_UPDATE", {"id": "901", "channel_id": "200", "content": "edited live"}))
                    events.put(("MESSAGE_DELETE", {"id": "902", "channel_id": "200"}))
                    events.put(("MESSAGE_CREATE", message("903", "created live")))
                    assert events_sent.wait(3)
                    time.sleep(0.15)  # Force event application to overlap with REST history.
                    data = [message("902", "deleted later"), message("901", "stale history")]
                else:
                    data = [message("901", "fresh REST")]
            elif self.path == "/api/channels/200/messages" and self.command == "POST":
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                assert body["allowed_mentions"]["parse"] == []
                if body["content"] == "fail-send":
                    status, data = 403, {"message": "Missing Access", "code": 50001}
                else:
                    data = message("904", body["content"])
            else:
                raise AssertionError(f"Unexpected route: {self.command} {self.path}")
            payload = json.dumps(data).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        except Exception as exc:
            errors.append(str(exc))
            self.send_error(500)

    do_POST = do_GET


def exact(sock, count):
    data = b""
    while len(data) < count:
        part = sock.recv(count - len(data))
        if not part:
            raise EOFError()
        data += part
    return data


def receive(sock):
    first, second = exact(sock, 2)
    size = second & 127
    if size == 126:
        size = struct.unpack("!H", exact(sock, 2))[0]
    elif size == 127:
        size = struct.unpack("!Q", exact(sock, 8))[0]
    assert size < 1024 * 1024
    mask = exact(sock, 4) if second & 128 else None
    data = exact(sock, size)
    if mask:
        data = bytes(c ^ mask[i % 4] for i, c in enumerate(data))
    return first & 15, data


class Gateway(socketserver.BaseRequestHandler):
    def handle(self):
        sock = self.request
        sock.settimeout(4)
        seq = 0

        def packet(op, data, name=None):
            nonlocal seq
            value = {"op": op, "d": data}
            if name:
                seq += 1
                value.update(t=name, s=seq)
            payload = json.dumps(value).encode()
            header = bytes([0x81, len(payload)]) if len(payload) < 126 else b"\x81\x7e" + struct.pack("!H", len(payload))
            sock.sendall(header + payload)

        try:
            headers = b""
            while b"\r\n\r\n" not in headers:
                headers += exact(sock, 1)
            key = next(line.split(b":", 1)[1].strip() for line in headers.split(b"\r\n") if line.lower().startswith(b"sec-websocket-key:"))
            accept = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
            sock.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
            packet(10, {"heartbeat_interval": 50})
            while True:
                op, raw = receive(sock)
                if op == 8:
                    return
                if op != 1:
                    continue
                value = json.loads(raw)
                if value["op"] == 2:
                    assert value["d"]["token"] == "offline-token"
                    packet(0, {"user": {"id": "100", "username": "fixture"}, "guilds": [], "session_id": "fixture", "resume_gateway_url": self.server.url}, "READY")
                    gateway_ready.set()
                elif value["op"] == 1:
                    packet(11, None)
                    while not events.empty():
                        name, payload = events.get_nowait()
                        packet(0, payload, name)
                        if name == "MESSAGE_CREATE":
                            events_sent.set()
        except (EOFError, ConnectionError, TimeoutError):
            pass
        except Exception as exc:
            errors.append(str(exc))


class WebSocketServer(socketserver.ThreadingTCPServer):
    daemon_threads = True


if __name__ == "__main__":
    http = http.server.ThreadingHTTPServer(("127.0.0.1", 0), HTTP)
    ws = WebSocketServer(("127.0.0.1", 0), Gateway)
    ws.url = f"ws://127.0.0.1:{ws.server_address[1]}/"
    for server in (http, ws):
        threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        result = subprocess.run([sys.argv[1], f"http://127.0.0.1:{http.server_port}/api", ws.url], timeout=35)
        if errors:
            print("Fixture errors:", errors, file=sys.stderr)
        sys.exit(result.returncode or bool(errors))
    finally:
        http.shutdown()
        ws.shutdown()
