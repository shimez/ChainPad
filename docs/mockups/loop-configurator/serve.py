"""Read-only Loop UI prototype. Run from any directory; no device/API access."""
import argparse
import mimetypes
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def between(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


def shared_assets():
    source = (ROOT / "web/index.html").read_text(encoding="utf-8")
    css = "\n".join(re.findall(r"<style>(.*?)</style>", source, re.S))
    # Reuse the real Action editor (OSC picker, MIDI, HID and OBS) unchanged.
    # Only its outer cards are replaced by the prototype's tree-aware headers.
    js = between(source, "const usageNames=", "const jisUsageNames=")
    js += between(source, "function control(", "function current()")
    js += between(source, "function button(", "function selectInput(")
    js = js.replace("function renderActions(){", "function renderOriginalActions(){")
    return css, js


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        path = urlsplit(self.path).path
        if path in ("/shared.css", "/shared.js"):
            css, js = shared_assets()
            data = (css if path.endswith(".css") else js).encode("utf-8")
            content_type = "text/css" if path.endswith(".css") else "text/javascript"
        else:
            local = {"/": "index.html", "/mock.css": "mock.css", "/mock.js": "mock.js",
                     "/model.js": "model.js"}.get(path)
            shared = {"presets.js", "action-ui.js", "osc-addresses.js", "obs-actions.js", "favicon.svg"}
            if local:
                file = HERE / local
            elif path.startswith("/web/") and path[5:] in shared:
                file = ROOT / "web" / path[5:]
            else:
                self.send_error(404)
                return
            data = file.read_bytes()
            content_type = mimetypes.guess_type(file)[0] or "application/octet-stream"
        self.send_response(200)
        self.send_header("Content-Type", content_type + "; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'none'")
        self.end_headers()
        self.wfile.write(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8778)
    args = parser.parse_args()
    shared_assets()  # Fail early if the production editor's extraction anchors changed.
    print(f"Loop Configurator mock: http://127.0.0.1:{args.port}/", flush=True)
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
