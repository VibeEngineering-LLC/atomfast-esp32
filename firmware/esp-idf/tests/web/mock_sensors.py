import copy
import json
import sys
import urllib.parse

TOKEN = 'mock'
CAPS = {'bmp280': (True, True, False), 'bme280': (True, True, True), 'sht3x': (True, False, True), 'aht2x': (True, False, True)}
ORDER = ['bmp280', 'bme280', 'sht3x', 'aht2x']
ADDRS = {'bmp280': ['0x76', '0x77'], 'bme280': ['0x76', '0x77'], 'sht3x': ['0x44', '0x45'], 'aht2x': ['0x38']}
ENV_T0 = 1792853220
ENV_ROWS = 200
ENV_STEP = 30
S = {'name': 'one', 'list': [], 'log': [], 'next': 1}


def sensor(n, model, addr, enabled=True, status='ok', err='', t=None, p=None, h=None, n_ok=120, n_err=0, ago=4, period=30,
           t_off=0.0, p_off=0.0, h_off=0.0):
    ct, cp, ch = CAPS[model]
    ok = status == 'ok'
    return {
        'sensor': n, 'model': model, 'addr': addr, 'enabled': enabled, 'status': status, 'err': err,
        'caps': (1 if ct else 0) | (2 if cp else 0) | (4 if ch else 0), 'temp': ct, 'press': cp, 'hum': ch,
        't_c': t if ok and ct else None, 'h_pct': h if ok and ch else None, 'p_mmhg': p if ok and cp else None,
        't_off': t_off, 'p_off': p_off, 'h_off': h_off, 'period_s': period, 'n_ok': n_ok, 'n_err': n_err,
        'store_ok': n_ok, 'store_fail': 0, 'last_ok_ts': 1792859000 if n_ok else 0, 'last_ok_s_ago': ago if n_ok else -1
    }


def build(name):
    if name == 'none':
        return []
    if name == 'one':
        return [sensor(1, 'bmp280', '0x76', t=23.41, p=745.23)]
    if name == 'one_off':
        return [sensor(1, 'bmp280', '0x76', t=23.41, p=745.23, t_off=-0.5, p_off=0.5)]
    if name == 'three':
        return [sensor(1, 'bmp280', '0x76', t=23.41, p=745.23), sensor(2, 'sht3x', '0x44', t=24.12, h=41.35),
                sensor(3, 'aht2x', '0x38', t=23.88, h=39.84, t_off=-0.5)]
    if name == 'two_same':
        return [sensor(1, 'bmp280', '0x76', t=23.41, p=745.23), sensor(2, 'bmp280', '0x77', t=23.95, p=744.98, p_off=0.5)]
    if name == 'all_off':
        return [sensor(1, 'bmp280', '0x76', enabled=False, status='off'), sensor(2, 'sht3x', '0x44', enabled=False, status='off'),
                sensor(3, 'aht2x', '0x38', enabled=False, status='off')]
    if name == 'error':
        return [sensor(1, 'bmp280', '0x76', t=23.41, p=745.23), sensor(2, 'sht3x', '0x44', status='err', err='i2c_nack', n_ok=12, n_err=5, ago=300),
                sensor(3, 'aht2x', '0x38', status='err', err='init', n_ok=0, n_err=3, ago=0)]
    if name == 'eight':
        models = ['bmp280', 'bmp280', 'bme280', 'bme280', 'sht3x', 'sht3x', 'aht2x', 'aht2x']
        out = []
        for i, m in enumerate(models):
            a = ADDRS[m][i % len(ADDRS[m])]
            out.append(sensor(i + 1, m, a, t=22.0 + i * 0.37, p=744.0 + i * 0.4, h=40.0 + i * 1.3))
        return out
    return []


def ordered(lst):
    return sorted(lst, key=lambda s: (ORDER.index(s['model']), s['sensor']))


