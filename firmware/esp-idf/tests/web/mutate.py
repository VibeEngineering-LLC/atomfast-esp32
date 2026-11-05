#!/usr/bin/env python3
"""Мутационная приёмка history-core.js (#SA-3): каждая мутация обязана покраснеть. Запуск: python tests/web/mutate.py"""
import os, pathlib, re, subprocess, sys, tempfile
root = pathlib.Path(__file__).resolve().parent.parent.parent
mod, test, muts, envn = (sys.argv[1:5] + ["history-core.js", "history-core.test.js", "mutations.txt", "HC_MODULE"][len(sys.argv[1:5]):])
src = (root / "web" / mod).read_text(encoding="utf-8")
M = [l.split(" => ") for l in (root / "tests" / "web" / muts).read_text(encoding="utf-8").splitlines() if " => " in l]
tmp = pathlib.Path(tempfile.mkdtemp()); res = []
for i, (a, b) in enumerate(M):
    if src.count(a) != 1:
        res.append((i, a, "BAD-PATTERN x%d" % src.count(a))); continue
    d = tmp / str(i); d.mkdir(); p = d / mod
    for sib in (root / "web").glob("*.js"):
        (d / sib.name).write_text(sib.read_text(encoding="utf-8"), encoding="utf-8")
    p.write_text(src.replace(a, b), encoding="utf-8")
    r = subprocess.run(["node", str(root / "tests/web" / test)], env=dict(os.environ, **{envn: str(p)}), capture_output=True, text=True, encoding="utf-8")
    fails = re.findall(r"^FAIL (.+?):", r.stdout, re.M)
    res.append((i, a, "KILLED by %d: %s" % (len(fails), "; ".join(fails)) if fails else "SURVIVED"))
for i, a, st in res: print("%02d %-55s %s" % (i, a[:55], st))
print("mutations: %d, survived/bad: %d" % (len(res), sum(1 for r in res if not r[2].startswith("KILLED"))))
sys.exit(1 if any(not r[2].startswith("KILLED") for r in res) else 0)
