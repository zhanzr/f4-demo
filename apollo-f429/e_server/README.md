# e_server - embedded web demo (frontend + reference C backend)

A minimal single-page web app for the **apollo-f429** board (STM32F429IGT6),
plus a host-side C backend that mirrors the API the board's embedded server
implements. The web assets are bundled into C arrays by a build script
(inline CSS/JS, gzip the page, embed the images raw) so they can be served
straight from flash on the MCU.

This is a copy of `fire-f429/e_server`, adapted for the apollo-f429 board:

- **LED control** drives both on-board LEDs (LED0 = PB1, LED1 = PB0).
- **Sensors** plots the three internal ADC channels (VREFINT / die
  temperature / VBAT) plus the DHT11 (temperature / humidity) - no IMU.
- The **Camera** tab is removed (no streaming UI on this board).

## Layout

```
e_server/
  web/             raw frontend sources
    index.html     single page, three tabs
    style.css
    app.js
  public/          images served to the page (embedded into web_assets.h)
    apollo_board_0.png
    apollo_board_1.png
  build_web.py     bundle: inline CSS/JS -> gzip page -> C arrays (--out to reuse)
  web_assets.h     generated C arrays (page + images + lookup table)
  server.c         reference C backend (host-side)
  Makefile / build.sh
```

`e_server/` is the **standalone source of truth for the site**. Other boards
and projects embed it by pointing the packer at their own output path, e.g.
`python build_web.py --out <project>/Inc/web_assets.h`.

## Frontend (three tabs, no external libraries)

1. **LED control** - two checkboxes reflecting the board LEDs (LED0 = PB1,
   LED1 = PB0, both low-active). Each click POSTs the new states to the
   backend (no page reload) and the checkboxes re-sync to the server's reply.
   Rapid clicks are coalesced.
2. **Sensors** - five canvas plots: VREFINT / die temperature / VBAT and the
   DHT11 (temperature / humidity). A dropdown picks the sample interval:
   1 s (default), 2 s, 4 s; the choice is persisted in `localStorage`.
   Samples are polled only while the tab is open (leaving the tab stops the
   timer and aborts any pending request). At most one request is in flight at
   a time, so the selected interval is always respected; on timeout/error the
   tick is skipped and the last value stays on screen.
3. **Board info** - architecture, LAN IP, public IP, geo location and weather
   (each falls back to "N/A" if unavailable), a manual *Refresh* button, and
   the board photos from `public/`.

## API (shared contract with the embedded server)

| Route                | Description                                     |
| -------------------- | ----------------------------------------------- |
| `GET /`              | the page: gzipped, CSS/JS inlined (`Content-Encoding: gzip`) |
| `GET /api/leds`      | `{"leds":[0,0]}` ({LED0, LED1} states)          |
| `POST /api/leds`     | body `{"leds":[0,1]}` -> applied, `{"leds":[0,1]}` |
| `GET /api/adc`       | `{"vrefint_mv":..,"temp_c":..,"vbat_v":..,"dht11_t":..,"dht11_h":..,"ts":..}` |
| `GET /api/info`      | `{"arch","lan_ip","public_ip","geo","weather","ts"}` (nulls = unavailable) |
| `GET /public/*`      | raw image bytes (`image/jpeg`) from `embedded_files[]` |

On the host the reference backend reads real ADC-style values from a
simulation and fetches the public IP / geo / weather over plain HTTP
(api.ipify.org, ip-api.com, wttr.in) - best-effort, so offline they come back
as `null` and the page shows "N/A". On the board those fields would come from
an HTTP client (+ TLS for HTTPS upstreams).

## Build & run (host reference)

Needs Python 3 and a host C compiler (gcc/clang). Windows/MinGW adds
`-lws2_32` automatically.

```bash
python build_web.py      # or: make web_assets.h
make                     # or: bash build.sh
./e_server 8080          # or: make run
```

Then open `http://localhost:8080/`.

## Bundling (build_web.py)

```text
[ index.html ] ──┬── inline <style> + <script> ──► index.html ── gzip ──► index_html_gz[]
[ style.css  ] ──┘                                          (raw)        │
[ app.js     ] ──┘                                                          ▼
[ public/*.jpg ] ─────────────────────────► raw byte arrays + embedded_files[]  web_assets.h
```

* The page is served gzipped (browser decompresses via `Content-Encoding: gzip`).
* JPEG/PNG are already compressed, so the images are embedded **raw** with a
  `path -> {ctype, data, len}` lookup table the server routes against.
* Regenerate after any change to `web/` or `public/`; commit `web_assets.h`
  alongside the sources.
* Other projects reuse the packer:
  `python build_web.py --out ../some/project/Inc/web_assets.h`.

## Porting to the board

`web_assets.h` drops straight into flash; `server.c`'s routing is the blueprint
for an embedded server. **Already done** in `app/eth_http_server` on this
board: the custom lwIP raw-TCP server serves this site + API over LAN8720A
(RMII) - `/api/leds` reads the two real LEDs, `/api/adc` reads the internal
ADC channels (VREFINT / die temperature / VBAT via the factory calibration)
and the DHT11 on PB12 (with the required PCF8574 INT release first),
`/api/info` reports `arch` + `lan_ip` with `public_ip`/`geo`/`weather` as
`null` (the board has no HTTP/TLS client yet).