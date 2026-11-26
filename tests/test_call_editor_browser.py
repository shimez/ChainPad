"""Offline Chromium DOM test. Requires playwright; optional --browser / --screenshots."""
import argparse
import json
from pathlib import Path
from urllib.parse import urlsplit

from playwright.sync_api import sync_playwright


WEB = Path(__file__).resolve().parents[1] / "web"


def call(target):
    return dict(protocol="callChain", delayMs=0, targetInput=target)


def run(browser, screenshots):
    for scenario in ("edit", "blocked", "unsupported"):
        cfg = json.loads((WEB / "builtin/ChainPad-Configuration-Empty.json").read_bytes())
        cfg["network"].update(ssid="", passwordConfigured=False, oscHost="127.0.0.1")
        rows = cfg["chains"]
        wait = dict(protocol="wait", delayMs=100)
        rows[0]["actions"] = [wait]
        if scenario == "edit":
            rows[0]["actions"] += [dict(protocol="loopStart", count=2, delayMs=0),
                                   call(27), dict(protocol="loopEnd", delayMs=0)]
            rows[1]["actions"] = [call(0)]
            rows[2]["actions"] = [call(1)]
        if scenario == "blocked":
            for row in rows[1:]:
                row["actions"] = [call(0)]

        writes, errors = [], []
        page = browser.new_page(viewport={"width": 1280, "height": 900})
        page.on("pageerror", lambda e: errors.append(str(e)))

        def serve(route):
            url = urlsplit(route.request.url)
            assert url.hostname == "127.0.0.1"
            if not url.path.startswith("/api/"):
                f = WEB / (Path(url.path).name or "index.html")
                route.fulfill(body=f.read_bytes(), content_type={
                    ".html": "text/html; charset=utf-8", ".js": "text/javascript",
                    ".svg": "image/svg+xml"}[f.suffix])
                return
            if route.request.method != "GET":
                writes.append((url.path, route.request.post_data_json))
            route.fulfill(json={
                "/api/config": cfg, "/api/capabilities": dict(callChainSupported=scenario != "unsupported",
                    defaultMidiTransport="ble", midiTransports=["ble"], keyboardTransports=["ble"]),
                "/api/status": dict(storageState="ready", configOutputsAllowed=True),
                "/api/config/begin": {"token": 1}, "/api/config/commit": {"message": "Saved fixture"},
            }.get(url.path, {}))

        page.route("**/*", serve)
        page.goto("http://127.0.0.1/")
        page.wait_for_function("document.querySelector('#message').textContent.includes('読み込みました')")
        snap = lambda: page.evaluate("JSON.stringify([config,dirty,selected,editingAction])")
        edit = page.locator(".edit-action")
        edit.first.click()
        detail = page.locator(".action-details")
        kind = detail.get_by_label("Action", exact=True)
        note = detail.get_by_role("status")
        if scenario == "unsupported":
            assert not kind.locator("option[value=callChain]").count()
        elif scenario == "blocked":
            before = snap()
            kind.select_option("callChain")
            assert "選択できる呼び出し先がありません" in note.inner_text()
            assert kind.input_value() == "wait" and snap() == before
        else:
            # Clean draft: default target cycles; cancel without changing anything.
            before = snap()
            kind.select_option("callChain")
            pick = note.get_by_label("呼び出し先")
            assert pick.evaluate("el=>el===document.activeElement")
            assert pick.locator("option").count() == 26  # placeholder + 25 valid
            if screenshots:
                detail.screenshot(path=screenshots / "choose-target.png")
            note.get_by_role("button", name="キャンセル").click()
            assert snap() == before and kind.input_value() == "wait"
            # Another unsaved edit remains intact across rejection in a nested Call.
            delay = detail.get_by_label("待機時間 (ms)")
            delay.fill("350")
            delay.press("Tab")
            edit.last.click()
            target = detail.locator("[data-call-target]")
            before = snap()
            assert target.locator("option[value='0']").count() == 0
            for bad in ("1", "2"):
                target.select_option(bad)
                assert target.input_value() == "27" and snap() == before
                assert "循環Call" in note.inner_text() and "KEY1 Press #3" in note.inner_text()
            if screenshots:
                detail.locator("..").screenshot(path=screenshots / "rejected-target.png")
            target.select_option("4")
            assert not note.inner_text() and "Call → KEY3 Press" in page.locator("#actions").inner_text()
            edit.first.click()
            assert delay.input_value() == "350"
            kind.select_option("callChain")
            note.get_by_label("呼び出し先").select_option("27")
            assert target.input_value() == "27"
            kind.select_option("wait")
            assert delay.input_value() == "100"
            # Fixture with an empty default target: conversion stays a single step.
            page.locator("#inputs button").nth(3).click()
            page.locator("#add").click()
            kind.select_option("callChain")
            assert target.input_value() == "7"
            assert not writes
            page.locator("#save").click()
            page.wait_for_function("document.querySelector('#message').textContent==='Saved fixture'")
            saved = [body for path, body in writes if path == "/api/config/chain"]
            assert len(saved) == 28 and saved[0]["actions"][2] == call(4)
            assert saved[6]["actions"] == [call(7)] and not page.evaluate("dirty")
        assert not errors, errors
        if scenario != "edit":
            assert not writes
        page.close()
        print("PASS", scenario)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", help="Chromium executable; default: Playwright Chromium")
    parser.add_argument("--screenshots", type=Path)
    args = parser.parse_args()
    if args.screenshots:
        args.screenshots.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as pw:
        with pw.chromium.launch(executable_path=args.browser) as browser:
            run(browser, args.screenshots)
