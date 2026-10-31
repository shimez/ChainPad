"""Run with: python -m unittest discover -s tools/ObsMediaTester -v"""
import json
import threading
import time
import unittest
from unittest.mock import patch

import websocket

from obs_client import Cancelled, Protocol, Worker, authentication
from media_tester import App, milliseconds


class Socket:
    def __init__(self, messages):
        self.messages = list(messages)
        self.sent = []
        self.closed = False

    def recv(self):
        if not self.messages:
            raise websocket.WebSocketTimeoutException()
        value = self.messages.pop(0)
        return json.dumps(value) if isinstance(value, dict) else value

    def send(self, value):
        self.sent.append(json.loads(value))

    def settimeout(self, _):
        pass

    def close(self, **_):
        self.closed = True


def reply(request_id="media-test-1", success=True):
    return {"op": 7, "d": {"requestType": "TriggerMediaInputAction", "requestId": request_id,
                            "requestStatus": {"result": success, "code": 100 if success else 604}}}


class ProtocolTests(unittest.TestCase):
    def test_authentication_reference_vector(self):
        # Shared reference vector used by the firmware protocol tests.
        self.assertEqual(authentication("test-password", "test-salt", "test-challenge"),
                         "zBIloly70ANx+T9nUkQVuTCqTUuHtfrqEdn8XDQ3m/s=")

    def test_authenticated_and_unauthenticated_handshake(self):
        for authenticated in (False, True):
            hello = {"rpcVersion": 1, "obsStudioVersion": "32.0", "obsWebSocketVersion": "5.6"}
            if authenticated:
                hello["authentication"] = {"salt": "salt", "challenge": "challenge"}
            socket = Socket([{"op": 0, "d": hello}, {"op": 2, "d": {"negotiatedRpcVersion": 1}}])
            log = []
            protocol = Protocol(socket, lambda *entry: log.append(entry), threading.Event())
            self.assertEqual(protocol.identify("秘密")["rpcVersion"], 1)
            identify = socket.sent[0]["d"]
            self.assertEqual(identify["eventSubscriptions"], 0)
            self.assertEqual("authentication" in identify, authenticated)
            if authenticated:
                self.assertEqual(identify["authentication"], authentication("秘密", "salt", "challenge"))
            self.assertEqual(log, [])

    def test_request_matching_and_obs_error(self):
        socket = Socket([{"op": 5, "d": {}}, reply("other"), reply(success=False)])
        protocol = Protocol(socket, lambda *_: None, threading.Event())
        body, elapsed = protocol.request("TriggerMediaInputAction", {"inputName": "動画 日本語", "mediaAction": "STOP"})
        self.assertFalse(body["requestStatus"]["result"])
        self.assertEqual(body["requestStatus"]["code"], 604)
        self.assertGreaterEqual(elapsed, 0)
        self.assertEqual(len(socket.sent), 1)
        self.assertEqual(socket.sent[0]["d"]["requestData"]["inputName"], "動画 日本語")

    def test_timeout_disconnect_and_cancellation(self):
        protocol = Protocol(Socket([]), lambda *_: None, threading.Event())
        with self.assertRaises(TimeoutError):
            protocol.receive(time.monotonic() - 1)
        protocol.stop.set()
        with self.assertRaises(Cancelled):
            protocol.receive(time.monotonic() + 1)
        protocol.stop.clear()
        protocol.socket = Socket([""])
        with self.assertRaises(ConnectionError):
            protocol.receive(time.monotonic() + 1)

    def test_worker_does_not_retry_after_lost_response(self):
        socket = Socket([{"op": 0, "d": {"rpcVersion": 1}}, {"op": 2}, ""])
        worker = Worker("127.0.0.1", 4455, "secret")
        worker.commands.put(("TriggerMediaInputAction", {"inputName": "video"}))
        with patch("obs_client.websocket.create_connection", return_value=socket):
            worker.start()
            worker.join(timeout=2)
        self.assertFalse(worker.is_alive())
        self.assertTrue(socket.closed)
        self.assertEqual(worker.password, "")
        self.assertEqual([item["op"] for item in socket.sent], [1, 6])
        kinds = []
        while not worker.events.empty():
            kinds.append(worker.events.get()[0])
        self.assertIn("error", kinds)
        self.assertEqual(kinds[-1], "closed")

    def test_seconds_conversion(self):
        self.assertEqual(milliseconds("30.125"), 30125)
        self.assertEqual(milliseconds("-10.001", True), -10001)
        self.assertEqual(milliseconds("0"), 0)
        for value in ("NaN", "Infinity", "-1", "0.0001", "hello", "1e30"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                milliseconds(value)


class GuiTests(unittest.TestCase):
    def setUp(self):
        import tkinter as tk
        self.root = tk.Tk()
        self.root.withdraw()
        self.app = App(self.root)
        self.app.worker = Worker("127.0.0.1", 4455, "")  # Queues only; no network thread.
        self.app.ready = True

    def tearDown(self):
        self.root.destroy()

    def test_filter_manual_name_and_action_then_status(self):
        app = self.app
        app.inputs = [{"inputName": "movie", "inputKind": "ffmpeg_source"},
                      {"inputName": "playlist", "inputKind": "vlc_source"},
                      {"inputName": "browser", "inputKind": "browser_source"}]
        app.populate()
        self.assertEqual(tuple(app.selector["values"]), ("movie", "playlist"))
        app.target.set("custom plugin")
        app.media_action("RESTART")
        kind, data = app.worker.commands.get_nowait()
        self.assertEqual(data, {"inputName": "custom plugin", "mediaAction": "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_RESTART"})
        app.result((kind, data, {"requestStatus": {"result": True, "code": 100}}, 2))
        self.assertEqual(app.worker.commands.get_nowait(), ("GetMediaInputStatus", {"inputName": "custom plugin"}))

    def test_paused_null_and_stale_target_status(self):
        app = self.app
        app.target.set("movie")
        body = {"requestStatus": {"result": True}, "responseData": {
            "mediaState": "OBS_MEDIA_STATE_PAUSED", "mediaDuration": 100000, "mediaCursor": 30125}}
        app.result(("GetMediaInputStatus", {"inputName": "movie"}, body, 1))
        self.assertIn("PAUSED", app.state.get())
        self.assertIn("30.125秒", app.state.get())
        app.target.set("different")
        app.result(("GetMediaInputStatus", {"inputName": "movie"}, body, 1))
        self.assertIn("未取得", app.state.get())
        body["responseData"] = {"mediaState": "OBS_MEDIA_STATE_STOPPED", "mediaDuration": None, "mediaCursor": None}
        app.result(("GetMediaInputStatus", {"inputName": "different"}, body, 1))
        self.assertIn("不明", app.state.get())

    def test_position_requests_and_busy_guard(self):
        app = self.app
        app.target.set("movie")
        app.offset.set("-2.125")
        app.seek(True)
        self.assertEqual(app.worker.commands.get_nowait(), ("OffsetMediaInputCursor", {"inputName": "movie", "mediaCursorOffset": -2125}))
        app.media_action("PLAY")  # Busy: no duplicate queue admission.
        self.assertTrue(app.worker.commands.empty())
        app.busy = False
        app.absolute.set("30")
        app.seek(False)
        self.assertEqual(app.worker.commands.get_nowait(), ("SetMediaInputCursor", {"inputName": "movie", "mediaCursor": 30000}))


if __name__ == "__main__":
    unittest.main()
