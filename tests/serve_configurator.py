"""Local UI fixture, NOT a firmware/API implementation. Ctrl+C to stop."""
import json
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit, parse_qs

ROOT = Path(__file__).resolve().parents[1]
CONFIG = {"schemaVersion": 1,
          "network": {"ssid": "", "password": "", "oscHost": "192.168.1.100", "oscPort": 9000},
          "chains": [{"input": i, "actions": []} for i in range(28)]}
BOARD = "s3"
PENDING = None
TOKEN = 0


class Handler(BaseHTTPRequestHandler):
    def reply(self, data):
        payload = json.dumps(data).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if self.path == "/favicon.svg":
            payload = (ROOT / "web/favicon.svg").read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "image/svg+xml")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        elif self.path == "/api/config":
            self.reply(CONFIG)
        elif self.path in ("/presets.js", "/action-ui.js"):
            payload = (ROOT / "web" / self.path.lstrip("/")).read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/javascript; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        elif self.path == "/api/wifi":
            self.reply({key: CONFIG["network"][key] for key in ("ssid", "password")})
        elif self.path == "/api/capabilities":
            usb = BOARD == "s3"
            self.reply({"hardware": f"XIAO ESP32{BOARD.upper()}", "usbMidi": usb, "usbKeyboard": usb,
                        "defaultMidiTransport": "both" if usb else "ble",
                        "midiTransports": ["both", "usb", "ble"] if usb else ["ble"],
                        "keyboardTransports": ["usb", "ble"] if usb else ["ble"]})
        elif self.path == "/api/status":
            self.reply({"hardware": f"XIAO ESP32{BOARD.upper()}",
                        "usbMidiSupported": BOARD == "s3", "usbKeyboardSupported": BOARD == "s3",
                        "wifi": True, "usbMidi": BOARD == "s3", "usbKeyboard": BOARD == "s3", "bleMidi": True,
                        "bleKeyboard": True, "ip": "127.0.0.1", "apIp": "192.168.4.1",
                        "events": 0, "accepted": 0, "skipped": 0, "lastInput": "key1.press",
                        "lastAction": 1, "lastResult": "accepted", "inputOverflows": 0,
                        "transportOverflows": 0, "transportRetries": 0,
                        "bootMessage": "UI TEST FIXTURE — no hardware connected", "inputsReady": True})
        elif self.path.split("?", 1)[0] in ("/", "/configurator"):
            page = "wifi.html" if self.path == "/?setup=wifi" else "index.html"
            payload = (ROOT / "web" / page).read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        else:
            self.send_error(404)

    def do_PUT(self):
        global CONFIG
        data = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        if self.path == "/api/wifi":
            for key in ("ssid", "password"):
                CONFIG["network"][key] = data[key]
        elif urlsplit(self.path).path == "/api/config/chain":
            query = parse_qs(urlsplit(self.path).query)
            if not PENDING or int(query["token"][0]) != TOKEN or int(query["input"][0]) != len(PENDING["chains"]):
                self.send_error(409)
                return
            PENDING["chains"].append(data)
        else:
            self.send_error(404)
            return
        self.reply({"ok": True, "message": "Saved (test fixture)"})

    def do_POST(self):
        global CONFIG, PENDING, TOKEN
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        path = urlsplit(self.path).path
        if path == "/api/config/begin":
            TOKEN += 1
            PENDING = {"schemaVersion": 1, "network": json.loads(body), "chains": []}
            self.reply({"token": TOKEN})
            return
        if path == "/api/config/commit":
            query = parse_qs(urlsplit(self.path).query)
            if not PENDING or int(query["token"][0]) != TOKEN or len(PENDING["chains"]) != 28:
                self.send_error(409)
                return
            CONFIG = PENDING
            PENDING = None
        self.reply({"ok": True, "message": "Dispatched (test fixture)"})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--board", choices=["s3", "c3", "c6", "c5"], default="s3")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    BOARD = args.board
    print(f"Configurator {BOARD} test fixture: http://127.0.0.1:{args.port}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
