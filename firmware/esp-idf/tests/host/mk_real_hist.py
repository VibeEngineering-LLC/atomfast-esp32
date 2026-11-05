# Генерирует real_hist.h из боевых CSV-выгрузок e7 (raw: первые 1000 строк, agg: все).
import csv, sys
src = sys.argv[1]
def rows(k):
    return list(csv.DictReader(open(f"{src}/e7_final_{k}.csv", encoding="utf-8")))
raw = rows("raw")[:1000]; agg = rows("agg")
o = ["/* Боевые записи e7 (плата AtomFast, выгрузка e7_final_*.csv). Сгенерировано mk_real_hist.py */",
     "typedef struct { unsigned ts; float dose, rate; unsigned p2s; int bat, temp; unsigned fl; } hraw_t;",
     "typedef struct { unsigned ts, period, n; float dose, rate, pulses; int bat, temp; unsigned fl; } hagg_t;",
     "static const hraw_t HRAW[] = {"]
for r in raw:
    o.append(f"    {{{r['ts']}u, {r['dose_msv']}f, {r['doserate_usvh']}f, {int(float(r['pulses_2s']))}u, {r['battery']}, {r['temp_c']}, {r['flags']}u}},")
o += ["};", f"#define HRAW_N {len(raw)}", "static const hagg_t HAGG[] = {"]
for r in agg:
    o.append(f"    {{{r['ts']}u, {r['kind']}u, {r['n']}u, {r['dose_msv']}f, {r['doserate_usvh']}f, {float(r['pulses_2s'])!r}f, {r['battery']}, {r['temp_c']}, {r['flags']}u}},")
o += ["};", f"#define HAGG_N {len(agg)}"]
open("real_hist.h", "w", encoding="utf-8", newline="\n").write("\n".join(o) + "\n")
