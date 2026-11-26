"""Offline Chromium DOM test. Requires playwright; optional --browser / --screenshots."""
import argparse
import json
from pathlib import Path
from urllib.parse import urlsplit

from playwright.sync_api import sync_playwright


WEB = Path(__file__).resolve().parents[1] / "web"


def call(target):
    return dict(protocol="callChain", delayMs=0, targetInput=target)


def run(browser, screenshots, embedded=False):
    embedded_html = None
    if embedded:
        # Exercise the exact single-page asset produced by the normal build.
        header = (WEB.parent / "src/web_assets.h").read_text(encoding="utf-8")
        embedded_html = header.split('WEB_UI[] PROGMEM = R"CHIMERA(', 1)[1].split(')CHIMERA";', 1)[0]
    for scenario in ("edit", "blocked", "unsupported", "mobile"):
        cfg = json.loads((WEB / "builtin/ChainPad-Configuration-Empty.json").read_bytes())
        cfg["network"].update(ssid="", passwordConfigured=False, oscHost="127.0.0.1")
        rows = cfg["chains"]
        wait = dict(protocol="wait", delayMs=100)
        rows[0]["actions"] = [wait]
        if scenario in ("edit", "mobile"):
            rows[0]["actions"] += [dict(protocol="loopStart", count=2, delayMs=0),
                                   call(27), dict(protocol="loopEnd", delayMs=0)]
            rows[1]["actions"] = [call(0)]
            rows[2]["actions"] = [call(1)]
        if scenario == "blocked":
            for row in rows[1:]:
                row["actions"] = [call(0)]

        writes, errors = [], []
        page = browser.new_page(viewport={"width": 1280, "height": 900})
        if scenario == "mobile":
            page.set_viewport_size({"width": 375, "height": 812})
        page.on("pageerror", lambda e: errors.append(str(e)))

        def serve(route):
            url = urlsplit(route.request.url)
            assert url.hostname == "127.0.0.1"
            if not url.path.startswith("/api/"):
                f = WEB / (Path(url.path).name or "index.html")
                route.fulfill(body=embedded_html if embedded and f.name == "index.html" else f.read_bytes(), content_type={
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
        if scenario == "mobile":
            edit.last.click()
            target = detail.get_by_label("呼び出し先（Call target）", exact=True)
            assert target.evaluate("el=>el.tagName") == "SELECT"
            groups = target.locator("optgroup").evaluate_all(
                "gs=>gs.map(g=>[g.label,[...g.children].map(o=>[+o.value,o.textContent])])")
            expected = []
            for label, ids in [("キー（Keys）", range(1, 24)), ("Encoder Push", range(24, 26)),
                               ("回転（Encoder Rotation）", range(26, 28))]:
                choices = []
                for i in ids:
                    name = ("Encoder " + ("CW", "CCW")[i-26] if i >= 26 else
                            ("Encoder Push" if i >= 24 else f"KEY{i//2+1}") +
                            (" Press", " Release")[i % 2])
                    choices.append([i, name + ("" if rows[i]["actions"] else " — Empty（空）")])
                expected.append([label, choices])
            assert groups == expected
            target.focus()
            target.press("ArrowUp")  # CCW -> CW
            assert target.input_value() == "26"
            target.press("ArrowUp")  # cross optgroup -> Encoder Push Release
            assert target.input_value() == "25"
            box = target.bounding_box()
            assert 0 <= box["x"] and box["x"] + box["width"] <= 375
            assert page.locator(".action-summary").last.inner_text() == "Call → Encoder Push Release"
            if screenshots:
                detail.locator("..").screenshot(path=screenshots / "mobile-loop-target.png")
            edit.last.click()
            assert not detail.count() and page.locator(".action-summary").last.inner_text() == "Call → Encoder Push Release"
            page.locator("#inputs button").nth(4).click()  # paste on KEY5, not KEY1
            page.locator("#keyPresetMenu summary").click()
            hint = page.locator("#keyImportHint")
            assert hint.is_visible() and "絶対ID" in hint.inner_text()
            if screenshots:
                page.locator(".preset-menu-items").screenshot(path=screenshots / "key-import-hint.png")
            with page.expect_file_chooser() as chooser:
                page.locator("#importKey").click()
            key = dict(format="chainpad-key-preset", version=1, chains=[
                dict(input=0, actions=[call(25)]), dict(input=1, actions=[call(26)])])
            chooser.value.set_files(dict(name="example.json", mimeType="application/json",
                                         buffer=json.dumps(key).encode()))
            page.wait_for_function("document.querySelector('#message').textContent.includes('Importしました')")
            assert page.evaluate("config.chains.slice(8,10).map(c=>c.actions[0].targetInput)") == [25, 26]
            assert page.locator(".action-summary").inner_text() == "Call → Encoder Push Release"
        elif scenario == "unsupported":
            assert not kind.locator("option[value=callChain]").count()
        elif scenario == "blocked":
            kind.select_option("callChain")
            assert kind.input_value() == "callChain" and "循環Call" in note.inner_text()
            page.locator("#save").click()
            assert "循環Call" in page.locator("#message").inner_text()
            kind.select_option("wait")
            assert not note.inner_text()
        else:
            # Reproduce both old cards reported on hardware: no pending UI.
            for old in ("osc", "obs"):
                kind.select_option(old)
                kind.select_option("callChain")
                assert detail.locator("select").count() == 2 and not detail.locator("input").count()
                assert kind.input_value() == "callChain"
                assert detail.locator("[data-call-target]").input_value() == "1"
                assert "循環Call" in note.inner_text() and not detail.get_by_text("キャンセル").count()
                detail.locator("[data-call-target]").select_option("3")
                assert not note.inner_text()
            kind.select_option("wait")
            # Another unsaved edit remains intact across rejection in a nested Call.
            delay = detail.get_by_label("待機時間 (ms)")
            delay.fill("350")
            delay.press("Tab")
            edit.last.click()
            target = detail.locator("[data-call-target]")
            assert target.locator("option[value='0']").count() == 0
            for bad in ("1", "2"):
                target.select_option(bad)
                assert target.input_value() == bad and page.evaluate("config.chains[0].actions[2].targetInput") == int(bad)
                assert "循環Call" in note.inner_text() and "KEY1 Press #3" in note.inner_text()
                assert "循環を解消しないと保存できません" in note.inner_text()
                assert note.evaluate("e=>getComputedStyle(e).color") == "rgb(255, 157, 153)"
                page.locator("#save").click()
                assert not writes and page.evaluate("dirty")
            if screenshots:
                detail.locator("..").screenshot(path=screenshots / "cycle-warning.png")
            target.select_option("4")
            assert not note.inner_text() and "Call → KEY3 Press" in page.locator("#actions").inner_text()
            edit.first.click()
            assert delay.input_value() == "350"
            kind.select_option("callChain")
            target.select_option("27")
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
    parser.add_argument("--embedded", action="store_true", help="Use the normal build's generated WEB_UI")
    args = parser.parse_args()
    if args.screenshots:
        args.screenshots.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as pw:
        with pw.chromium.launch(executable_path=args.browser) as browser:
            run(browser, args.screenshots, args.embedded)
