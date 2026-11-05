"""Exercise the library's actual voice WebSocket with local TLS and fake data."""
import base64
import hashlib
import json
from pathlib import Path
import socketserver
import ssl
import struct
import subprocess
import sys
import threading

errors = []
completed = []
headers = []


def exact(connection, count):
    data = b''
    while len(data) < count:
        block = connection.recv(count - len(data))
        if not block:
            raise EOFError()
        data += block
    return data


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            self.request.settimeout(4)
            with self.server.tls.wrap_socket(self.request, server_side=True) as connection:
                request = b''
                while not request.endswith(b'\r\n\r\n'):
                    request += exact(connection, 1)
                    assert len(request) < 16384
                lines = request.decode('ascii').split('\r\n')
                assert lines[0] == 'GET /media HTTP/1.1'
                values = dict(line.split(': ', 1) for line in lines[1:] if ': ' in line)
                headers.append({key.lower(): value for key, value in values.items()})
                key = headers[-1]['sec-websocket-key']
                accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
                connection.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + b'\r\n\r\n')
                first, size = exact(connection, 2)
                assert first == 0x81 and size & 128
                size &= 127
                if size == 126:
                    size = struct.unpack('!H', exact(connection, 2))[0]
                assert size < 512
                mask = exact(connection, 4)
                payload = exact(connection, size)
                payload = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
                assert json.loads(payload) == {'fixture': 'media'}
                completed.append(True)
                connection.sendall(b'\x81' + bytes([len(payload)]) + payload)
                # Stay alive until the client consumes the echo and cleans up.
                connection.recv(512)
        except (EOFError, ConnectionError, ssl.SSLError):
            pass
        except Exception as error:
            errors.append(str(error))


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True


fixtures = Path(__file__).parent / 'fixtures'
server = Server(('127.0.0.1', 0), Handler)
server.tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
server.tls.load_cert_chain(fixtures / 'localhost.pem', fixtures / 'localhost-key.pem')
server.tls.set_alpn_protocols(['http/1.1'])
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    port = server.server_address[1]
    for host, bundle, result in (
        ('localhost', 'localhost.pem', 'accept'),
        ('localhost', 'untrusted.pem', 'reject'),
        ('127.0.0.1', 'localhost.pem', 'reject'),
    ):
        subprocess.run([sys.argv[1], f'wss://{host}:{port}/media', str(fixtures / bundle), result], check=True, timeout=7)
    assert len(completed) == 1 and len(headers) == 1 and not errors, errors
    if len(sys.argv) > 2 and sys.argv[2].upper() in ('ON', 'TRUE', '1'):
        assert 'Chrome/152.' in headers[0].get('user-agent', ''), headers
    assert not any('authorization' in header for header in headers)
    print('Native voice WSS echo, custom CA, bad trust and hostname rejection passed')
finally:
    server.shutdown()
    server.server_close()
