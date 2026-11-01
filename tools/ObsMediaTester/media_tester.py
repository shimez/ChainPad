"""Standalone Tk GUI for observing OBS media behavior before firmware implementation."""
from collections import deque
from datetime import datetime
from decimal import Decimal, DecimalException
import json
import math
import queue
import time
import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, ttk

from obs_client import Worker


MEDIA_KINDS = {"ffmpeg_source", "vlc_source"}
ACTIONS = [("再生 / 再開", "PLAY"), ("一時停止", "PAUSE"), ("停止", "STOP"),
           ("先頭から再生", "RESTART"), ("次へ", "NEXT"), ("前へ", "PREVIOUS")]


def volume_percent(text):
    try:
        value = float(text)
        if not math.isfinite(value) or not 0 <= value <= 100:
            raise ValueError()
        return value
    except (ValueError, TypeError):
        raise ValueError("音量・増減幅は0〜100の数値（%）で指定してください。") from None


def milliseconds(text, relative=False):
    try:
        seconds = Decimal(text)
        ms = seconds * 1000
        if not ms.is_finite() or ms != ms.to_integral_value() or abs(ms) > 9007199254740991:
            raise ValueError()
        if not relative and ms < 0:
            raise ValueError()
        return int(ms)
    except (DecimalException, ValueError):
        raise ValueError("秒数は小数第3位までの数値で指定してください。絶対位置は0以上です。") from None


