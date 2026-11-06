"""Exercise NOVA's actual SDK-backed jobs against a local HTTP server only."""
import http.server
import gzip
import json
from pathlib import Path
import subprocess
import ssl
import sys
import threading
import urllib.parse

root = Path(__file__).resolve().parents[1]
binary = sys.argv[1]
browser = len(sys.argv) == 5
operations = []
errors = []
def message(identifier='900', content='hello', author='100'):
    return {'id': identifier, 'channel_id': '200', 'author': {'id': author, 'username': 'fixture'},
            'content': content, 'timestamp': '2026-10-29T12:30:00Z', 'attachments': []}
class Http(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def handle_api(self):
        try:
            if self.headers.get('Upgrade', '').lower() == 'websocket':
                # The immediate-close test may reach an Upgrade but must never
                # need a real Gateway. Reject it locally without authentication.
                self.send_response(503)
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            assert self.headers.get('Authorization') == 'offline-token'
            if browser:
                assert 'Chrome/152.' in self.headers.get('User-Agent', '')
                assert self.headers.get('Accept-Encoding') == 'gzip, deflate, br, zstd'
            route = urllib.parse.urlparse(self.path)
            path = route.path.removeprefix('/api')
            query = urllib.parse.parse_qs(route.query)
            body = json.loads(self.rfile.read(int(self.headers.get('Content-Length', 0))) or b'null')
            operations.append((self.command, path, body))
            status = 200
            if path == '/users/@me': data = {'id': '100', 'username': 'fixture'}
            elif path == '/users/@me/guilds': data = [{'id': '400', 'name': 'Fixture'}]
            elif path == '/users/@me/relationships': data = [{'id': '101', 'type': 1, 'user': {'id': '101', 'username': 'friend'}}]
            elif path == '/users/@me/channels':
                if self.command == 'POST': assert body['recipients'] == ['101']
                channel = {'id': '300', 'type': 1, 'recipients': [{'id': '101', 'username': 'friend'}]}
                data = channel if self.command == 'POST' else [channel]
            elif path == '/guilds/400/channels': data = [{'id': '200', 'type': 0, 'name': 'general'}]
            elif path == '/channels/200/messages':
                if self.command == 'POST':
                    assert body['enforce_nonce'] is True and 1 <= len(body['nonce']) <= 25
                    assert body['allowed_mentions']['parse'] == []
                    if body['content'] == 'fixture reply': assert body['message_reference']['message_id'] == '900'
                    data = message('902', body['content'])
                    if body['content'] == 'fixture restricted':
                        status, data = 429, {'code': 40004, 'message': 'Send messages temporarily disabled', 'retry_after': 0}
                elif 'around' in query:
                    identifier = query['around'][0]
                    data = [message(identifier, author='101' if identifier == '901' else '100')]
                else: data = [message(), message('901', author='101')]
            elif path == '/channels/200/messages/900':
                data = message(content=body['content']) if self.command == 'PATCH' else None
            elif path.startswith('/channels/200/messages/900/reactions/'): data = None
            elif path == '/users/101/profile': data = {'user': {'id': '101', 'username': 'friend'}, 'user_profile': {'bio': 'fixture'}}
            else: raise AssertionError(f'Unexpected request: {self.command} {path}')
            payload = json.dumps(data).encode() if data is not None else b''
            if payload:
                payload = gzip.compress(payload)
            self.send_response(status if data is not None else 204)
            self.send_header('Content-Type', 'application/json')
            if payload:
                self.send_header('Content-Encoding', 'gzip')
            self.send_header('Content-Length', str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        except Exception as e:
            errors.append(str(e))
            self.send_response(500); self.send_header('Content-Length', '0'); self.end_headers()
    do_GET = do_POST = do_PATCH = do_PUT = do_DELETE = handle_api

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Http)
if browser:
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(sys.argv[3], sys.argv[4])
    tls.set_alpn_protocols(['http/1.1'])
    server.socket = tls.wrap_socket(server.socket, server_side=True)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    url = f'{"https://localhost" if browser else "http://127.0.0.1"}:{server.server_port}/api'
    command = [binary, '--fixture', url]
    if browser:
        command += [sys.argv[2], sys.argv[3]]
    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=45)
    print(result.stdout)
    if result.returncode: print(result.stderr)
    assert result.returncode == 0, 'SDK-backed fixture failed'
    assert not errors, errors
    assert not any(method in ('PATCH', 'DELETE') and path.endswith('/901') for method,path,_ in operations)
    assert sum(method == 'POST' and path == '/channels/200/messages' for method,path,_ in operations) == 3
    assert not any(body and body.get('content') == 'fixture must stay blocked' for _,_,body in operations)
    print(f'{len(operations)} loopback requests checked; no foreign edit/delete and no real Discord requests')
finally:
    server.shutdown(); server.server_close(); thread.join()
