"""Run the original demo in a real POSIX PTY, with no account or network."""
import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import termios
import time

master, slave = pty.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 42, 150, 0, 0))
before = termios.tcgetattr(slave)
process = subprocess.Popen([sys.argv[1], '--demo'], stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
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
    collect(b'\x1b[?2004h')
    os.write(master, b'\r')
    payload = 'ə🌟\nsecond line'.encode()
    os.write(master, b'\x1b[200~' + payload + b'\x1b[201~')
    collect('second line'.encode())
    assert process.poll() is None, 'Pasting text must keep the demo running'
    os.write(master, b'\x11')
    deadline = time.monotonic() + 8
    while process.poll() is None and time.monotonic() < deadline:
        if select.select([master], [], [], 0.1)[0]:
            output.extend(os.read(master, 65536))
    assert process.wait(timeout=2) == 0, output.decode('utf-8', errors='replace')[-1500:]
    while select.select([master], [], [], 0.05)[0]:
        output.extend(os.read(master, 65536))
    assert termios.tcgetattr(slave) == before, 'Terminal settings were not restored'
    assert b'\x1b[?2004l' in output, 'Bracketed paste mode was not restored'
    print('POSIX demo, UTF-8 paste, explicit Ctrl+Q and terminal restoration passed')
finally:
    if process.poll() is None:
        process.kill()
        process.wait()
    os.close(master)
    os.close(slave)
