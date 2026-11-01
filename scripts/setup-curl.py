"""Build a pinned WebSocket-enabled curl on Linux (distribution curl may lack ws/wss)."""
import hashlib
from pathlib import Path
import subprocess
import tarfile
import urllib.request

root = Path(__file__).resolve().parents[1]
tools = root / "build/tools"
tools.mkdir(parents=True, exist_ok=True)
archive = tools / "curl-8.17.0.tar.xz"
digest = "955f6e729ad6b3566260e8fef68620e76ba3c31acf0a18524416a185acf77992"
if not archive.exists():
    with urllib.request.urlopen("https://curl.se/download/curl-8.17.0.tar.xz", timeout=60) as response:
        archive.write_bytes(response.read())
if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
    raise SystemExit("curl archive SHA-256 mismatch")
source = tools / "curl-8.17.0"
if not source.exists():
    with tarfile.open(archive) as tar:
        tar.extractall(tools, filter="data")
prefix = tools / "curl"
build = tools / "curl-build"
subprocess.run([
    "cmake", "-S", str(source), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
    f"-DCMAKE_INSTALL_PREFIX={prefix}", "-DCMAKE_INSTALL_LIBDIR=lib", "-DBUILD_SHARED_LIBS=ON",
    "-DBUILD_CURL_EXE=OFF", "-DBUILD_TESTING=OFF", "-DCURL_USE_OPENSSL=ON",
    "-DCURL_DISABLE_WEBSOCKETS=OFF", "-DCURL_USE_LIBPSL=OFF", "-DCURL_BROTLI=OFF", "-DCURL_ZSTD=OFF",
    "-DUSE_NGHTTP2=OFF", "-DCURL_USE_LIBSSH2=OFF", "-DCURL_USE_GSSAPI=OFF",
], check=True)
subprocess.run(["cmake", "--build", str(build), "--parallel", "2"], check=True)
subprocess.run(["cmake", "--install", str(build)], check=True)
print(f"Use -DCMAKE_PREFIX_PATH={prefix}")
