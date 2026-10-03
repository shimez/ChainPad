"""ChainPadReceiver: one timeline for OSC, OS MIDI, direct BLE-MIDI and focused HID."""
from __future__ import annotations

import asyncio
import csv
import queue
import threading
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, ttk

import mido
from bleak import BleakClient, BleakScanner
from pythonosc.dispatcher import Dispatcher
from pythonosc.osc_server import BlockingOSCUDPServer

from ble_midi import MIDI_CHARACTERISTIC, MIDI_SERVICE, decode_packet


@dataclass
class Event:
    wall: str
    monotonic_ns: int
    protocol: str
    source: str
    content: str


class EventBus:
    def __init__(self):
        self.queue = queue.Queue(maxsize=10000)
        self.lock = threading.Lock()
        self.dropped = 0

    def add(self, protocol, source, content):
        # Serialize timestamp + enqueue across OSC/MIDI/BLE callback threads.
        with self.lock:
            event = Event(datetime.now().isoformat(timespec="milliseconds"),
                          time.perf_counter_ns(), protocol, source, content)
            try:
                self.queue.put_nowait(event)
            except queue.Full:
                self.dropped += 1


def describe_midi(message):
    channel = f"Ch{message.channel + 1} " if hasattr(message, "channel") else ""
    if message.type in ("note_on", "note_off"):
        kind = "Note Off" if message.type == "note_off" or message.velocity == 0 else "Note On"
        return f"{kind} {channel}Note {message.note} Velocity {message.velocity}"
    if message.type == "control_change":
        return f"CC {channel}Controller {message.control} Value {message.value}"
    return str(message)


