#!/usr/bin/env python3
"""Serial monitor and local page for the iceberg gateway.

The gateway prints one JSON object per line. This serves a page on
127.0.0.1 and streams those lines to the browser. Nothing leaves the machine.
"""

import argparse
import glob
import json
import os
import queue
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "index.html")

clients = []
clients_lock = threading.Lock()
recent = []
recent_lock = threading.Lock()
RECENT_MAX = 400


def publish(line):
    with recent_lock:
        recent.append(line)
        if len(recent) > RECENT_MAX:
            del recent[: len(recent) - RECENT_MAX]
    with clients_lock:
        dead = []
        for q in clients:
            try:
                q.put_nowait(line)
            except queue.Full:
                dead.append(q)
        for q in dead:
            if q in clients:
                clients.remove(q)


def serial_loop(port, baud):
    ser = serial.Serial(port, baud, timeout=1)
    print("reading %s at %d" % (port, baud))
    buf = b""
    try:
        while True:
            chunk = ser.read(256)
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.decode("utf-8", "replace").strip()
                if not text.startswith("{"):
                    if text:
                        print(text)
                    continue
                try:
                    json.loads(text)
                except json.JSONDecodeError:
                    print(text)
                    continue
                print(text)
                publish(text)
    finally:
        ser.close()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        return

    def _send(self, code, body, content_type):
        data = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            with open(PAGE, "rb") as fh:
                self._send(200, fh.read(), "text/html; charset=utf-8")
            return
        if path == "/api/recent":
            with recent_lock:
                body = '{"lines":[' + ",".join(recent) + "]}"
            self._send(200, body, "application/json")
            return
        if path == "/events":
            q = queue.Queue(maxsize=50)
            with clients_lock:
                clients.append(q)
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            try:
                while True:
                    try:
                        line = q.get(timeout=15)
                    except queue.Empty:
                        self.wfile.write(b": ping\n\n")
                        self.wfile.flush()
                        continue
                    self.wfile.write(b"data: " + line.encode("utf-8") + b"\n\n")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
            finally:
                with clients_lock:
                    if q in clients:
                        clients.remove(q)
            return
        self._send(404, "not found", "text/plain")


def default_port():
    found = []
    if list_ports is not None:
        for p in list_ports.comports():
            found.append(p.device)
    if not found:
        found = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    return found


def main():
    parser = argparse.ArgumentParser(description="Iceberg gateway monitor")
    parser.add_argument("--port", help="serial device, for example /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--http", default="127.0.0.1:8765")
    args = parser.parse_args()

    if serial is None:
        raise SystemExit("pyserial is not installed. Run: pip install pyserial")

    port = args.port
    if not port:
        found = default_port()
        if len(found) == 1:
            port = found[0]
        else:
            raise SystemExit(
                "pass --port. serial devices: %s" % (", ".join(found) or "none")
            )

    host, _, http_port = args.http.partition(":")
    if not http_port:
        host, http_port = "127.0.0.1", host
    http_port = int(http_port)

    threading.Thread(target=serial_loop, args=(port, args.baud), daemon=True).start()
    server = ThreadingHTTPServer((host, http_port), Handler)
    print("open http://%s:%d" % (host, http_port))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
