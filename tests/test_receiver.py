"""Receiver protocol and actual UDP-to-Tk timeline integration tests."""
import socket
import sys
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/ChainPadReceiver"))
from ble_midi import decode_packet
from receiver import EventBus, Receiver, describe_midi
from pythonosc.udp_client import SimpleUDPClient
import mido
import tkinter as tk


class BleMidiTests(unittest.TestCase):
    def test_note_and_timestamp_wrap(self):
        self.assertEqual(decode_packet(bytes.fromhex("bf ff 90 30 7f")), [bytes.fromhex("90 30 7f")])
        self.assertEqual(decode_packet(bytes.fromhex("80 80 80 30 00")), [bytes.fromhex("80 30 00")])

    def test_multiple_messages_running_status_and_cc(self):
        self.assertEqual(decode_packet(bytes.fromhex("80 80 90 30 7f 81 31 64 82 b0 0a 40")),
                         [bytes.fromhex(s) for s in ["90 30 7f", "90 31 64", "b0 0a 40"]])

    def test_malformed_packets(self):
        for data in [b"", b"\x00\x80\x90\x30\x7f", b"\x80\x80", bytes.fromhex("80 80 90 30"), bytes.fromhex("80 80 f0 01 f7")]:
            with self.subTest(data=data), self.assertRaises(ValueError):
                decode_packet(data)

    def test_zero_velocity_note_on(self):
        self.assertEqual(describe_midi(mido.Message('note_on', note=48, velocity=0)), "Note Off Ch1 Note 48 Velocity 0")

    def test_bus_order_across_threads_and_overflow(self):
        bus = EventBus()
        threads = [threading.Thread(target=lambda: [bus.add("MIDI", "mock", "x") for _ in range(3000)]) for _ in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        stamps = [bus.queue.get_nowait().monotonic_ns for _ in range(10000)]
        self.assertEqual(stamps, sorted(stamps))
        self.assertEqual(bus.dropped, 2000)


class ReceiverIntegrationTests(unittest.TestCase):
    def test_reconnect_replaces_stale_handle_and_reports_open_failure(self):
        root = tk.Tk()
        root.withdraw()
        with patch('mido.get_input_names', return_value=['Test BLE']):
            app = Receiver(root, start_osc=False)
        try:
            old, reopened, other = Mock(), Mock(), Mock()
            app.ports['Test BLE'] = old
            app.ports['Other USB'] = other
            app.transport.set('BLE MIDI')
            with patch('mido.open_input', return_value=reopened) as open_input:
                app.open_port()
                old.close.assert_called_once()
                open_input.assert_called_once()
                self.assertIs(app.ports['Test BLE'], reopened)
                callback = open_input.call_args.kwargs['callback']
                callback(mido.Message('note_on', note=48, velocity=127))
                callback(mido.Message('note_off', note=48, velocity=0))
            with patch('mido.open_input', side_effect=OSError('Device disconnected')):
                app.open_port()
            reopened.close.assert_called_once()
            other.close.assert_not_called()
            self.assertNotIn('Test BLE', app.ports)
            self.assertNotIn('Test BLE', app.opened.get())
            app.drain()
            rows = [values for _, values in app.rows]
            self.assertEqual(sum(row[2] == 'BLE MIDI' for row in rows), 2)
            self.assertTrue(any(row[2] == 'ERROR' and 'Device disconnected' in row[4] for row in rows))
        finally:
            app.close()

    def test_udp_and_mocked_midi_share_timeline(self):
        root = tk.Tk()
        root.withdraw()
        with patch('mido.get_input_names', return_value=['Test USB']):
            app = Receiver(root, start_osc=False)
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.bind(('127.0.0.1', 0))
                port = sock.getsockname()[1]
            app.bind.set('127.0.0.1')
            app.port.set(str(port))
            app.start_osc()
            client = SimpleUDPClient('127.0.0.1', port)
            client.send_message('/Costume', 2)
            client._sock.close()
            def open_input(name, callback):
                callback(mido.Message('note_on', channel=0, note=48, velocity=127))
                class Port:
                    def close(self):
                        pass
                return Port()
            with patch('mido.open_input', side_effect=open_input):
                app.open_port()
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                root.update()
                data = [values for _, values in app.rows if values[2] in ('OSC / Wi-Fi', 'USB MIDI')]
                if len(data) == 2:
                    break
                time.sleep(0.01)
            self.assertEqual({row[2] for row in data}, {'OSC / Wi-Fi', 'USB MIDI'})
            self.assertTrue(any('/Costume int 2' in row[4] for row in data))
            self.assertTrue(any('Note On Ch1 Note 48 Velocity 127' in row[4] for row in data))
            self.assertEqual(data[0][1], '+0.000')
            self.assertGreaterEqual(float(data[1][1]), 0)
        finally:
            app.close()


if __name__ == '__main__':
    unittest.main()