def reset(name):
    S['name'] = name
    S['list'] = ordered(build(name))
    S['log'] = []
    S['next'] = max([s['sensor'] for s in S['list']] + [0]) + 1


reset('one')


def response():
    lst = ordered(S['list'])
    p = lst[0] if lst else None
    sensor_block = {'status': p['status'] if p else 'no_sensor', 'model': p['model'] if p else 'none', 'addr': p['addr'] if p else '0x00',
                    't_c': p['t_c'] if p else None, 'p_mmhg': p['p_mmhg'] if p else None, 'h_pct': p['h_pct'] if p else None,
                    'temp': p['temp'] if p else False, 'press': p['press'] if p else False, 'hum': p['hum'] if p else False}
    return {
        'board': 'atomfast-s3', 'i2c': {'sda': 8, 'scl': 9, 'scan_done': 1, 'scan': []}, 'sensor': sensor_block,
        'cfg': {'enabled': 1 if p and p['enabled'] else 0, 't_off': p['t_off'] if p else 0.0, 'p_off': p['p_off'] if p else 0.0,
                'h_off': p['h_off'] if p else 0.0, 'agg_s': 10, 'period_s': p['period_s'] if p else 30, 'nvs_ok': 1},
        'live': {'ble': True, 'has_rec': True, 'cps': 11.5, 'rate_usvh': 0.0644, 'rate_urh': 6.44, 'pulses_2s': 23, 'dose_usv': 194.054,
                 'avg_cps': 15.75, 'avg_rate_usvh': 0.0649, 'avg_n': 30, 'avg_age_s': 2},
        'agg_s': 10, 'max_sensors': 8, 'sensors': lst, 'last_scan': {'n': 0, 'changed': 0, 'state': 0}
    }


def _send(h, code, body, ctype='application/json'):
    raw = (json.dumps(body) if not isinstance(body, str) else body).encode('utf-8')
    h.send_response(code)
    h.send_header('Content-Type', ctype)
    h.send_header('Content-Length', str(len(raw)))
    h.end_headers()
    h.wfile.write(raw)


def env_csv():
    head = 'ts,kind,sensor,model,temp_c,hum_pct,press_mmhg,press_pa,flags\n'
    lines = []
    for i in range(ENV_ROWS):
        ts = ENV_T0 + i * ENV_STEP
        for s in ordered(S['list']):
            if not s['enabled'] or s['status'] != 'ok':
                continue
            ct, cp, ch = CAPS[s['model']]
            k = s['sensor']
            t = (s['t_c'] or 0) + 0.8 * (i % 20) / 20.0 + k * 0.1
            hum = '%.2f' % ((s['h_pct'] or 0) + (i % 10) * 0.3) if ch else ''
            pr = (s['p_mmhg'] or 0) + (i % 15) * 0.05 if cp else None
            lines.append('%d,env,%d,%s,%.2f,%s,%s,%s,%d\n' % (ts, k, s['model'], t, hum, ('%.2f' % pr) if cp else '',
                                                               ('%d' % round(pr * 133.322)) if cp else '', 1 | (2 if cp else 0) | (8 if ch else 0)))
    return head + ''.join(lines)


def _body(h):
    n = int(h.headers.get('Content-Length', 0))
    raw = h.rfile.read(n).decode('utf-8') if n > 0 else ''
    return raw, json.loads(raw)


