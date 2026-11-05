import json
import math
import urllib.parse

TOKEN = 'mock'
UTC0 = 1792859805
S = {'now': 1000, 'next': 1, 'runs': [], 'time_ok': True, 'log': [], 'scenario': 'empty'}
LIMITS = {'n_max': 1000, 't_min_s': 10, 't_max_s': 3600000, 'pause_max_s': 3600000, 'delay_max_s': 604800, 'res_max': 1000, 'runs_max': 8, 'files_max': 8}
KEYS = {'name', 'n', 'endless', 't_s', 'pause_s', 'retry_invalid', 'lost_max', 'stop_mode', 'eps_pct', 't_min_s', 't_max_s', 'start_mode', 'start_at', 'delay_s',
        'file_store', 'name_template', 'prefix', 'no_overwrite'}
RATE = 5.0


def _send(h, code, body, ctype='application/json', extra=None):
    raw = body if isinstance(body, bytes) else (body if isinstance(body, str) else json.dumps(body, ensure_ascii=False)).encode('utf-8')
    h.send_response(code)
    h.send_header('Content-Type', ctype + '; charset=utf-8')
    h.send_header('Content-Length', str(len(raw)))
    for k, v in (extra or {}).items():
        h.send_header(k, v)
    h.end_headers()
    h.wfile.write(raw)
    return True


def _err(h, code, err):
    return _send(h, code, {'ok': False, 'err': err})


def _num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def _int(v, lo, hi):
    return isinstance(v, int) and not isinstance(v, bool) and lo <= v <= hi


def parse_start(o):
    if not isinstance(o, dict):
        return None, (400, 'bad_body')
    for k in o:
        if k not in KEYS:
            return None, (400, 'unknown_key')
    p = {'name': '', 'n': 1, 't_s': 600, 'pause_s': 0, 'retry_invalid': True, 'lost_max': 2, 'stop_mode': 'time', 'eps_pct': 1, 't_min_s': 10, 't_max_s': 3600,
         'start_mode': 'now', 'file_store': False, 'no_overwrite': True, 'prefix': 'AtomFast', 'name_template': '{prefix}_{n:000}'}
    p.update(o)
    if 'endless' in o:
        if o['endless'] is not True and o['endless'] is not False:
            return None, (400, 'bad_value')
        if o['endless'] and o.get('n', 0) != 0:
            return None, (400, 'bad_value')
        if o['endless']:
            p['n'] = 0
    bad = (not isinstance(p['name'], str) or len(p['name'].encode('utf-8')) > 32 or '\\' in p['name'] or any(ord(c) < 32 for c in p['name'])
           or not _int(p['n'], 0, 1000) or not _int(p['t_s'], 10, 3600000) or not _int(p['pause_s'], 0, 3600000) or not _int(p['lost_max'], 0, 60)
           or p['stop_mode'] not in ('time', 'sigma') or not _num(p['eps_pct']) or not (0.1 <= p['eps_pct'] <= 50)
           or not _int(p['t_min_s'], 1, 3600000) or not _int(p['t_max_s'], 1, 3600000) or p['t_min_s'] > p['t_max_s']
           or p['start_mode'] not in ('now', 'at', 'delay') or not isinstance(p['retry_invalid'], bool) or not isinstance(p['file_store'], bool)
           or not isinstance(p['no_overwrite'], bool))
    if bad:
        return None, (400, 'bad_value')
    if p['stop_mode'] == 'sigma' and p['n'] != 1:
        return None, (400, 'sigma_only_single')
    if p['start_mode'] == 'at' and not _int(o.get('start_at'), 1, 4294967295):
        return None, (400, 'bad_value')
    if p['start_mode'] == 'delay' and not _int(o.get('delay_s'), 1, 604800):
        return None, (400, 'bad_value')
    return p, None

def active():
    for r in S['runs']:
        if r['state'] in ('wait', 'running', 'paused'):
            return r
    return None


def mode_of(n):
    return 'single' if n == 1 else ('endless' if n == 0 else 'series')


