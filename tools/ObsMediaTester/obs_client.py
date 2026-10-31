"""Small OBS WebSocket 5.x client. Socket ownership stays in the worker thread."""
import base64
import hashlib
import json
import queue
import threading
import time

import websocket


def authentication(password, salt, challenge):
    def digest(text):
        return base64.b64encode(hashlib.sha256(text.encode("utf-8")).digest()).decode("ascii")
    return digest(digest(password + salt) + challenge)


class Cancelled(Exception):
    pass


class Protocol:
    def __init__(self, socket, emit, stop):
        self.socket, self.emit, self.stop = socket, emit, stop
        self.sequence = 0

    def receive(self, deadline):
        while time.monotonic() < deadline:
            if self.stop.is_set():
                raise Cancelled()
            try:
                raw = self.socket.recv()
            except websocket.WebSocketTimeoutException:
                continue
            if not raw:
                raise ConnectionError("OBSとの接続が閉じられました")
            packet = json.loads(raw)
            if not isinstance(packet, dict):
                raise ValueError("不正なOBS応答です")
            return packet
        raise TimeoutError("OBS応答タイムアウト。操作結果は未確定です（自動再送なし）")

    def identify(self, password):
        hello = self.receive(time.monotonic() + 5)
        if hello.get("op") != 0:
            raise ValueError("OBS WebSocket 5.xのHelloを受信できませんでした")
        info = hello["d"]
        if info.get("rpcVersion", 0) < 1:
            raise ValueError("RPC version 1に対応していません")
        data = {"rpcVersion": 1, "eventSubscriptions": 0}
        if "authentication" in info:
            auth = info["authentication"]
            data["authentication"] = authentication(password, auth["salt"], auth["challenge"])
        # Password, salt, challenge and authentication tokens never enter the log.
        self.socket.send(json.dumps({"op": 1, "d": data}))
        identified = self.receive(time.monotonic() + 5)
        if identified.get("op") != 2:
            raise ValueError("OBS認証に失敗しました")
        return {key: info.get(key) for key in ("obsStudioVersion", "obsWebSocketVersion", "rpcVersion")}

    def request(self, kind, data):
        self.sequence += 1
        request_id = f"media-test-{self.sequence}"
        packet = {"op": 6, "d": {"requestType": kind, "requestId": request_id, "requestData": data}}
        started = time.monotonic()
        self.emit("tx", packet)
        self.socket.send(json.dumps(packet, ensure_ascii=False))
        deadline = started + 5
        while True:
            response = self.receive(deadline)
            self.emit("rx", response)
            if response.get("op") != 7:
                continue
            body = response["d"]
            if body.get("requestId") != request_id or body.get("requestType") != kind:
                continue
            return body, round((time.monotonic() - started) * 1000, 1)


class Worker(threading.Thread):
    def __init__(self, host, port, password):
        super().__init__(daemon=True)
        self.host, self.port, self.password = host, port, password
        self.commands = queue.Queue(maxsize=1)
        self.events = queue.Queue()
        self.stop_event = threading.Event()

    def emit(self, kind, data):
        self.events.put((kind, data))

    def run(self):
        socket = None
        try:
            host = f"[{self.host}]" if ":" in self.host and not self.host.startswith("[") else self.host
            socket = websocket.create_connection(
                f"ws://{host}:{self.port}", timeout=3, subprotocols=["obswebsocket.json"],
                http_no_proxy=[self.host],
            )
            socket.settimeout(0.2)
            protocol = Protocol(socket, self.emit, self.stop_event)
            info = protocol.identify(self.password)
            self.password = ""
            self.emit("ready", info)
            while not self.stop_event.is_set():
                try:
                    kind, data = self.commands.get(timeout=0.1)
                except queue.Empty:
                    # No subscriptions: idle receive detects a server close promptly.
                    try:
                        packet = protocol.receive(time.monotonic() + 0.2)
                        self.emit("rx", packet)
                    except TimeoutError:
                        pass
                    continue
                if self.stop_event.is_set():
                    break
                result, elapsed = protocol.request(kind, data)
                self.emit("result", (kind, data, result, elapsed))
        except Cancelled:
            pass
        except Exception as error:
            self.emit("error", f"{type(error).__name__}: {error}")
        finally:
            self.password = ""
            try:
                if socket is not None:
                    socket.close(timeout=0)
            finally:
                self.emit("closed", None)