def _post_sensors(h, raw, o):
    if not isinstance(o, dict):
        return _send(h, 400, 'bad body', 'text/plain')
    act = o.get('action')
    if act == 'scan':
        known = [s['addr'] for s in S['list']]
        add = [('sht3x', '0x45'), ('aht2x', '0x38')] if S['name'] != 'eight' else []
        added = 0
        for m, a in add:
            if a not in known and len(S['list']) < 8:
                S['list'].append(sensor(S['next'], m, a, enabled=False, status='off', n_ok=0, n_err=0))
                S['next'] += 1
                added += 1
        return _send(h, 200, {'ok': True, 'found': len(add), 'changed': added})
    if act == 'add':
        m, a = o.get('model'), o.get('addr')
        if m not in CAPS or a not in ADDRS[m]:
            return _send(h, 400, 'bad add', 'text/plain')
        if any(s['addr'] == a for s in S['list']) or len(S['list']) >= 8:
            return _send(h, 409, 'busy', 'text/plain')
        S['list'].append(sensor(S['next'], m, a, enabled=bool(o.get('enabled')), status='off' if not o.get('enabled') else 'err', n_ok=0, err='init'))
        S['next'] += 1
        return _send(h, 200, {'ok': True, 'persisted': True, 'sensor': S['next'] - 1})
    if act == 'remove':
        n = o.get('sensor')
        left = [s for s in S['list'] if s['sensor'] != n]
        if len(left) == len(S['list']):
            return _send(h, 404, 'no sensor', 'text/plain')
        S['list'] = left
        return _send(h, 200, {'ok': True, 'persisted': True})
    n = o.get('sensor')
    tgt = [s for s in S['list'] if s['sensor'] == n]
    if not tgt:
        return _send(h, 404, 'no sensor', 'text/plain')
    s = tgt[0]
    for k in ('t_off', 'p_off', 'h_off', 'period_s'):
        if k in o:
            s[k] = o[k]
    if 'enabled' in o:
        s['enabled'] = bool(o['enabled'])
        s['status'] = 'ok' if s['enabled'] and s['n_ok'] else ('off' if not s['enabled'] else 'err')
    return _send(h, 200, {'ok': True, 'persisted': True})


def handle(h, method, path, query):
    try:
        if method == 'POST' and path == '/__mock/sensors':
            q = urllib.parse.parse_qs(query)
            reset(q.get('scenario', ['one'])[0])
            return _send(h, 200, {'ok': True, 'n': len(S['list'])}) or True
        if method == 'GET' and path == '/__mock/sensors-log':
            return _send(h, 200, S['log']) or True
        if method == 'GET' and path == '/api/sensors':
            S['log'].append({'method': 'GET', 'path': path})
            return _send(h, 200, response()) or True
        if method == 'GET' and path == '/api/history.csv' and 'kind=env' in query and S['name'] in ('three', 'two_same', 'eight', 'error') and 'from=' not in query:
            return _send(h, 200, env_csv(), 'text/csv') or True
        if method == 'POST' and path in ('/api/sensors', '/api/sensors/model'):
            raw, o = _body(h)
            S['log'].append({'method': 'POST', 'path': path, 'csrf': h.headers.get('X-CSRF-Token'), 'body': o})
            if h.headers.get('X-CSRF-Token') != TOKEN:
                return _send(h, 403, 'Bad or missing CSRF token', 'text/plain') or True
            if path == '/api/sensors':
                _post_sensors(h, raw, o)
                return True
            m = o.get('model') if isinstance(o, dict) else None
            if m not in CAPS or not isinstance(o.get('enabled'), bool):
                return _send(h, 400, 'bad model', 'text/plain') or True
            changed = 0
            for s in S['list']:
                if s['model'] == m and s['enabled'] != o['enabled']:
                    s['enabled'] = o['enabled']
                    s['status'] = 'ok' if o['enabled'] and s['n_ok'] else ('off' if not o['enabled'] else 'err')
                    changed += 1
            return _send(h, 200, {'ok': True, 'changed': changed, 'persisted': True}) or True
        return False
    except Exception:
        try:
            _send(h, 500, 'Internal Server Error', 'text/plain')
        except Exception:
            pass
        return True


if __name__ == '__main__' and len(sys.argv) > 2 and sys.argv[1] == 'dump':
    for nm in ['none', 'one', 'three', 'two_same', 'all_off', 'error', 'eight']:
        reset(nm)
        with open(sys.argv[2] + '/sensors-' + nm + '.json', 'w', encoding='utf-8') as f:
            json.dump(response(), f, ensure_ascii=False, indent=1)