class Receiver:
    def __init__(self, root, start_osc=True):
        self.root = root
        self.bus = EventBus()
        self.server = None
        self.osc_thread = None
        self.ports = {}
        self.ble_client = None
        self.ble_devices = {}
        self.last_ns = None
        self.rows = []
        self.closed = False
        self.drain_job = None
        self.ble_loop = asyncio.new_event_loop()
        self.ble_lock = asyncio.Lock()
        self.ble_thread = threading.Thread(target=self.ble_loop.run_forever, daemon=True)
        self.ble_thread.start()
        root.title("ChainPadReceiver — Project Chimera / Phase 1")
        root.geometry("1180x720")
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.build_ui()
        self.refresh_ports()
        if start_osc:
            self.start_osc()
        self.drain()

    def build_ui(self):
        bar = ttk.Frame(self.root, padding=10)
        bar.pack(fill="x")
        ttk.Label(bar, text="OSC bind").pack(side="left")
        self.bind = tk.StringVar(value="0.0.0.0")
        ttk.Entry(bar, textvariable=self.bind, width=16).pack(side="left", padx=5)
        self.port = tk.StringVar(value="9000")
        ttk.Entry(bar, textvariable=self.port, width=7).pack(side="left")
        ttk.Button(bar, text="Start OSC", command=self.start_osc).pack(side="left", padx=5)
        ttk.Button(bar, text="Stop OSC", command=self.stop_osc).pack(side="left")
        self.status = tk.StringVar(value="Ready")
        ttk.Label(bar, textvariable=self.status).pack(side="left", padx=15)

        midi = ttk.LabelFrame(self.root, text="OS MIDI input — transport label is explicit, not auto-detected", padding=10)
        midi.pack(fill="x", padx=10, pady=4)
        self.port_name = ttk.Combobox(midi, state="readonly", width=48)
        self.port_name.pack(side="left")
        self.transport = ttk.Combobox(midi, values=["USB MIDI", "BLE MIDI", "MIDI (OS)"], state="readonly", width=12)
        self.transport.set("USB MIDI")
        self.transport.pack(side="left", padx=5)
        ttk.Button(midi, text="Refresh", command=self.refresh_ports).pack(side="left")
        ttk.Button(midi, text="Open / Reconnect", command=self.open_port).pack(side="left", padx=5)
        ttk.Button(midi, text="Close selected", command=self.close_port).pack(side="left")
        self.opened = tk.StringVar(value="Opened: none")
        ttk.Label(self.root, textvariable=self.opened, wraplength=1100).pack(anchor="w", padx=15)
        ttk.Label(self.root, text="USB / BLE both use OS MIDI ports when available. The transport label changes the log only.").pack(anchor="w", padx=15)

        ble = ttk.LabelFrame(self.root, text="Direct BLE-MIDI — use when BLE MIDI is absent from OS MIDI ports", padding=10)
        ble.pack(fill="x", padx=10, pady=4)
        self.ble_name = ttk.Combobox(ble, state="readonly", width=48)
        self.ble_name.pack(side="left")
        ttk.Button(ble, text="Scan (5s)", command=lambda: self.submit(self.scan_ble())).pack(side="left", padx=5)
        ttk.Button(ble, text="Connect", command=self.connect_ble).pack(side="left")
        ttk.Button(ble, text="Disconnect", command=lambda: self.submit(self.disconnect_ble())).pack(side="left", padx=5)

        hid = ttk.Frame(self.root, padding=10)
        hid.pack(fill="x")
        ttk.Label(hid, text="HID test area (focus here):").pack(side="left")
        entry = ttk.Entry(hid, width=35)
        entry.pack(side="left", padx=5)
        entry.bind("<KeyPress>", lambda e: self.bus.add("Keyboard (focused)", "OS / transport unknown", f"KeyDown {e.keysym} code={e.keycode}"))
        entry.bind("<KeyRelease>", lambda e: self.bus.add("Keyboard (focused)", "OS / transport unknown", f"KeyUp {e.keysym} code={e.keycode}"))
        ttk.Button(hid, text="Clear timeline", command=self.clear).pack(side="left", padx=5)
        ttk.Button(hid, text="Export CSV", command=self.export).pack(side="left")
        ttk.Label(self.root, text="Delta = PC callback arrival interval (all protocols). HID is focus-only; OS hotkeys may be consumed. Last 5000 rows retained.").pack(anchor="w", padx=15)

        frame = ttk.Frame(self.root, padding=10)
        frame.pack(fill="both", expand=True)
        columns = ("time", "delta", "protocol", "source", "event")
        self.table = ttk.Treeview(frame, columns=columns, show="headings")
        for name, title, width in zip(columns, ["Time", "Delta ms", "Protocol / Transport", "Source", "Event"], [185, 85, 155, 230, 460]):
            self.table.heading(name, text=title)
            self.table.column(name, width=width, minwidth=50)
        scroll = ttk.Scrollbar(frame, orient="vertical", command=self.table.yview)
        self.table.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.table.pack(fill="both", expand=True)
        self.summary = tk.StringVar()
        ttk.Label(self.root, textvariable=self.summary).pack(anchor="w", padx=15, pady=5)

    def start_osc(self):
        try:
            port = int(self.port.get())
            if not 1 <= port <= 65535:
                raise ValueError("OSC port must be 1..65535")
            self.stop_osc()
            dispatcher = Dispatcher()
            def receive(source, address, *args):
                values = " ".join(f"{type(value).__name__} {value!r}" for value in args)
                self.bus.add("OSC / Wi-Fi", f"{source[0]}:{source[1]}", f"{address} {values}")
            dispatcher.set_default_handler(receive, needs_reply_address=True)
            self.server = BlockingOSCUDPServer((self.bind.get(), port), dispatcher)
            self.osc_thread = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.1}, daemon=True)
            self.osc_thread.start()
            self.status.set(f"OSC listening {self.bind.get()}:{port}")
        except Exception as error:
            self.status.set(str(error))
            self.bus.add("ERROR", "OSC", str(error))

    def stop_osc(self):
        if self.server:
            self.server.shutdown()
            self.server.server_close()
            self.server = None
            self.status.set("OSC stopped")

    def refresh_ports(self):
        try:
            names = mido.get_input_names()
            self.port_name["values"] = names
            if self.port_name.get() not in names:
                self.port_name.set(names[0] if names else "")
        except Exception as error:
            self.bus.add("ERROR", "OS MIDI", str(error))

    def update_opened(self):
        self.opened.set("Opened: " + (", ".join(self.ports) or "none"))

    def open_port(self):
        name, label = self.port_name.get(), self.transport.get()
        if not name:
            self.bus.add("ERROR", "OS MIDI", "No MIDI input selected. Click Refresh and select a port.")
            return
        try:
            # Like the proven PoC monitor, explicitly reopen on every click.
            # A board restart can invalidate a handle without removing its
            # name from our dictionary or raising a callback error.
            previous = self.ports.pop(name, None)
            if previous is not None:
                previous.close()
            self.ports[name] = mido.open_input(name, callback=lambda msg: self.bus.add(label, name, describe_midi(msg)))
            self.bus.add("INFO", name, f"Opened as {label}")
        except Exception as error:
            self.bus.add("ERROR", name, str(error))
        finally:
            self.update_opened()

    def close_port(self):
        name = self.port_name.get()
        port = self.ports.pop(name, None)
        if port:
            port.close()
        self.update_opened()

    def submit(self, coroutine):
        future = asyncio.run_coroutine_threadsafe(coroutine, self.ble_loop)
        def complete(done):
            try:
                done.result()
            except Exception as error:
                self.bus.add("ERROR", "BLE", str(error))
        future.add_done_callback(complete)

    async def scan_ble(self):
        self.bus.add("INFO", "BLE", "Scanning for MIDI service…")
        results = await BleakScanner.discover(timeout=5, return_adv=True)
        devices = {}
        for device, advertisement in results.values():
            if MIDI_SERVICE in [u.lower() for u in advertisement.service_uuids]:
                devices[f"{advertisement.local_name or device.name or 'BLE MIDI'} [{device.address}]"] = device
        # GUI updates happen exclusively on the Tk thread via the same queue.
        self.bus.add("DEVICES", "BLE", devices)

    def connect_ble(self):
        device = self.ble_devices.get(self.ble_name.get())
        if device:
            self.submit(self.open_ble(device))

    async def open_ble(self, device):
        async with self.ble_lock:
            await self._open_ble(device)

    async def _open_ble(self, device):
        await self._disconnect_ble()
        client = BleakClient(device, disconnected_callback=lambda _: self.bus.add("INFO", "BLE", "Disconnected"))
        self.ble_client = client
        try:
            await client.connect()
            def receive(_, packet):
                try:
                    for raw in decode_packet(bytes(packet)):
                        self.bus.add("BLE MIDI", device.address, describe_midi(mido.Message.from_bytes(raw)))
                except Exception as error:
                    self.bus.add("ERROR", "BLE decoder", f"{error}: {bytes(packet).hex()}")
            await client.start_notify(MIDI_CHARACTERISTIC, receive)
            self.bus.add("INFO", "BLE", f"Subscribed: {device.address}")
        except Exception:
            await client.disconnect()
            self.ble_client = None
            raise

    async def disconnect_ble(self):
        async with self.ble_lock:
            await self._disconnect_ble()

    async def _disconnect_ble(self):
        if self.ble_client:
            client = self.ble_client
            self.ble_client = None
            await client.disconnect()

    async def shutdown_ble(self):
        pending = [task for task in asyncio.all_tasks() if task is not asyncio.current_task()]
        for task in pending:
            task.cancel()
        if pending:
            await asyncio.gather(*pending, return_exceptions=True)
        await self._disconnect_ble()

    def drain(self):
        if self.drain_job is not None:
            self.root.after_cancel(self.drain_job)
            self.drain_job = None
        if self.closed:
            return
        for _ in range(500):
            try:
                event = self.bus.queue.get_nowait()
            except queue.Empty:
                break
            if event.protocol == "DEVICES":
                self.ble_devices = event.content
                names = list(self.ble_devices)
                self.ble_name["values"] = names
                self.ble_name.set(names[0] if names else "")
                continue
            # Diagnostic messages do not change the protocol-event delta.
            is_data = event.protocol not in ("INFO", "ERROR")
            delta = f"{(event.monotonic_ns - self.last_ns) / 1e6:+.3f}" if is_data and self.last_ns is not None else "+0.000" if is_data else "—"
            if is_data:
                self.last_ns = event.monotonic_ns
            values = (event.wall, delta, event.protocol, event.source, event.content)
            row_id = self.table.insert("", "end", values=values)
            self.rows.append((row_id, values))
            if len(self.rows) > 5000:
                old, _ = self.rows.pop(0)
                self.table.delete(old)
            self.table.see(row_id)
        self.summary.set(f"Rows {len(self.rows)} / 5000 · Queue drops {self.bus.dropped}")
        self.drain_job = self.root.after(30, self.drain)

    def clear(self):
        self.table.delete(*self.table.get_children())
        self.rows.clear()
        self.last_ns = None

    def export(self):
        filename = filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV", "*.csv")])
        if filename:
            try:
                with Path(filename).open("w", encoding="utf-8-sig", newline="") as handle:
                    writer = csv.writer(handle)
                    writer.writerow(["time", "delta_ms", "protocol_transport", "source", "event"])
                    writer.writerows(values for _, values in self.rows)
            except OSError as error:
                self.bus.add("ERROR", "CSV", str(error))

    def close(self):
        self.closed = True
        if self.drain_job is not None:
            self.root.after_cancel(self.drain_job)
            self.drain_job = None
        self.stop_osc()
        for port in self.ports.values():
            port.close()
        future = asyncio.run_coroutine_threadsafe(self.shutdown_ble(), self.ble_loop)
        try:
            future.result(timeout=3)
        except Exception:
            pass
        self.ble_loop.call_soon_threadsafe(self.ble_loop.stop)
        self.ble_thread.join(timeout=1)
        if not self.ble_thread.is_alive():
            self.ble_loop.close()
        self.root.destroy()


if __name__ == "__main__":
    window = tk.Tk()
    Receiver(window)
    window.mainloop()
