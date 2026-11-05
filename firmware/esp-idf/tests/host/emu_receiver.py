from __future__ import annotations

import argparse
import os
import sys
import threading
import urllib.parse


def _setup_paths() -> None:
    listen_dir = os.environ.get("AF_LISTEN_DIR")
    scripts_dir = os.environ.get("AF_SCRIPTS_DIR")
    if not listen_dir or not scripts_dir:
        print("Ошибка: не заданы AF_LISTEN_DIR и/или AF_SCRIPTS_DIR", file=sys.stderr)
        sys.exit(2)
    sys.path.insert(0, listen_dir)
    sys.path.insert(0, scripts_dir)


_setup_paths()

import listen
from af_collector.memdb import MemoryDb
from af_collector.push import ingest_batch

RING_MAP = {"raw": 0, "agg": 1, "env": 2}


def ingest(db, mac, ring_name, epoch, first_seq, lost, csv_text):
    ring_num = RING_MAP[ring_name]
    return ingest_batch(db, mac, ring_num, epoch, first_seq, lost, csv_text)


class EmuHandler(listen.Handler):
    posts: list[dict] = []
    faults: list[dict] = []
    lock = threading.Lock()

    def _send(self, status: int, payload: dict) -> None:
        if getattr(self, "_suppress", False):
            self.close_connection = True
            return
        self._last = (status, payload)
        super()._send(status, payload)

    def do_GET(self) -> None:
        self._suppress = False
        self._last = None
        if self.path == "/ctl/stats" and self.client_address[0] == "127.0.0.1":
            stats = self._build_stats()
            listen.Handler._send(self, 200, stats)
            return
        super().do_GET()

    def do_POST(self) -> None:
        self._suppress = False
        self._last = None

        if self.path.startswith("/ctl/") and self.client_address[0] == "127.0.0.1":
            self._handle_ctl()
            return

        if self.path.startswith("/ingest/"):
            self._handle_ingest()
            return

        super().do_POST()

    def _handle_ctl(self) -> None:
        parsed = urllib.parse.urlsplit(self.path)
        params = urllib.parse.parse_qs(parsed.query)
        path = parsed.path

        if path == "/ctl/fault":
            kind = params.get("kind", [""])[0]
            n = int(params.get("n", ["1"])[0])
            code = params.get("code", [""])[0]
            with self.lock:
                for _ in range(n):
                    self.faults.append({"kind": kind, "code": code})
            listen.Handler._send(self, 200, {"ok": True})
        elif path == "/ctl/token":
            v = params.get("v", [""])[0]
            self.server.token = v
            listen.Handler._send(self, 200, {"ok": True})
        elif path == "/ctl/maxbody":
            v = params.get("v", ["65536"])[0]
            self.server.max_body = int(v)
            listen.Handler._send(self, 200, {"ok": True})
        elif path == "/ctl/reset_db":
            self.server.db = MemoryDb()
            with self.lock:
                self.posts.clear()
                self.faults.clear()
            listen.Handler._send(self, 200, {"ok": True})
        elif path == "/ctl/stop":
            listen.Handler._send(self, 200, {"ok": True})
            threading.Thread(target=self.server.shutdown, daemon=True).start()
        else:
            listen.Handler._send(self, 404, {"error": "unknown ctl command"})

    def _auth_ok(self) -> bool:
        import hmac
        auth = self.headers.get("Authorization", "")
        return hmac.compare_digest(auth.encode(), f"Bearer {self.server.token}".encode())

    def _handle_ingest(self) -> None:
        ring_name = self.path.split("/")[-1]
        first_seq = self.headers.get("X-AF-First-Seq")
        if first_seq is not None:
            try:
                first_seq = int(first_seq)
            except ValueError:
                first_seq = None
        else:
            first_seq = None

        length = int(self.headers.get("Content-Length", 0) or 0)

        with self.lock:
            fault = self.faults.pop(0) if self.faults else None

        if fault:
            kind = fault["kind"]
            code = fault.get("code", "")

            if kind == "code":
                status_code = int(code) if code else 500
                self._send(status_code, {"error": "injected"})
                with self.lock:
                    self.posts.append({
                        "ring": ring_name,
                        "first_seq": first_seq,
                        "status": status_code,
                        "inj": True
                    })
            elif kind == "drop":
                if length > 0:
                    self.rfile.read(length)
                self.close_connection = True
                with self.lock:
                    self.posts.append({
                        "ring": ring_name,
                        "first_seq": first_seq,
                        "status": 0,
                        "inj": True
                    })
            elif kind == "commit_drop":
                self._suppress = True
                super().do_POST()
                committed_status = self._last[0] if self._last else None
                with self.lock:
                    self.posts.append({
                        "ring": ring_name,
                        "first_seq": first_seq,
                        "status": 0,
                        "inj": True,
                        "committed": committed_status
                    })
                self.close_connection = True
        elif self.headers.get("X-AF-Test") == "1" and self._auth_ok():
            # пробный запрос: ответ без обращения к БД
            self._send(200, {"accepted": 0, "last_seq": -1, "test": True})
            with self.lock:
                self.posts.append({"ring": ring_name, "first_seq": first_seq, "status": 200,
                                   "accepted": 0, "last_seq": -1, "inj": False, "test": True, "len": length})
        else:
            self._suppress = False
            super().do_POST()
            status = self._last[0] if self._last else 0
            payload = self._last[1] if self._last else {}
            
            accepted = payload.get("accepted", 0) if isinstance(payload, dict) else 0
            last_seq = payload.get("last_seq") if isinstance(payload, dict) and status == 200 else None
            
            with self.lock:
                self.posts.append({
                    "ring": ring_name,
                    "first_seq": first_seq,
                    "status": status,
                    "accepted": accepted,
                    "last_seq": last_seq,
                    "inj": False,
                    "len": length
                })

    def _build_stats(self) -> dict:
        db = self.server.db
        rows = db.rows
        
        ring_counts = {"raw": 0, "agg": 0, "env": 0}
        ring_max_seq = {"raw": -1, "agg": -1, "env": -1}
        ring_min_seq = {"raw": -1, "agg": -1, "env": -1}
        
        ring_names = {0: "raw", 1: "agg", 2: "env"}
        
        for key in rows.keys():
            if len(key) >= 4:
                ring_num = key[1]
                seq = key[3]
                if ring_num in ring_names:
                    name = ring_names[ring_num]
                    ring_counts[name] += 1
                    if seq > ring_max_seq[name]:
                        ring_max_seq[name] = seq
                    if ring_min_seq[name] == -1 or seq < ring_min_seq[name]:
                        ring_min_seq[name] = seq

        accepted_sum = {"raw": 0, "agg": 0, "env": 0}
        with self.lock:
            posts_copy = list(self.posts)
            for p in posts_copy:
                if p.get("status") == 200 and p.get("inj") is False:
                    ring = p.get("ring")
                    if ring in accepted_sum:
                        accepted_sum[ring] += p.get("accepted", 0)

        return {
            "posts": posts_copy,
            "rows": ring_counts,
            "max_seq": ring_max_seq,
            "min_seq": ring_min_seq,
            "accepted_sum": accepted_sum,
            "hardware": len(db.hardware)
        }


def main() -> int:
    parser = argparse.ArgumentParser(description="Эмулятор приёмника AtomFast")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--token", type=str, required=True)
    args = parser.parse_args()

    db = MemoryDb()
    listen.Handler = EmuHandler
    server = listen.make_server("127.0.0.1", args.port, args.token, ingest, db)

    print("READY", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
