"""C6/S3 Loop HTTP/OSC probe; real-device use, not a firmware simulator.

Backs up public configuration before changes and restores it in finally.
Wi-Fi password is never fetched or replaced. Use --backup-only before flashing.
Results measure PC HTTP RTT / OSC arrival, not physical input-edge latency.
"""
import argparse
import copy
import json
import math
import socket
import statistics
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path


def request(base, path, method="GET", body=None):
    data = None if body is None else json.dumps(body, ensure_ascii=False).encode()
    req = urllib.request.Request(base + "/api/" + path, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    start = time.perf_counter()
    with urllib.request.urlopen(req, timeout=2) as response:
        result = json.load(response)
    return result, (time.perf_counter() - start) * 1000


def apply(base, config):
    # Public GET omits the password: omission preserves the device credential.
    network = {k: v for k, v in config["network"].items() if k != "passwordConfigured"}
    token = request(base, "config/begin", "POST",
                    {"schemaVersion": 2, "network": network})[0]["token"]
    for chain in sorted(config["chains"], key=lambda c: c["input"]):
        request(base, f"config/chain?token={token}&input={chain['input']}", "PUT", chain)
    request(base, f"config/rotation?token={token}", "PUT", config["encoderRotation"])
    request(base, f"config/commit?token={token}", "POST")
    restored = request(base, "config")[0]
    if restored != config:
        raise RuntimeError("GET configuration does not exactly match the applied snapshot")


def summary(values):
    ordered = sorted(values)
    return {"count": len(values), "medianMs": statistics.median(values),
            "p95Ms": ordered[math.ceil(len(ordered) * .95) - 1], "maxMs": max(values)} if values else {}


class Receiver:
    def __init__(self, port):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("0.0.0.0", port))
        self.socket.settimeout(.1)
        self.done = threading.Event()
        self.lock = threading.Lock()
        self.counts = {}
        self.last = {}
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.done.is_set():
            try:
                data, _ = self.socket.recvfrom(65535)
            except socket.timeout:
                continue
            name = data.split(b"\0", 1)[0].decode("utf-8", errors="replace")
            with self.lock:
                self.counts[name] = self.counts.get(name, 0) + 1
                self.last[name] = time.perf_counter()

    def snapshot(self):
        with self.lock:
            return dict(self.counts), dict(self.last)

    def close(self):
        self.done.set()
        self.thread.join()
        self.socket.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--backup-only", action="store_true")
    parser.add_argument("--restore", type=Path)
    parser.add_argument("--samples", type=int, default=200)
    parser.add_argument("--port", type=int, default=19090)
    parser.add_argument("--case", choices=["baseline", "empty-depth8", "empty-32", "osc-no-wait", "osc-32", "stop-no-wait"])
    parser.add_argument("--physical-seconds", type=int, default=0,
                        help="Run OSC Loop while the user presses KEY2; always restore afterwards")
    args = parser.parse_args()
    if args.samples < 1 or args.physical_seconds < 0:
        parser.error("samples must be positive; physical-seconds must be nonnegative")
    base = "http://" + args.host
    args.output.mkdir(parents=True, exist_ok=True)
    if args.restore:
        apply(base, json.loads(args.restore.read_text(encoding="utf-8")))
        print("Original configuration restored and GET verified", flush=True)
        return
    status = request(base, "status")[0]
    original = request(base, "config")[0]
    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup = args.output / f"config-{stamp}.json"
    backup.write_text(json.dumps(original, ensure_ascii=False, indent=2), encoding="utf-8")
    (args.output / f"status-{stamp}.json").write_text(json.dumps(status, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Backup: {backup}; hardware={status['hardware']}", flush=True)
    if args.backup_only:
        return
    if request(base, "capabilities")[0].get("maxLoopCount") != 999:
        raise RuntimeError("Device is not running the Loop-enabled firmware")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.connect((args.host, args.port))
        local_ip = route.getsockname()[0]
    receiver = Receiver(args.port)
    start = {"protocol": "loopStart", "delayMs": 0, "count": 999}
    end = {"protocol": "loopEnd", "delayMs": 0}
    osc = {"protocol": "osc", "transport": "wifi", "delayMs": 0,
           "address": "/loop-test/load", "type": "int", "value": 1}
    stop = {"protocol": "stopOtherChains", "delayMs": 0}
    busy = [start, start, osc, end, end]
    cases = [("baseline", [], 0), ("empty-depth8", [start] * 8 + [end] * 8, 1),
             ("osc-no-wait", busy, 1), ("osc-32", busy, 32),
             ("stop-no-wait", [start, start, stop, osc, end, end], 1)]
    if args.case == "empty-32":
        cases = [("empty-32", [start] * 8 + [end] * 8, 32)]
    if args.physical_seconds:
        cases = [("physical-key2", busy, 1)]
    elif args.case:
        cases = [c for c in cases if c[0] == args.case]
    report = {"hardware": status["hardware"], "firmware": status["version"],
              "backup": str(backup), "sink": f"{local_ip}:{args.port}", "cases": [], "setup": []}
    report_path = args.output / f"results-{stamp}.json"
    try:
        for name, actions, invocations in cases:
            test = copy.deepcopy(original)
            test["network"]["oscHost"], test["network"]["oscPort"] = local_ip, args.port
            for c in test["chains"]:
                c["actions"] = []
            test["encoderRotation"]["mode"] = "actionChain"
            test["chains"][0]["actions"] = actions
            test["chains"][2]["actions"] = [{**osc, "address": "/loop-test/key"}]
            apply(base, test)
            for index in range(invocations):
                try:
                    _, trigger_ms = request(base, "trigger", "POST", {"input": 0})
                    live, status_ms = request(base, "status")
                    step = {"case": name, "invocation": index + 1, "triggerMs": trigger_ms,
                            "statusMs": status_ms, "running": live["runningChains"],
                            "loopIntervalMaxUs": live.get("obs", {}).get("loopIntervalMaxUs")}
                except (OSError, ValueError) as exc:
                    step = {"case": name, "invocation": index + 1, "error": str(exc)}
                    report["setup"].append(step)
                    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
                    raise
                report["setup"].append(step)
                print("SETUP " + json.dumps(step), flush=True)
                report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
            before = request(base, "status")[0]
            counts_before, _ = receiver.snapshot()
            rtts, trigger_rtts, failures = [], [], []
            print(f"CASE {name}: started {invocations}; KEY2 is the probe key", flush=True)
            samples = args.physical_seconds * 10 if args.physical_seconds else args.samples
            for _ in range(samples):
                t0 = time.perf_counter()
                try:
                    _, ms = request(base, "status")
                    rtts.append(ms)
                    if not args.physical_seconds:
                        _, ms = request(base, "trigger", "POST", {"input": 2})
                        trigger_rtts.append(ms)
                except (OSError, ValueError) as exc:
                    failures.append(str(exc))
                time.sleep(max(0, .1 - (time.perf_counter() - t0)))
            panic_start = time.perf_counter()
            _, panic_ms = request(base, "panic", "POST")
            # Observation window, not waiting for a background tool job.
            time.sleep(2)
            after = request(base, "status")[0]
            counts_after, last = receiver.snapshot()
            tail = max(0, (last.get("/loop-test/load", panic_start) - panic_start) * 1000)
            result = {"name": name, "http": summary(rtts), "triggerHttp": summary(trigger_rtts),
                      "errors": failures, "panicRttMs": panic_ms, "lastLoadAfterPanicRequestMs": tail,
                      "oscReceived": {k: v - counts_before.get(k, 0) for k, v in counts_after.items()},
                      "before": before, "after": after}
            report["cases"].append(result)
            report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
            print(json.dumps({k: result[k] for k in
                              ("name", "http", "triggerHttp", "errors", "panicRttMs", "oscReceived")}), flush=True)
            if after["runningChains"] != 0:
                raise RuntimeError("Panic did not cancel all invocations")
            if after["uptimeMs"] < before["uptimeMs"]:
                raise RuntimeError("Unexpected device restart during probe")
    finally:
        try:
            request(base, "panic", "POST")
        finally:
            try:
                apply(base, original)
                report["restored"] = True
                print("Original configuration restored and GET verified", flush=True)
            finally:
                receiver.close()
                report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Results: {report_path}", flush=True)


if __name__ == "__main__":
    main()
