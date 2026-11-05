#!/usr/bin/env python3
"""Snapshot of element ids and API calls of the web pages: python scripts/ids_snapshot.py <web dir> > file"""
import pathlib, re, sys
web = pathlib.Path(sys.argv[1])
ids, apis = set(), set()
for name in ("index.html", "index.src.html", "history.src.html", "history-core.js", "history-series.js", "history-sensors.js", "common-time.js", "system-view.html", "system-view.js", "ui-chrome.html", "system-nav.js", "home-model.js", "sections.js"):
    p = web / name
    if not p.exists():
        continue
    t = p.read_text(encoding="utf-8")
    ids |= {"id:" + m for m in re.findall(r'\bid="([^"]+)"', t)}
    apis |= {"api:" + m for m in re.findall(r"['\"`](/api/[A-Za-z0-9_./-]+)", t)}
for x in sorted(ids | apis):
    print(x)