def make_run(p, state='running'):
    n = p['n']
    r = dict(id=S['next'], name=p['name'], mode=mode_of(n), state=state, phase='measuring' if state == 'running' else 'none', n=n, done=0,
             cur_i=1, t_s=p['t_s'], pause_s=p['pause_s'], retry_invalid=p['retry_invalid'], lost_max=p['lost_max'], stop_mode=p['stop_mode'],
             eps_pct=p['eps_pct'], t_min_s=p['t_min_s'], t_max_s=p['t_max_s'], start_mode=p['start_mode'], start_utc=UTC0 + (S['now'] - 1000),
             start_mono_s=S['now'], end_mono_s=None, end_reason=None, file_store=p['file_store'], no_overwrite=p['no_overwrite'], prefix=p['prefix'],
             name_template=p['name_template'], dropped=0, el=0, rows=[])
    S['next'] += 1
    S['runs'].insert(0, r)
    return r


def row_for(r, i, t_ms, reason='time', partial=False):
    k = 1.0 + 0.06 * math.sin(i * 1.7)
    counts = max(1, int(round(RATE * k * t_ms / 1000.0)))
    live = 2.0 * max(1, t_ms // 2000)
    cps = counts / live
    sig = math.sqrt(counts) / live
    usv = cps * 0.0056
    return dict(i=i, attempt=1, valid=True, t0_utc=UTC0 + (S['now'] - 1000) - int(t_ms / 1000), t0_mono_s=S['now'] - int(t_ms / 1000), t_ms=t_ms,
                counts=counts, npk=int(live / 2), lost=0, cps=round(cps, 5), cps_sigma=round(sig, 6), usv_h=round(usv, 6), usv_h_sigma=round(usv / math.sqrt(counts), 7),
                eps_pct=round(100.0 / math.sqrt(counts), 4), stop_reason=reason, partial=partial, goal_missed=False, time_jump=False, final=True,
                raw_seq0=0, raw_seq1=0, raw_epoch=0)


def summary(r):
    ok = [x for x in r['rows'] if x['valid']]
    n = len(ok)
    z = dict(rows=len(r['rows']), n_valid=n, n_invalid=len(r['rows']) - n, counts=sum(x['counts'] for x in ok), live_s=sum(2 * x['npk'] for x in ok),
             mean_cps=None, sd_cps=None, sem_cps=None, min_cps=None, max_cps=None, pooled_cps=None, pooled_sigma=None, chi2_ndf=None, mean_usvh=None, dose_usv=None)
    if n:
        cp = [x['cps'] for x in ok]
        m = sum(cp) / n
        z['mean_cps'] = round(m, 5)
        z['min_cps'], z['max_cps'] = min(cp), max(cp)
        z['pooled_cps'] = round(z['counts'] / z['live_s'], 5)
        z['pooled_sigma'] = round(math.sqrt(z['counts']) / z['live_s'], 6)
        z['mean_usvh'] = round(sum(x['usv_h'] for x in ok) / n, 6)
        z['dose_usv'] = round(sum(x['usv_h'] * 2 * x['npk'] for x in ok) / 3600.0, 6)
        if n > 1:
            sd = math.sqrt(sum((c - m) ** 2 for c in cp) / (n - 1))
            z['sd_cps'], z['sem_cps'] = round(sd, 6), round(sd / math.sqrt(n), 6)
            z['chi2_ndf'] = round(sum(((x['cps'] - m) / x['cps_sigma']) ** 2 for x in ok) / (n - 1), 4)
    return z


def live_of(r):
    if r['state'] not in ('wait', 'running', 'paused'):
        return None
    el = r['el'] if r['phase'] == 'measuring' else 0
    counts = int(RATE * el)
    npk = el // 2
    cps = counts / (2.0 * npk) if npk else None
    return dict(i=r['cur_i'], attempt=1, phase=r['phase'], elapsed_ms=el * 1000, remaining_ms=max(0, r['t_s'] - el) * 1000, counts=counts, npk=npk, lost=0,
                cps=cps, cps_sigma=(math.sqrt(counts) / (2.0 * npk)) if npk and counts else None, eps_pct=(100.0 / math.sqrt(counts)) if counts else None,
                n_target=None, eta_s=None)


def view(r):
    n = r['n']
    total = None if n == 0 else n * (r['t_s'] + r['pause_s'])
    keys = ('id', 'name', 'mode', 'state', 'phase', 'n', 'done', 'cur_i', 't_s', 'pause_s', 'retry_invalid', 'lost_max', 'stop_mode', 'eps_pct', 't_min_s',
            't_max_s', 'start_mode', 'start_utc', 'start_mono_s', 'end_mono_s', 'end_reason', 'file_store', 'no_overwrite', 'prefix', 'name_template', 'dropped')
    o = {k: r[k] for k in keys}
    if r['state'] in ('done', 'aborted', 'error'):
        o['cur_i'] = None
    o.update(rows=len(r['rows']), rows_total=r['done'], total_s=total, expected_end_utc=(r['start_utc'] + total) if total and S['time_ok'] else None)
    if not S['time_ok']:
        o['start_utc'] = None
    return o

def finish(r, reason):
    r['state'], r['phase'], r['end_reason'], r['end_mono_s'] = 'done', 'none', reason, S['now']


def tick(secs):
    for _ in range(secs):
        S['now'] += 1
        r = active()
        if not r or r['state'] != 'running':
            continue
        if r['phase'] == 'gap':
            r['el'] += 1
            if r['el'] >= r['pause_s']:
                r['phase'], r['el'] = 'measuring', 0
            continue
        r['el'] += 1
        reason = None
        if r['stop_mode'] == 'sigma':
            if r['el'] >= r['t_max_s']:
                reason = 'limit'
            elif r['el'] >= r['t_min_s'] and RATE * r['el'] >= 1e4 / (r['eps_pct'] ** 2):
                reason = 'sigma'
        elif r['el'] >= r['t_s']:
            reason = 'time'
        if reason:
            row = row_for(r, r['cur_i'], r['el'] * 1000, reason)
            row['goal_missed'] = reason == 'limit'
            r['rows'].append(row)
            r['done'] += 1
            r['el'] = 0
            if r['n'] != 0 and r['done'] >= r['n'] or r['stop_mode'] == 'sigma':
                finish(r, 'time' if reason == 'time' else reason)
            else:
                r['cur_i'] += 1
                r['phase'] = 'gap' if r['pause_s'] > 0 else 'measuring'


def build(name):
    S['runs'], S['next'], S['now'], S['time_ok'] = [], 1, 1000, name != 'notime'
    base = dict(name='', n=1, t_s=600, pause_s=0, retry_invalid=True, lost_max=2, stop_mode='time', eps_pct=1, t_min_s=10, t_max_s=3600, start_mode='now',
                file_store=False, no_overwrite=True, prefix='AtomFast', name_template='{prefix}_{n:000}')
    if name in ('series_done', 'many', 'big', 'invalid'):
        n = {'series_done': 5, 'many': 30, 'big': 500, 'invalid': 6}[name]
        r = make_run(dict(base, name='Фон', n=n, t_s=20))
        for i in range(1, n + 1):
            S['now'] += 20
            r['rows'].append(row_for(r, i, 20000))
        if name == 'invalid':
            r['rows'][2].update(valid=False, lost=3, attempt=1, stop_reason='error')
        r['done'] = n
        finish(r, 'time')
        one = make_run(dict(base, name='Образец', n=1, t_s=120))
        one['rows'].append(row_for(one, 1, 120000))
        one['done'] = 1
        finish(one, 'time')
    if name == 'running':
        r = make_run(dict(base, name='Фон', n=5, t_s=60, pause_s=10))
        r['el'] = 5
    if name == 'endless':
        r = make_run(dict(base, name='Непрерывно', n=0, t_s=30))
        r['el'] = 3
    S['scenario'] = name
    S['log'] = []


def csv_of(r):
    head = 'i,t0_utc,t_s,counts,cps,cps_sigma,usv_h,usv_h_sigma,valid,stop_reason,eps_achieved_pct,attempt,npk,lost_packets,partial,raw_seq0,raw_seq1\n'
    out = []
    for x in r['rows']:
        out.append(','.join(str(v) for v in (x['i'], '', '%.3f' % (x['t_ms'] / 1000.0), x['counts'], x['cps'], x['cps_sigma'], x['usv_h'], x['usv_h_sigma'],
                                              1 if x['valid'] else 0, x['stop_reason'], x['eps_pct'], x['attempt'], x['npk'], x['lost'], 1 if x['partial'] else 0, 0, 0)) + '\n')
    return head + ''.join(out)


def find(rid):
    for r in S['runs']:
        if r['id'] == rid:
            return r
    return None

def _body(h):
    n = int(h.headers.get('Content-Length', 0))
    return h.rfile.read(n).decode('utf-8', 'replace') if n > 0 else ''


def _post(h, path):
    raw = _body(h)
    try:
        o = json.loads(raw) if raw.strip() else None
    except Exception:
        o = '__bad__'
    S['log'].append({'path': path, 'csrf': h.headers.get('X-CSRF-Token'), 'raw': raw, 'body': None if o == '__bad__' else o})
    if h.headers.get('X-CSRF-Token') != TOKEN:
        return _send(h, 403, 'Bad or missing CSRF token', 'text/plain')
    act = path.rsplit('/', 1)[1]
    if o == '__bad__' or len(raw) > 640:
        return _err(h, 400, 'json')
    if act == 'start':
        if o is None:
            o = {}
        p, e = parse_start(o)
        if e:
            return _err(h, e[0], e[1])
        if p['start_mode'] != 'now' and not S['time_ok']:
            return _err(h, 409, 'time_not_synced')
        if active():
            return _err(h, 409, 'busy')
        r = make_run(p, 'running' if p['start_mode'] == 'now' else 'wait')
        return _send(h, 200, {'ok': True, 'id': r['id']})
    if o is not None and (not isinstance(o, dict) or set(o) - {'id'}):
        return _err(h, 400, 'unknown_key')
    rid = o.get('id') if o else None
    if rid is None and act == 'delete':
        return _err(h, 400, 'bad_body')
    if rid is not None and not _int(rid, 1, 4294967295):
        return _err(h, 400, 'bad_value')
    r = find(rid) if rid is not None else active()
    if rid is not None and r is None:
        return _err(h, 404, 'not_found')
    if r is None:
        return _err(h, 409, 'bad_state')
    st = r['state']
    if act == 'stop' and st in ('wait', 'running', 'paused'):
        if r['phase'] == 'measuring' and r['el'] >= 2:
            r['rows'].append(row_for(r, r['cur_i'], r['el'] * 1000, 'manual', True))
            r['done'] += 1
        finish(r, 'manual')
    elif act == 'pause' and st in ('wait', 'running'):
        r['state'], r['phase'], r['el'] = 'paused', 'none', 0
    elif act == 'resume' and st == 'paused':
        r['state'], r['phase'], r['el'] = 'running', 'measuring', 0
    elif act == 'delete' and st in ('done', 'aborted', 'error'):
        S['runs'].remove(r)
    else:
        return _err(h, 409, 'bad_state')
    return _send(h, 200, {'ok': True, 'id': r['id']})


def _get(h, path, q):
    if path == '/api/meas':
        runs = [dict(run=view(r), sum=summary(r), live=live_of(r)) for r in S['runs']]
        a = active()
        return _send(h, 200, {'ok': True, 'time_ok': S['time_ok'], 'now_utc': (UTC0 + S['now'] - 1000) if S['time_ok'] else 0, 'active_id': a['id'] if a else None,
                              'limits': LIMITS, 'runs': runs})
    qs = urllib.parse.parse_qs(q, keep_blank_values=True)
    rid = qs.get('id', [None])[0]
    if rid is not None and not rid.isdigit():
        return _err(h, 400, 'bad_value')
    r = find(int(rid)) if rid is not None else (active() or (S['runs'][0] if S['runs'] else None))
    if r is None:
        return _err(h, 404, 'not_found')
    if path.endswith('.csv'):
        return _send(h, 200, csv_of(r), 'text/csv', {'Content-Disposition': 'attachment; filename="measure-%d.csv"' % r['id']})
    lim = qs.get('limit', ['100'])[0]
    if not lim.isdigit() or not (1 <= int(lim) <= 1000):
        return _err(h, 400, 'bad_value')
    rows = r['rows'][-int(lim):]
    return _send(h, 200, {'ok': True, 'run': view(r), 'sum': summary(r), 'live': live_of(r), 'from_idx': len(r['rows']) - len(rows), 'rows': rows})


def handle(h, method, path, query):
    if path == '/__mock/meas' and method == 'POST':
        q = urllib.parse.parse_qs(query)
        build(q.get('scenario', ['empty'])[0])
        return _send(h, 200, {'ok': True})
    if path == '/__mock/meas-tick' and method == 'POST':
        tick(int(urllib.parse.parse_qs(query).get('s', ['1'])[0]))
        return _send(h, 200, {'ok': True, 'now': S['now']})
    if path == '/__mock/meas-log' and method == 'GET':
        return _send(h, 200, S['log'])
    if method == 'POST' and path in ('/api/meas/start', '/api/meas/stop', '/api/meas/pause', '/api/meas/resume', '/api/meas/delete'):
        return _post(h, path)
    if method == 'GET' and path in ('/api/meas', '/api/meas/results', '/api/meas/results.csv'):
        return _get(h, path, query)
    return False
