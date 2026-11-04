"""Headless Chrome guest(s) with a fake mic: join a room, Listen, Unmute.

  python3 scripts/guest_mic_browser.py <base> <sessionId> <seconds> [guests]

Prints each guest's codec, whether it heard the other guest, and console errors.
Needs `pip install playwright` and a Chrome/Chromium.
"""
import os, sys, time
from playwright.sync_api import sync_playwright

base, sid, secs = sys.argv[1], sys.argv[2], float(sys.argv[3])
n = int(sys.argv[4]) if len(sys.argv) > 4 else 2
args = ["--use-fake-ui-for-media-stream", "--use-fake-device-for-media-stream",
        "--autoplay-policy=no-user-gesture-required"]
with sync_playwright() as p:
    b = p.chromium.launch(channel="chrome", headless=True, args=args)
    pages, errs = [], []
    for i in range(n):
        ctx = b.new_context(permissions=["microphone"])
        pg = ctx.new_page()
        pg.on("console", lambda m, i=i: m.type == "error" and errs.append(f"g{i}: {m.text}"))
        pg.on("pageerror", lambda e, i=i: errs.append(f"g{i}: {e}"))
        # count mic frames heard from others
        pg.add_init_script("""
          window.__heard = 0;
          const _ws = window.WebSocket;
          window.WebSocket = function(u, p) { const w = new _ws(u, p);
            w.addEventListener('message', e => { if (e.data instanceof ArrayBuffer && e.data.byteLength > 12 &&
              new DataView(e.data).getUint32(4, true) === 0) window.__heard++; }); return w; };
          window.WebSocket.prototype = _ws.prototype; Object.assign(window.WebSocket, _ws);
        """)
        if os.environ.get("NO_OPUS"):  # exercise the PCM16 fallback (no WebCodecs encoder)
            pg.add_init_script("delete window.AudioEncoder;")
        pg.goto(f"{base}/m/{sid}/room?name=Guest{i+1}")
        pg.click("#mic")  # unmute (starts listening too)
        pages.append(pg)
    time.sleep(secs)
    for i, pg in enumerate(pages):
        st = pg.evaluate("({live: GigmaTalk.isLive(), heard: window.__heard, enc: 'AudioEncoder' in window, mic: document.querySelector('#mic').textContent.trim(), people: document.querySelector('#people').innerText})")
        print(f"guest{i+1}: {st}")
    for e in errs[:10]:
        print("ERR", e)
    b.close()
