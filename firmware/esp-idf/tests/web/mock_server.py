import http.server
import json
import sys
import time
import urllib.parse
from pathlib import Path
import mock_gh
import mock_board
import mock_hist
import mock_sensors
import mock_ble
import mock_clear
import mock_meas

MOCK_HANDLERS = (mock_meas, mock_clear, mock_ble, mock_sensors, mock_hist, mock_board, mock_gh)


def dispatch(handler, method, path, query):
    for module in MOCK_HANDLERS:
        if module.handle(handler, method, path, query):
            return True
    return False

BASE = {"raw": 2000, "agg": 80}
START_TIME = time.time()
TIME_SOURCE = "none"

def load_data(csv_dir):
    data = {"raw": [], "agg": []}
    status = {}
    
    try:
        raw_path = Path(csv_dir) / "e7_preota_raw.csv"
        if raw_path.exists():
            with open(raw_path, "r", encoding="utf-8") as f:
                lines = f.readlines()
            if lines:
                data["raw"] = lines[1:]
    except Exception:
        pass

    try:
        agg_path = Path(csv_dir) / "e7_preota_agg.csv"
        if agg_path.exists():
            with open(agg_path, "r", encoding="utf-8") as f:
                lines = f.readlines()
            if lines:
                data["agg"] = lines[1:]
    except Exception:
        pass

    try:
        status_path = Path(csv_dir) / "e7_preota_status.json"
        if status_path.exists():
            with open(status_path, "r", encoding="utf-8") as f:
                status = json.load(f)
    except Exception:
        pass

    return data, status

def env_count():
    """e10: число записей env, растёт на 1 каждые 30 с (боевой период датчика)."""
    return 120 + int((time.time() - START_TIME) / 30)

def env_ts0():
    try:
        return int(DATA["raw"][0].split(",")[0])
    except Exception:
        return 1761300000

def env_row(i):
    ts = env_ts0() + 30 * i
    t = 26.5 + 0.01 * (i % 20)
    p_mm = 758.3 - 0.02 * (i % 30)
    return "%d,env,%.2f,%.2f,%d,3\n" % (ts, t, p_mm, round(p_mm * 133.322387))

SENSORS = {"board": "ESP32-S3-DevKitC-1", "i2c": {"sda": 8, "scl": 9, "scan_done": 1, "scan": ["0x76"]},
           "sensor": {"status": "ok", "model": "BMP280", "addr": "0x76", "chip_id": "0x58", "caps": 3, "temp": True, "press": True, "hum": False,
                      "t_c": 26.51, "p_mmhg": 751.7, "t_raw_c": 26.51, "p_raw_mmhg": 751.7, "n_ok": 12, "n_err": 0, "store_ok": 12, "store_fail": 0,
                      "last_ok_s_ago": 14, "last_ok_ts": 1761300000, "err": "", "rec_rc": 0},
           "cfg": {"enabled": 1, "t_off": 0.0, "p_off": 0.0, "agg_s": 5, "period_s": 30, "nvs_ok": 1},
           "live": {"ble": True, "has_rec": True, "cps": 0.5, "rate_usvh": 0.11, "rate_urh": 11.0, "pulses_2s": 1, "dose_usv": 1.234,
                    "avg_cps": 0.45, "avg_rate_usvh": 0.105, "avg_n": 3, "avg_age_s": 2}}

def get_visible_count(kind, total_rows):
    elapsed = time.time() - START_TIME
    if __import__("os").environ.get("MOCK_ALL"):
        return total_rows
    growth = int(elapsed / 2)
    return min(total_rows, BASE[kind] + growth)

class MockHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def _send_json(self, data, status_code=200):
        body = json.dumps(data).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_text(self, text, content_type="text/plain", status_code=200, extra_headers=None):
        body = text.encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        if extra_headers:
            for k, v in extra_headers.items():
                self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)
        if dispatch(self, "GET", path, parsed.query):
            return

        if path in ("/history", "/settings", "/readings", "/system", "/measure", "/series", "/"):
            try:
                html_path = Path(__file__).resolve().parent.parent.parent / "web" / "history.html"
                if html_path.exists():
                    content = html_path.read_bytes()
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html")
                    self.send_header("Content-Length", str(len(content)))
                    self.end_headers()
                    self.wfile.write(content)
                else:
                    self._send_text("Not Found", "text/plain", 404)
            except Exception:
                self._send_text("Internal Server Error", "text/plain", 500)
            return

        if path == "/__lc":
            # layout harness: runs tests/web/layout-check.js with the config from ?cfg=<json>
            sname = query.get("script", ["layout-check.js"])[0]
            if sname not in ("layout-check.js", "measure-top.js", "ui-e2e.js", "ui-e2e2.js", "ui-e2e5.js", "ui-e2e6.js", "ui-e2e7.js", "ui-e2e8.js", "ui-e2e9.js", "ui-e2e10.js", "ui-e2e11.js", "ui-e2e12.js", "ui-e2e13.js", "ui-e2e14.js", "ui-e2e15.js", "shot-sensors.js", "dbg.js", "shot-range.js", "shot-colors.js", "shot-fw.js", "shot-fwb.js", "shot-axes.js"):
                sname = "layout-check.js"
            js = (Path(__file__).resolve().parent / sname).read_text(encoding="utf-8")
            cfg = query.get("cfg", ["{}"])[0]
            page = ("<!doctype html><meta charset=utf-8><pre id=out>running</pre><script>window.__LC=" + cfg +
                    ";" + js.rstrip().rstrip(";") + ".then(function(r){document.getElementById('out').textContent=r;});</script>")
            self._send_text(page, "text/html")
            return

        if path == "/api/history/status":
            visible_raw = get_visible_count("raw", len(DATA["raw"]))
            visible_agg = get_visible_count("agg", len(DATA["agg"]))
            
            status_copy = json.loads(json.dumps(STATUS))
            
            if "raw" in status_copy and isinstance(status_copy["raw"], dict):
                status_copy["raw"]["count"] = visible_raw
                status_copy["raw"]["seq"] = visible_raw
            if "agg" in status_copy and isinstance(status_copy["agg"], dict):
                status_copy["agg"]["count"] = visible_agg
                status_copy["agg"]["seq"] = visible_agg
            
            n_env = env_count()
            status_copy["env"] = {"cap": 40320, "limit": 40320, "count": n_env, "seq": n_env, "oldest": 0, "dropped": 0, "rejected": 0, "epoch": 1}
            status_copy["persist"] = {
                "flushes": 3,
                "last_flush_ms": 412,
                "flush_errs": 0,
                "since_flush_s": 95
            }
            
            self._send_json(status_copy)
            return

        if path == "/api/status":
            fw_code, fw_ver = mock_gh.fw_state()
            if fw_code != 200:
                self._send_text("rebooting", "text/plain", fw_code)
                return
            self._send_json({
                "wifi_connected": True, "ble_ready": True, "ble_connected": True,
                "ble_errors": 0, "ble_reconnects": 2, "ble_rx_total": 5230, "ble_bad_total": 1, "ble_last_rec": {"age_ms": 1800, "battery": 27},
                "fw_version": fw_ver, "running_partition": "ota_0",
                "uptime_s": int(time.time() - START_TIME) + 93784, "boot_count": 14, "reset_reason": "software",
                "time_source": TIME_SOURCE, "sntp_synced": False,
                "mqtt": {"enabled": True, "connected": True, "published": 412, "failed": 0, "last_err": ""}
            })
            return

        if path == "/api/system":
            self._send_json({
                "free_heap": 3298560, "min_free_heap": 3201024, "psram_total": 8386560, "psram_free": 7712768,
                "int_free": 98304, "int_min": 93215, "flash_total": 1048576, "flash_used": 317440,
                "rssi": -58, "ssid": "demo-net", "net_mode": "sta"
            })
            return

        if path == "/api/mqtt":
            self._send_json({"enabled": True, "host": "mqtt.local", "port": 1883, "user": "af", "pass_set": True,
                             "period_s": 60, "prefix": "homeassistant"})
            return

        if path == "/api/sensors":
            self._send_json(SENSORS)
            return

        if path == "/api/csrf-token":
            self._send_json({"token": "mock"})
            return

        if path == "/api/history.csv":
            kind = query.get("kind", ["raw"])[0]
            if kind == "env":
                since = int(query.get("since", ["0"])[0])
                limit = int(query.get("limit", ["200000"])[0])
                n_env = env_count()
                rows = [env_row(i) for i in range(since, min(n_env, since + limit))]
                body = ("ts,kind,temp_c,press_mmhg,press_pa,flags\n" + "".join(rows)).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/csv")
                self.send_header("X-AF-Seq", str(n_env))
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            if kind not in ("raw", "agg"):
                self._send_text("Bad Request", "text/plain", 400)
                return

            try:
                since = int(query.get("since", ["0"])[0])
                limit = int(query.get("limit", ["200000"])[0])
            except ValueError:
                self._send_text("Bad Request", "text/plain", 400)
                return

            if since < 0 or limit < 0:
                self._send_text("Bad Request", "text/plain", 400)
                return

            total_rows = len(DATA[kind])
            visible = get_visible_count(kind, total_rows)
            
            if since >= visible:
                rows = []
            else:
                end_idx = min(since + limit, visible)
                rows = DATA[kind][since:end_idx]

            header = "ts,kind,dose_msv,doserate_usvh,pulses_2s,cps,battery,temp_c,flags,n\n"
            output_lines = [header] + rows
            body = "".join(output_lines).encode("utf-8")
            
            self.send_response(200)
            self.send_header("Content-Type", "text/csv")
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-AF-Seq", str(visible))
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        self._send_text("Not Found", "text/plain", 404)

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        if dispatch(self, "POST", path, parsed.query):
            return

        if path == "/api/time":
            content_length = int(self.headers.get("Content-Length", 0))
            if content_length > 0:
                self.rfile.read(content_length)
            
            global TIME_SOURCE
            TIME_SOURCE = "manual"
            
            self._send_json({
                "ok": True,
                "accepted": True,
                "source": "manual",
                "sntp": False
            })
            return

        if path in ("/api/sensors", "/api/ble-reconnect", "/api/reboot-esp"):
            n = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(n) if n > 0 else b""
            if path == "/api/sensors":
                try:
                    SENSORS["cfg"].update({k: v for k, v in json.loads(body).items() if k in SENSORS["cfg"]})
                except Exception:
                    pass
            self._send_json({"ok": True, "persisted": True})
            return

        self._send_text("Not Found", "text/plain", 404)

def main():
    global DATA, STATUS
    
    port = 8765
    csv_dir = "C:/Temp/atomfast_work/e8"
    
    args = sys.argv[1:]
    if len(args) >= 1:
        try:
            port = int(args[0])
        except ValueError:
            pass
    if len(args) >= 2:
        csv_dir = args[1]

    DATA, STATUS = load_data(csv_dir)
    mock_hist.configure(DATA, lambda: [env_row(i) for i in range(env_count())])

    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), MockHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == "__main__":
    main()
