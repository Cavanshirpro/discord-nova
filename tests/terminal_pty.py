"""Run the original demo in a real POSIX PTY, with no account or network."""
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import sys
import termios
import time

if len(sys.argv) > 1 and sys.argv[1] == '--supervise':
    # Retain the controlling session like a shell after its foreground app exits.
    # This separates application restoration from BSD session-leader hangup.
    status, control = int(sys.argv[3]), int(sys.argv[4])
    os.write(status, b'R')
    if os.read(control, 1) != b'S':
        sys.exit(2)
    result = subprocess.call([sys.argv[2], '--demo'])
    os.write(status, bytes([result if 0 <= result <= 255 else 1]))
    os.read(control, 1)
    sys.exit(result)

master, slave = pty.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 42, 150, 0, 0))
status_read, status_write = os.pipe()
control_read, control_write = os.pipe()


def own_terminal():
    os.setsid()
    fcntl.ioctl(0, termios.TIOCSCTTY, 0)
    os.tcsetpgrp(0, os.getpgrp())


process = subprocess.Popen(
    [sys.executable, str(Path(__file__).resolve()), '--supervise', sys.argv[1], str(status_write), str(control_read)],
    stdin=slave, stdout=slave, stderr=slave, pass_fds=(status_write, control_read), preexec_fn=own_terminal)
os.close(status_write)
os.close(control_read)
output = bytearray()


def collect(until):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if select.select([master], [], [], 0.1)[0]:
            output.extend(os.read(master, 65536))
        if until in output:
            return
        if process.poll() is not None:
            raise AssertionError(f'Demo exited early: {process.returncode}')
    raise AssertionError(f'Terminal output missing {until!r}')


try:
    assert select.select([status_read], [], [], 10)[0], 'PTY supervisor did not start'
    assert os.read(status_read, 1) == b'R'
    before = termios.tcgetattr(slave)
    os.write(control_write, b'S')
    collect(b'\x1b[?2004h')
    os.write(master, b'\r')
    payload = 'ə🌟\nsecond line'.encode()
    os.write(master, b'\x1b[200~' + payload + b'\x1b[201~')
    collect(b'second line')
    os.write(master, b'\x11')
    deadline = time.monotonic() + 8
    result = None
    while time.monotonic() < deadline:
        ready = select.select([master, status_read], [], [], 0.1)[0]
        if master in ready:
            output.extend(os.read(master, 65536))
        if status_read in ready:
            result = os.read(status_read, 1)
            break
    assert result == b'\0', output.decode('utf-8', errors='replace')[-1500:]
    while select.select([master], [], [], 0.05)[0]:
        output.extend(os.read(master, 65536))
    after = termios.tcgetattr(slave)
    assert after == before, f'Terminal settings were not restored: {before!r} -> {after!r}'
    assert b'\x1b[?2004l' in output, 'Bracketed paste mode was not restored'
    os.write(control_write, b'Q')
    assert process.wait(timeout=3) == 0
    print('POSIX demo, UTF-8 paste, explicit Ctrl+Q and terminal restoration passed')
finally:
    if process.poll() is None:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
    for descriptor in (master, slave, status_read, control_write):
        os.close(descriptor)