class App:
    def __init__(self, root):
        self.root = root
        root.title("ChainPad — OBS Media Tester")
        root.geometry("1060x950")
        root.minsize(820, 850)
        self.worker = None
        self.ready = self.busy = False
        self.closing = False
        self.last_poll = 0
        self.inputs = []
        self.logs = deque(maxlen=2000)
        self.controls = []
        self.host = tk.StringVar(value="127.0.0.1")
        self.port = tk.StringVar(value="4455")
        self.password = tk.StringVar()
        self.target = tk.StringVar()
        self.media_only = tk.BooleanVar(value=True)
        self.poll = tk.BooleanVar(value=False)
        self.absolute = tk.StringVar(value="0")
        self.offset = tk.StringVar(value="10")
        self.connection = tk.StringVar(value="未接続")
        self.state = tk.StringVar(value="状態：未取得")
        self.outcome = tk.StringVar(value="操作結果：未実行")
        self.volume = tk.DoubleVar(value=100)
        self.volume_value = tk.StringVar(value="100")
        self.volume_step = tk.StringVar(value="5")
        self.volume_state = tk.StringVar(value="音量：未取得")
        self.slider_label = tk.StringVar(value="100.0 %")
        self.pending_volume = None
        self.relative_volume = None
        self.volume_sent_at = 0
        outer = ttk.Frame(root, padding=10)
        outer.pack(fill="both", expand=True)

        connection = ttk.LabelFrame(outer, text="OBS WebSocket 5.x", padding=8)
        connection.pack(fill="x")
        for column, (label, variable, width) in enumerate([
            ("Host / IP", self.host, 20), ("Port", self.port, 7), ("Password", self.password, 20)
        ]):
            ttk.Label(connection, text=label).grid(row=0, column=column * 2, padx=4)
            ttk.Entry(connection, textvariable=variable, width=width,
                      show="*" if variable is self.password else "").grid(row=0, column=column * 2 + 1)
        self.connect_button = ttk.Button(connection, text="接続", command=self.connect)
        self.connect_button.grid(row=0, column=6, padx=8)
        self.disconnect_button = ttk.Button(connection, text="切断", command=self.disconnect)
        self.disconnect_button.grid(row=0, column=7)
        ttk.Label(connection, textvariable=self.connection).grid(row=1, column=0, columnspan=8, sticky="w", pady=(6, 0))

        source = ttk.LabelFrame(outer, text="対象入力ソース（シーン名ではありません）", padding=8)
        source.pack(fill="x", pady=8)
        self.button(source, "一覧取得", self.refresh).pack(side="left")
        ttk.Checkbutton(source, text="メディア / VLCのみ", variable=self.media_only,
                        command=self.populate).pack(side="left", padx=8)
        self.selector = ttk.Combobox(source, textvariable=self.target, width=38)
        self.selector.pack(side="left", fill="x", expand=True)
        ttk.Label(source, text="手入力も可能").pack(side="left", padx=8)
        self.target.trace_add("write", lambda *_: self.target_changed())

        operations = ttk.LabelFrame(outer, text="TriggerMediaInputAction", padding=8)
        operations.pack(fill="x")
        for caption, action in ACTIONS:
            self.button(operations, caption, lambda action=action: self.media_action(action)).pack(side="left", padx=3)

        position = ttk.LabelFrame(outer, text="再生位置変更（再生中・一時停止中のみ）", padding=8)
        position.pack(fill="x", pady=8)
        ttk.Label(position, text="絶対位置（秒）").grid(row=0, column=0, sticky="w")
        ttk.Entry(position, textvariable=self.absolute, width=15).grid(row=0, column=1, padx=6)
        self.button(position, "指定位置へ", lambda: self.seek(False)).grid(row=0, column=2, padx=6)
        ttk.Label(position, text="相対移動（秒、負数で戻る）").grid(row=1, column=0, sticky="w", pady=6)
        ttk.Entry(position, textvariable=self.offset, width=15).grid(row=1, column=1, padx=6)
        self.button(position, "相対移動", lambda: self.seek(True)).grid(row=1, column=2, padx=6)
        self.button(position, "−10秒", lambda: self.send_target("OffsetMediaInputCursor", {"mediaCursorOffset": -10000})).grid(row=1, column=3, padx=6)
        self.button(position, "＋10秒", lambda: self.send_target("OffsetMediaInputCursor", {"mediaCursorOffset": 10000})).grid(row=1, column=4, padx=6)
        ttk.Label(position, text="末尾を超える指定もそのままOBSへ送ります。シークの可否・精度はソースによります。"
                  ).grid(row=2, column=0, columnspan=5, sticky="w")

        volume = ttk.LabelFrame(outer, text="音量（共通の対象入力ソース / 線形 0〜100%）", padding=8)
        volume.pack(fill="x", pady=(0, 8))
        self.volume_slider = ttk.Scale(volume, from_=0, to=100, variable=self.volume, command=self.slider_changed)
        self.volume_slider.grid(row=0, column=0, columnspan=4, sticky="ew")
        volume.columnconfigure(0, weight=1)
        ttk.Label(volume, textvariable=self.slider_label).grid(row=0, column=4, padx=6)
        self.button(volume, "音量取得", lambda: self.send_target("GetInputVolume")).grid(row=0, column=5)
        ttk.Label(volume, text="増減幅（%ポイント）").grid(row=1, column=0, sticky="w")
        ttk.Entry(volume, textvariable=self.volume_step, width=8).grid(row=1, column=1)
        self.button(volume, "− 減少", lambda: self.adjust_volume(-1)).grid(row=1, column=2, padx=6)
        self.button(volume, "＋ 増加", lambda: self.adjust_volume(1)).grid(row=1, column=3)
        ttk.Entry(volume, textvariable=self.volume_value, width=8).grid(row=2, column=1)
        ttk.Label(volume, text="指定音量（%）").grid(row=2, column=0, sticky="w")
        self.button(volume, "指定音量に設定", self.set_volume).grid(row=2, column=2, columnspan=2, pady=4)
        ttk.Label(volume, textvariable=self.volume_state).grid(row=3, column=0, columnspan=6, sticky="w")
        ttk.Label(volume, text="マイク等は「メディア / VLCのみ」を解除して選択。0%は無音（ミュート設定は変更しません）。").grid(row=4, column=0, columnspan=6, sticky="w")

        status = ttk.LabelFrame(outer, text="GetMediaInputStatus", padding=8)
        status.pack(fill="x")
        self.button(status, "状態取得", self.get_status).pack(side="left")
        ttk.Checkbutton(status, text="0.5秒ごとに取得", variable=self.poll).pack(side="left", padx=8)
        ttk.Label(status, textvariable=self.state).pack(side="left", padx=6)
        ttk.Label(outer, textvariable=self.outcome, wraplength=1000).pack(fill="x", pady=6)
        ttk.Label(outer, text="各操作後に状態を1回取得します。再生制御は表示／非表示を変えません。NEXT / PREVIOUSは対応ソースのみ。"
                  ).pack(fill="x")

        logbar = ttk.Frame(outer)
        logbar.pack(fill="x", pady=(8, 4))
        ttk.Label(logbar, text="通信ログ（時刻・要求・応答・処理時間、最新2000件）").pack(side="left")
        ttk.Button(logbar, text="ログ保存", command=self.save_log).pack(side="right")
        ttk.Button(logbar, text="ログ消去", command=self.clear_log).pack(side="right", padx=6)
        self.log_widget = scrolledtext.ScrolledText(outer, height=16, wrap="word", state="disabled")
        self.log_widget.pack(fill="both", expand=True)
        self.update_controls()
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(50, self.tick)

    def button(self, parent, text, command):
        widget = ttk.Button(parent, text=text, command=command)
        self.controls.append(widget)
        return widget

    def log(self, kind, value):
        body = value if isinstance(value, str) else json.dumps(value, ensure_ascii=False)
        line = f"{datetime.now().isoformat(timespec='milliseconds')} [{kind}] {body}"
        # One text-widget line per log entry, with bounded retained history.
        line = line.replace("\n", "\\n")
        full = len(self.logs) == self.logs.maxlen
        self.logs.append(line)
        self.log_widget.configure(state="normal")
        if full:
            self.log_widget.delete("1.0", "2.0")
        self.log_widget.insert("end", line + "\n")
        self.log_widget.see("end")
        self.log_widget.configure(state="disabled")

    def update_controls(self):
        self.volume_slider.configure(state="normal" if self.ready else "disabled")
        for widget in self.controls:
            widget.configure(state="normal" if self.ready and not self.busy else "disabled")
        self.connect_button.configure(state="disabled" if self.worker else "normal")
        self.disconnect_button.configure(state="normal" if self.worker else "disabled")

    def connect(self):
        host = self.host.get().strip()
        try:
            port = int(self.port.get())
            if not host or any(char in host for char in "/?#@ ") or not 1 <= port <= 65535:
                raise ValueError()
        except ValueError:
            messagebox.showerror("接続設定", "Hostにはホスト名／IP、Portには1〜65535を指定してください。")
            return
        self.inputs = []
        self.selector["values"] = ()
        self.state.set("状態：未取得")
        self.connection.set("接続・認証中…")
        self.worker = Worker(host, port, self.password.get())
        self.worker.start()
        self.update_controls()

    def disconnect(self):
        self.ready = False
        self.clear_volume()
        if self.worker:
            self.worker.stop_event.set()
            self.connection.set("切断中…")
        self.state.set("状態：未取得（切断）")
        self.update_controls()

    def send(self, kind, data):
        if not self.ready or self.busy or not self.worker:
            return
        self.worker.commands.put_nowait((kind, data))
        self.busy = True
        self.last_poll = time.monotonic()
        self.update_controls()

    def send_target(self, kind, extra=None):
        name = self.target.get()
        if not name:
            messagebox.showinfo("対象ソース", "対象ソース名を選択または入力してください。")
            return
        self.send(kind, {"inputName": name, **(extra or {})})

    def refresh(self):
        self.send("GetInputList", {})

    def populate(self):
        candidates = [item["inputName"] for item in self.inputs if not self.media_only.get() or
                      item.get("unversionedInputKind", item.get("inputKind")) in MEDIA_KINDS]
        self.selector["values"] = candidates
        if not self.target.get() and candidates:
            self.target.set(candidates[0])

    def target_changed(self):
        self.state.set("状態：未取得（対象変更）")
        self.clear_volume()

    def clear_volume(self):
        self.pending_volume = self.relative_volume = None
        self.volume_state.set("音量：未取得")

    def slider_changed(self, value):
        percent = volume_percent(value)
        self.slider_label.set(f"{percent:.1f} %")
        if self.ready and self.target.get():
            self.pending_volume = (self.target.get(), percent)

    def flush_volume(self):
        if self.pending_volume and self.ready and not self.busy and time.monotonic() - self.volume_sent_at >= 0.1:
            name, percent = self.pending_volume
            self.pending_volume = None
            if name == self.target.get():
                self.send("SetInputVolume", {"inputName": name, "inputVolumeMul": percent / 100})
                self.volume_sent_at = time.monotonic()

    def set_volume(self):
        try:
            percent = volume_percent(self.volume_value.get())
        except ValueError as error:
            messagebox.showerror("音量", str(error))
            return
        if self.ready and not self.busy:
            self.pending_volume = None
            self.send_target("SetInputVolume", {"inputVolumeMul": percent / 100})

    def adjust_volume(self, direction):
        try:
            delta = direction * volume_percent(self.volume_step.get()) / 100
        except ValueError as error:
            messagebox.showerror("音量", str(error))
            return
        if self.ready and not self.busy and self.target.get():
            self.pending_volume = None
            self.relative_volume = (self.target.get(), delta)
            self.send_target("GetInputVolume")

    def media_action(self, action):
        self.send_target("TriggerMediaInputAction", {"mediaAction": "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_" + action})

    def seek(self, relative):
        try:
            ms = milliseconds((self.offset if relative else self.absolute).get(), relative)
        except ValueError as error:
            messagebox.showerror("再生位置", str(error))
            return
        self.send_target("OffsetMediaInputCursor" if relative else "SetMediaInputCursor",
                         {"mediaCursorOffset" if relative else "mediaCursor": ms})

    def get_status(self):
        self.send_target("GetMediaInputStatus")

    def result(self, payload):
        kind, data, response, elapsed = payload
        self.busy = False
        status = response.get("requestStatus", {})
        success = status.get("result") is True
        self.log("result", {"requestType": kind, "elapsedMs": elapsed, **status})
        if kind != "GetMediaInputStatus" or not success:
            self.outcome.set(f"{kind}: {'成功' if success else '失敗'} / code={status.get('code')} / {elapsed} ms "
                             + status.get("comment", ""))
        same_target = data.get("inputName") == self.target.get()
        relative = self.relative_volume if kind == "GetInputVolume" else None
        if kind == "GetInputVolume":
            self.relative_volume = None
        if success:
            body = response.get("responseData", {})
            if kind == "GetInputList":
                self.inputs = body.get("inputs", [])
                self.populate()
            elif kind == "GetMediaInputStatus" and same_target and self.ready:
                def seconds(value):
                    return "不明" if value is None else f"{value / 1000:.3f}秒"
                self.state.set(f"{body.get('mediaState', '不明').removeprefix('OBS_MEDIA_STATE_')}  "
                               f"位置: {seconds(body.get('mediaCursor'))} / 長さ: {seconds(body.get('mediaDuration'))}")
            elif kind == "GetInputVolume" and same_target and self.ready:
                value = body.get("inputVolumeMul")
                if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value >= 0:
                    self.volume_state.set(f"現在音量: {value * 100:.1f}% / {body.get('inputVolumeDb', '不明')} dB")
                    if self.pending_volume is None:
                        self.volume.set(min(100, value * 100))
                        self.slider_label.set(f"{min(100, value * 100):.1f} %")
                    if relative and relative[0] == self.target.get() and self.pending_volume is None:
                        self.send_target("SetInputVolume", {"inputVolumeMul": max(0, min(1, value + relative[1]))})
                else:
                    self.volume_state.set("音量：不正な応答（設定中止）")
        elif kind == "GetMediaInputStatus" and same_target:
            self.state.set("状態：取得失敗")
        elif kind in {"GetInputVolume", "SetInputVolume"} and same_target:
            self.volume_state.set("音量：操作失敗")
            self.pending_volume = None
        self.update_controls()
        if kind in {"TriggerMediaInputAction", "SetMediaInputCursor", "OffsetMediaInputCursor"} and same_target and self.ready:
            self.get_status()
        if kind == "SetInputVolume" and success and same_target and self.ready:
            self.send_target("GetInputVolume")

    def tick(self):
        if self.worker:
            for _ in range(100):
                try:
                    kind, data = self.worker.events.get_nowait()
                except queue.Empty:
                    break
                if kind == "ready":
                    if not self.worker.stop_event.is_set():
                        self.ready = True
                        self.connection.set(f"接続済み / OBS {data.get('obsStudioVersion')} / WebSocket {data.get('obsWebSocketVersion')}")
                        self.log("connected", data)
                        self.refresh()
                elif kind == "result":
                    self.result(data)
                elif kind == "closed":
                    self.ready = self.busy = False
                    self.clear_volume()
                    self.worker = None
                    self.connection.set("未接続")
                    self.state.set("状態：未取得（切断）")
                    self.log("closed", "切断しました。自動再接続・再送は行いません。")
                    self.update_controls()
                    break
                else:
                    self.log(kind, data)
                    if kind == "error":
                        self.outcome.set(data)
            self.update_controls()
        if self.closing and self.worker is None:
            self.root.destroy()
            return
        self.flush_volume()
        if self.poll.get() and self.ready and not self.busy and self.target.get() and time.monotonic() - self.last_poll >= 0.5:
            self.get_status()
        self.root.after(50, self.tick)

    def clear_log(self):
        self.logs.clear()
        self.log_widget.configure(state="normal")
        self.log_widget.delete("1.0", "end")
        self.log_widget.configure(state="disabled")

    def save_log(self):
        path = filedialog.asksaveasfilename(defaultextension=".log", filetypes=[("UTF-8 log", "*.log"), ("All files", "*.*")])
        if path:
            try:
                with open(path, "w", encoding="utf-8") as output:
                    output.write("\n".join(self.logs) + "\n")
            except OSError as error:
                messagebox.showerror("ログ保存", str(error))

    def close(self):
        self.closing = True
        self.disconnect()


if __name__ == "__main__":
    App(tk.Tk()).root.mainloop()
