# b29 G: прогон test_af_settings_http/atype: CURRENT, HEAD (argv[1] - каталог HEAD-копий af_*.c), мутации из test_af_settings_mut.json.
import json, os, shutil, subprocess, sys, tempfile
os.chdir(os.path.dirname(os.path.abspath(__file__)))
SRC = ["af_settings_http.c", "af_store.c", "af_ble_sel_http.c"]
def run(tag, ovr=""):
    mk = subprocess.run(["make", "-s", "-B", "-f", "test_af_settings_http.mk", "OVR=" + ovr, "test_af_settings_http", "test_af_settings_atype"], capture_output=True, text=True)
    print("=== " + tag + ("" if mk.returncode == 0 else " BUILD FAILED\n" + mk.stderr[-2000:]))
    if mk.returncode: return
    for t in ("./test_af_settings_http", "./test_af_settings_atype"):
        out = subprocess.run([t], capture_output=True, text=True).stdout
        print("\n".join(l for l in out.splitlines() if l.startswith(("FAIL", "TESTS")) or tag == "CURRENT"))
run("CURRENT")
if len(sys.argv) > 1: run("HEAD", sys.argv[1])
for m in json.load(open("test_af_settings_mut.json", encoding="utf-8")):
    d = tempfile.mkdtemp()
    for f in SRC: shutil.copy("../../main/" + f, d)
    p = os.path.join(d, m["file"]); s = open(p, encoding="utf-8").read()
    if any(s.count(o) != 1 for o, _ in m["subs"]): print("=== MUT " + m["name"] + ": MUTATION NOT APPLIED"); continue
    for o, n in m["subs"]: s = s.replace(o, n)
    open(p, "w", encoding="utf-8").write(s)
    run("MUT " + m["name"], d); shutil.rmtree(d)
